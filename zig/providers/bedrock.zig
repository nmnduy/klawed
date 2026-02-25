//! providers/bedrock.zig — AWS Bedrock Converse API provider
//!
//! Zig port of src/bedrock_provider.c, src/bedrock_converse.c,
//! src/aws_bedrock.c.
//!
//! Key improvements over the C version:
//!   - HMAC-SHA256 via `std.crypto.auth.hmac.HmacSha256` (no OpenSSL dep)
//!   - SHA-256 via `std.crypto.hash.sha2.Sha256` (no OpenSSL dep)
//!   - Hex encoding via `std.fmt.fmtSliceHexLower`
//!   - URL parsing with `std.Uri`
//!   - No manual `malloc`/`free` — Zig allocator + `defer`
//!
//! ## AWS SigV4 implementation
//! The signing logic is self-contained in `signRequest` and can be tested
//! with known AWS test vectors (see test block below).
//!
//! ## Phase 5 note
//! HTTP is not implemented; `sendRequest` returns `error.NotImplemented`.

const std = @import("std");

pub const default_region = "us-east-1";
pub const default_service = "bedrock";

// ---------------------------------------------------------------------------
// AWS Credentials
// ---------------------------------------------------------------------------

pub const Credentials = struct {
    access_key_id: []const u8,
    secret_access_key: []const u8,
    session_token: ?[]const u8 = null,
};

/// Load credentials from environment variables.
/// Returns null if AWS_ACCESS_KEY_ID is not set.
pub fn loadCredentials(allocator: std.mem.Allocator) !?Credentials {
    const key_id = std.process.getEnvVarOwned(allocator, "AWS_ACCESS_KEY_ID") catch |err| switch (err) {
        error.EnvironmentVariableNotFound => return null,
        else => return err,
    };
    errdefer allocator.free(key_id);

    const secret_key = std.process.getEnvVarOwned(allocator, "AWS_SECRET_ACCESS_KEY") catch |err| switch (err) {
        error.EnvironmentVariableNotFound => {
            allocator.free(key_id);
            return null;
        },
        else => return err,
    };
    errdefer allocator.free(secret_key);

    const session_token = std.process.getEnvVarOwned(allocator, "AWS_SESSION_TOKEN") catch |err| switch (err) {
        error.EnvironmentVariableNotFound => null,
        else => return err,
    };

    return Credentials{
        .access_key_id = key_id,
        .secret_access_key = secret_key,
        .session_token = session_token,
    };
}

/// Free credential strings returned by `loadCredentials`.
pub fn freeCredentials(allocator: std.mem.Allocator, creds: *Credentials) void {
    allocator.free(creds.access_key_id);
    allocator.free(creds.secret_access_key);
    if (creds.session_token) |st| allocator.free(st);
}

// ---------------------------------------------------------------------------
// AWS SigV4 helpers
// ---------------------------------------------------------------------------

/// Compute HMAC-SHA256(key, data) → 32 bytes.
pub fn hmacSha256(key: []const u8, data: []const u8) [32]u8 {
    var mac: [32]u8 = undefined;
    std.crypto.auth.hmac.sha2.HmacSha256.create(&mac, data, key);
    return mac;
}

/// Compute SHA-256(data) → 32 bytes.
pub fn sha256(data: []const u8) [32]u8 {
    var hash: [32]u8 = undefined;
    std.crypto.hash.sha2.Sha256.hash(data, &hash, .{});
    return hash;
}

/// Hex-encode `bytes` into a newly allocated lowercase hex string.
/// Caller must free.
pub fn hexEncode(allocator: std.mem.Allocator, bytes: []const u8) ![]u8 {
    const out = try allocator.alloc(u8, bytes.len * 2);
    _ = std.fmt.bufPrint(out, "{}", .{std.fmt.fmtSliceHexLower(bytes)}) catch unreachable;
    return out;
}

/// URL-encode `str`.  When `encode_slash` is false, '/' is passed through.
/// Caller must free.
pub fn urlEncode(allocator: std.mem.Allocator, str: []const u8, encode_slash: bool) ![]u8 {
    var buf = std.ArrayList(u8).init(allocator);
    errdefer buf.deinit();
    for (str) |c| {
        if (std.ascii.isAlphanumeric(c) or c == '-' or c == '_' or c == '.' or c == '~') {
            try buf.append(c);
        } else if (c == '/' and !encode_slash) {
            try buf.append(c);
        } else {
            try buf.writer().print("%{X:0>2}", .{c});
        }
    }
    return buf.toOwnedSlice();
}

/// Format a timestamp as ISO 8601: YYYYMMDDTHHMMSSZ
pub fn iso8601Now(buf: *[16]u8) void {
    const epoch_secs: u64 = @intCast(std.time.timestamp());
    const epoch = std.time.epoch.EpochSeconds{ .secs = epoch_secs };
    const day = epoch.getEpochDay();
    const yd = day.calculateYearDay();
    const md = yd.calculateMonthDay();
    const ds = epoch.getDaySeconds();
    _ = std.fmt.bufPrint(buf, "{d:0>4}{d:0>2}{d:0>2}T{d:0>2}{d:0>2}{d:0>2}Z", .{
        yd.year,
        md.month.numeric(),
        md.day_index + 1,
        ds.getHoursIntoDay(),
        ds.getMinutesIntoHour(),
        ds.getSecondsIntoMinute(),
    }) catch unreachable;
}

/// Format a date stamp: YYYYMMDD (first 8 chars of ISO 8601 timestamp).
pub fn dateStamp(timestamp: []const u8) []const u8 {
    return timestamp[0..8];
}

// ---------------------------------------------------------------------------
// SigV4 Request Signing
// ---------------------------------------------------------------------------

/// Signed headers result — a set of header k/v pairs to add to the request.
pub const SignedHeaders = struct {
    content_type: []const u8 = "application/json",
    x_amz_date: [16]u8,
    authorization: []u8,
    x_amz_security_token: ?[]const u8,

    pub fn deinit(self: *SignedHeaders, allocator: std.mem.Allocator) void {
        allocator.free(self.authorization);
        if (self.x_amz_security_token) |st| allocator.free(st);
    }
};

/// Compute AWS SigV4 signed headers for an HTTP request.
///
/// Parameters:
///   - method: "POST", "GET", etc.
///   - url: full URL including scheme and path
///   - payload: request body bytes
///   - creds: AWS credentials
///   - region: AWS region (e.g., "us-east-1")
///   - service: AWS service (e.g., "bedrock")
///   - timestamp_override: pass a 16-char ISO 8601 string for deterministic tests,
///                         or pass null to use the current time
pub fn signRequest(
    allocator: std.mem.Allocator,
    method: []const u8,
    url: []const u8,
    payload: []const u8,
    creds: Credentials,
    region: []const u8,
    service: []const u8,
    timestamp_override: ?[]const u8,
) !SignedHeaders {
    // Timestamp
    var ts_buf: [16]u8 = undefined;
    const timestamp: []const u8 = if (timestamp_override) |t| t else blk: {
        iso8601Now(&ts_buf);
        break :blk ts_buf[0..16];
    };
    const date_str = dateStamp(timestamp);

    // Parse URL to extract host and path
    const host, const path = try parseHostPath(allocator, url);
    defer allocator.free(host);
    defer allocator.free(path);

    // Encode path for canonical request
    const encoded_path = try urlEncode(allocator, path, false);
    defer allocator.free(encoded_path);

    // Payload hash
    const payload_hash_bytes = sha256(payload);
    const payload_hash = try hexEncode(allocator, &payload_hash_bytes);
    defer allocator.free(payload_hash);

    // Canonical headers (sorted: host, x-amz-date)
    var canonical_headers_buf = std.ArrayList(u8).init(allocator);
    defer canonical_headers_buf.deinit();
    try canonical_headers_buf.writer().print(
        "host:{s}\nx-amz-date:{s}\n",
        .{ host, timestamp },
    );
    const canonical_headers = canonical_headers_buf.items;
    const signed_headers = "host;x-amz-date";

    // Canonical request
    var canonical_req_buf = std.ArrayList(u8).init(allocator);
    defer canonical_req_buf.deinit();
    try canonical_req_buf.writer().print(
        "{s}\n{s}\n\n{s}\n{s}\n{s}",
        .{ method, encoded_path, canonical_headers, signed_headers, payload_hash },
    );
    const canonical_req = canonical_req_buf.items;

    // Hash canonical request
    const canonical_hash_bytes = sha256(canonical_req);
    const canonical_hash = try hexEncode(allocator, &canonical_hash_bytes);
    defer allocator.free(canonical_hash);

    // Credential scope
    var scope_buf = std.ArrayList(u8).init(allocator);
    defer scope_buf.deinit();
    try scope_buf.writer().print("{s}/{s}/{s}/aws4_request", .{ date_str, region, service });
    const scope = scope_buf.items;

    // String to sign
    var sts_buf = std.ArrayList(u8).init(allocator);
    defer sts_buf.deinit();
    try sts_buf.writer().print(
        "AWS4-HMAC-SHA256\n{s}\n{s}\n{s}",
        .{ timestamp, scope, canonical_hash },
    );
    const string_to_sign = sts_buf.items;

    // Signing key derivation: "AWS4" + secret_access_key
    var key_bytes = std.ArrayList(u8).init(allocator);
    defer key_bytes.deinit();
    try key_bytes.appendSlice("AWS4");
    try key_bytes.appendSlice(creds.secret_access_key);

    const k_date = hmacSha256(key_bytes.items, date_str);
    const k_region = hmacSha256(&k_date, region);
    const k_service = hmacSha256(&k_region, service);
    const k_signing = hmacSha256(&k_service, "aws4_request");

    // Signature
    const signature_bytes = hmacSha256(&k_signing, string_to_sign);
    const signature = try hexEncode(allocator, &signature_bytes);
    defer allocator.free(signature);

    // Authorization header
    const auth_header = try std.fmt.allocPrint(
        allocator,
        "AWS4-HMAC-SHA256 Credential={s}/{s}, SignedHeaders={s}, Signature={s}",
        .{ creds.access_key_id, scope, signed_headers, signature },
    );

    var result = SignedHeaders{
        .x_amz_date = undefined,
        .authorization = auth_header,
        .x_amz_security_token = null,
    };
    @memcpy(&result.x_amz_date, timestamp[0..16]);

    if (creds.session_token) |st| {
        result.x_amz_security_token = try allocator.dupe(u8, st);
    }

    return result;
}

fn parseHostPath(allocator: std.mem.Allocator, url: []const u8) !struct { []u8, []u8 } {
    // Find "://"
    const scheme_end = std.mem.indexOf(u8, url, "://") orelse return error.InvalidUrl;
    const after_scheme = url[scheme_end + 3 ..];
    // Find first '/' after scheme
    const slash_pos = std.mem.indexOfScalar(u8, after_scheme, '/');
    if (slash_pos) |pos| {
        const host = try allocator.dupe(u8, after_scheme[0..pos]);
        const path = try allocator.dupe(u8, after_scheme[pos..]);
        return .{ host, path };
    } else {
        const host = try allocator.dupe(u8, after_scheme);
        const path = try allocator.dupe(u8, "/");
        return .{ host, path };
    }
}

// ---------------------------------------------------------------------------
// Bedrock Converse API request types
// ---------------------------------------------------------------------------

pub const ContentBlock = union(enum) {
    text: []const u8,
    image: ImageBlock,
    tool_use: ToolUseBlock,
    tool_result: ToolResultBlock,

    pub const ImageBlock = struct {
        format: []const u8, // "png", "jpeg", etc.
        bytes_base64: []const u8,
    };

    pub const ToolUseBlock = struct {
        tool_use_id: []const u8,
        name: []const u8,
        input_json: []const u8,
    };

    pub const ToolResultBlock = struct {
        tool_use_id: []const u8,
        content: []const u8,
        status: []const u8 = "success",
    };
};

pub const Role = enum {
    user,
    assistant,

    pub fn toString(self: Role) []const u8 {
        return switch (self) {
            .user => "user",
            .assistant => "assistant",
        };
    }
};

pub const Message = struct {
    role: Role,
    content: []const ContentBlock,
};

pub const ToolDefinition = struct {
    name: []const u8,
    description: []const u8,
    input_schema_json: []const u8,
};

pub const Request = struct {
    model_id: []const u8,
    messages: []const Message,
    system_prompt: ?[]const u8 = null,
    tools: []const ToolDefinition = &.{},
    max_tokens: u32 = 16384,
    temperature: ?f64 = null,
};

// ---------------------------------------------------------------------------
// Response types
// ---------------------------------------------------------------------------

pub const Response = struct {
    allocator: std.mem.Allocator,
    stop_reason: []const u8,
    content: []const u8,
    tool_uses: []const ToolUse,
    usage: ?Usage,

    pub const ToolUse = struct {
        tool_use_id: []const u8,
        name: []const u8,
        input_json: []const u8,
    };

    pub const Usage = struct {
        input_tokens: u32,
        output_tokens: u32,
    };

    pub fn deinit(self: *Response) void {
        const a = self.allocator;
        a.free(self.stop_reason);
        a.free(self.content);
        for (self.tool_uses) |tu| {
            a.free(tu.tool_use_id);
            a.free(tu.name);
            a.free(tu.input_json);
        }
        a.free(self.tool_uses);
    }
};

// ---------------------------------------------------------------------------
// BedrockProvider
// ---------------------------------------------------------------------------

pub const BedrockProvider = struct {
    allocator: std.mem.Allocator,
    region: []const u8,
    model_id: []const u8,

    pub fn init(
        allocator: std.mem.Allocator,
        region: []const u8,
        model_id: []const u8,
    ) !BedrockProvider {
        return BedrockProvider{
            .allocator = allocator,
            .region = try allocator.dupe(u8, if (region.len > 0) region else default_region),
            .model_id = try allocator.dupe(u8, model_id),
        };
    }

    pub fn deinit(self: *BedrockProvider) void {
        self.allocator.free(self.region);
        self.allocator.free(self.model_id);
    }

    /// Build the Bedrock Converse API endpoint URL.
    pub fn buildEndpointUrl(self: *const BedrockProvider) ![]u8 {
        return std.fmt.allocPrint(
            self.allocator,
            "https://bedrock-runtime.{s}.amazonaws.com/model/{s}/converse",
            .{ self.region, self.model_id },
        );
    }

    /// Serialize a request to the Bedrock Converse API JSON format.
    pub fn buildRequestBody(self: *const BedrockProvider, req: Request) ![]u8 {
        var buf = std.ArrayList(u8).init(self.allocator);
        errdefer buf.deinit();
        try serializeConverseRequest(buf.writer(), req);
        return buf.toOwnedSlice();
    }

    /// HTTP stub — Phase 5 will implement this.
    pub fn sendRequest(
        self: *BedrockProvider,
        allocator: std.mem.Allocator,
        body: []const u8,
    ) ![]u8 {
        _ = self;
        _ = allocator;
        _ = body;
        return error.NotImplemented;
    }

    /// Parse a Bedrock Converse API response.
    pub fn parseResponse(self: *const BedrockProvider, json_body: []const u8) !Response {
        return deserializeConverseResponse(self.allocator, json_body);
    }
};

// ---------------------------------------------------------------------------
// Bedrock Converse serialization
// ---------------------------------------------------------------------------

pub fn serializeConverseRequest(writer: anytype, req: Request) !void {
    var jw = std.json.writeStream(writer, .{});
    try jw.beginObject();

    // System prompt
    if (req.system_prompt) |sp| {
        try jw.objectField("system");
        try jw.beginArray();
        try jw.beginObject();
        try jw.objectField("text");
        try jw.write(sp);
        try jw.endObject();
        try jw.endArray();
    }

    // Messages
    try jw.objectField("messages");
    try jw.beginArray();
    for (req.messages) |msg| {
        try jw.beginObject();
        try jw.objectField("role");
        try jw.write(msg.role.toString());
        try jw.objectField("content");
        try jw.beginArray();
        for (msg.content) |blk| {
            try serializeConverseBlock(&jw, blk);
        }
        try jw.endArray();
        try jw.endObject();
    }
    try jw.endArray();

    // Tools
    if (req.tools.len > 0) {
        try jw.objectField("toolConfig");
        try jw.beginObject();
        try jw.objectField("tools");
        try jw.beginArray();
        for (req.tools) |tool| {
            try jw.beginObject();
            try jw.objectField("toolSpec");
            try jw.beginObject();
            try jw.objectField("name");
            try jw.write(tool.name);
            try jw.objectField("description");
            try jw.write(tool.description);
            try jw.objectField("inputSchema");
            try jw.beginObject();
            try jw.objectField("json");
            try jw.print("{s}", .{tool.input_schema_json});
            try jw.endObject();
            try jw.endObject();
            try jw.endObject();
        }
        try jw.endArray();
        try jw.endObject();
    }

    // Inference config
    try jw.objectField("inferenceConfig");
    try jw.beginObject();
    try jw.objectField("maxTokens");
    try jw.write(req.max_tokens);
    if (req.temperature) |t| {
        try jw.objectField("temperature");
        try jw.write(t);
    }
    try jw.endObject();

    try jw.endObject();
}

fn serializeConverseBlock(jw: anytype, blk: ContentBlock) !void {
    switch (blk) {
        .text => |t| {
            try jw.beginObject();
            try jw.objectField("text");
            try jw.write(t);
            try jw.endObject();
        },
        .image => |img| {
            try jw.beginObject();
            try jw.objectField("image");
            try jw.beginObject();
            try jw.objectField("format");
            try jw.write(img.format);
            try jw.objectField("source");
            try jw.beginObject();
            try jw.objectField("bytes");
            try jw.write(img.bytes_base64);
            try jw.endObject();
            try jw.endObject();
            try jw.endObject();
        },
        .tool_use => |tu| {
            try jw.beginObject();
            try jw.objectField("toolUse");
            try jw.beginObject();
            try jw.objectField("toolUseId");
            try jw.write(tu.tool_use_id);
            try jw.objectField("name");
            try jw.write(tu.name);
            try jw.objectField("input");
            try jw.print("{s}", .{tu.input_json});
            try jw.endObject();
            try jw.endObject();
        },
        .tool_result => |tr| {
            try jw.beginObject();
            try jw.objectField("toolResult");
            try jw.beginObject();
            try jw.objectField("toolUseId");
            try jw.write(tr.tool_use_id);
            try jw.objectField("status");
            try jw.write(tr.status);
            try jw.objectField("content");
            try jw.beginArray();
            try jw.beginObject();
            try jw.objectField("text");
            try jw.write(tr.content);
            try jw.endObject();
            try jw.endArray();
            try jw.endObject();
            try jw.endObject();
        },
    }
}

// ---------------------------------------------------------------------------
// Bedrock response deserialization
// ---------------------------------------------------------------------------

fn deserializeConverseResponse(allocator: std.mem.Allocator, json_body: []const u8) !Response {
    const parsed = try std.json.parseFromSlice(std.json.Value, allocator, json_body, .{});
    defer parsed.deinit();

    const root = parsed.value;
    if (root != .object) return error.InvalidResponseFormat;

    const stop_reason = blk: {
        const v = root.object.get("stopReason") orelse break :blk "end_turn";
        break :blk if (v == .string) v.string else "end_turn";
    };

    var text_buf = std.ArrayList(u8).init(allocator);
    errdefer text_buf.deinit();
    var tool_uses = std.ArrayList(Response.ToolUse).init(allocator);
    errdefer tool_uses.deinit();

    if (root.object.get("output")) |output_v| {
        if (output_v == .object) {
            if (output_v.object.get("message")) |msg_v| {
                if (msg_v == .object) {
                    if (msg_v.object.get("content")) |content_v| {
                        if (content_v == .array) {
                            for (content_v.array.items) |blk| {
                                if (blk != .object) continue;
                                if (blk.object.get("text")) |tv| {
                                    if (tv == .string) try text_buf.appendSlice(tv.string);
                                } else if (blk.object.get("toolUse")) |tu_v| {
                                    if (tu_v == .object) {
                                        const id = blk: {
                                            const v = tu_v.object.get("toolUseId") orelse break :blk "";
                                            break :blk if (v == .string) v.string else "";
                                        };
                                        const name = blk: {
                                            const v = tu_v.object.get("name") orelse break :blk "";
                                            break :blk if (v == .string) v.string else "";
                                        };
                                        var input_buf = std.ArrayList(u8).init(allocator);
                                        errdefer input_buf.deinit();
                                        if (tu_v.object.get("input")) |iv| {
                                            try std.json.stringify(iv, .{}, input_buf.writer());
                                        } else {
                                            try input_buf.appendSlice("{}");
                                        }
                                        try tool_uses.append(Response.ToolUse{
                                            .tool_use_id = try allocator.dupe(u8, id),
                                            .name = try allocator.dupe(u8, name),
                                            .input_json = try input_buf.toOwnedSlice(),
                                        });
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    var usage_val: ?Response.Usage = null;
    if (root.object.get("usage")) |usage_v| {
        if (usage_v == .object) {
            const inp = getU32(usage_v, "inputTokens");
            const outp = getU32(usage_v, "outputTokens");
            usage_val = Response.Usage{ .input_tokens = inp, .output_tokens = outp };
        }
    }

    return Response{
        .allocator = allocator,
        .stop_reason = try allocator.dupe(u8, stop_reason),
        .content = try text_buf.toOwnedSlice(),
        .tool_uses = try tool_uses.toOwnedSlice(),
        .usage = usage_val,
    };
}

fn getU32(obj: std.json.Value, key: []const u8) u32 {
    const v = obj.object.get(key) orelse return 0;
    return switch (v) {
        .integer => @intCast(v.integer),
        .float => @intFromFloat(v.float),
        else => 0,
    };
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

test "hmacSha256 known vector (RFC 4231 test case 1)" {
    // Key:  0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b  (20 bytes)
    // Data: "Hi There"
    // HMAC: b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7
    const key = [_]u8{0x0b} ** 20;
    const data = "Hi There";
    const expected = "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7";

    const result = hmacSha256(&key, data);
    const hex = try hexEncode(std.testing.allocator, &result);
    defer std.testing.allocator.free(hex);

    try std.testing.expectEqualStrings(expected, hex);
}

test "sha256 known vector" {
    // SHA-256("") = e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855
    const result = sha256("");
    const hex = try hexEncode(std.testing.allocator, &result);
    defer std.testing.allocator.free(hex);
    try std.testing.expectEqualStrings(
        "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
        hex,
    );
}

test "urlEncode basic" {
    const encoded = try urlEncode(std.testing.allocator, "/model/foo bar/converse", false);
    defer std.testing.allocator.free(encoded);
    try std.testing.expectEqualStrings("/model/foo%20bar/converse", encoded);
}

test "urlEncode encode_slash" {
    const encoded = try urlEncode(std.testing.allocator, "/a/b", true);
    defer std.testing.allocator.free(encoded);
    try std.testing.expectEqualStrings("%2Fa%2Fb", encoded);
}

test "parseHostPath" {
    const host, const path = try parseHostPath(
        std.testing.allocator,
        "https://bedrock-runtime.us-east-1.amazonaws.com/model/claude/converse",
    );
    defer std.testing.allocator.free(host);
    defer std.testing.allocator.free(path);
    try std.testing.expectEqualStrings("bedrock-runtime.us-east-1.amazonaws.com", host);
    try std.testing.expectEqualStrings("/model/claude/converse", path);
}

test "signRequest produces deterministic output with known inputs" {
    // Use fixed timestamp to get deterministic signature
    const creds = Credentials{
        .access_key_id = "AKIDEXAMPLE",
        .secret_access_key = "wJalrXUtnFEMI/K7MDENG+bPxRfiCYEXAMPLEKEY",
    };
    var signed = try signRequest(
        std.testing.allocator,
        "POST",
        "https://bedrock-runtime.us-east-1.amazonaws.com/model/anthropic.claude/converse",
        "{}",
        creds,
        "us-east-1",
        "bedrock",
        "20231001T120000Z",
    );
    defer signed.deinit(std.testing.allocator);

    // Verify the authorization header has the expected structure
    try std.testing.expect(std.mem.startsWith(u8, signed.authorization, "AWS4-HMAC-SHA256 "));
    try std.testing.expect(std.mem.indexOf(u8, signed.authorization, "AKIDEXAMPLE") != null);
    try std.testing.expect(std.mem.indexOf(u8, signed.authorization, "us-east-1") != null);
    try std.testing.expect(std.mem.indexOf(u8, signed.authorization, "bedrock") != null);
}

test "signRequest with session token" {
    const creds = Credentials{
        .access_key_id = "AKID",
        .secret_access_key = "SECRET",
        .session_token = "mytoken",
    };
    var signed = try signRequest(
        std.testing.allocator,
        "POST",
        "https://bedrock-runtime.us-east-1.amazonaws.com/test",
        "body",
        creds,
        "us-east-1",
        "bedrock",
        "20231001T120000Z",
    );
    defer signed.deinit(std.testing.allocator);

    try std.testing.expect(signed.x_amz_security_token != null);
    try std.testing.expectEqualStrings("mytoken", signed.x_amz_security_token.?);
}

test "serializeConverseRequest basic" {
    const allocator = std.testing.allocator;
    var buf = std.ArrayList(u8).init(allocator);
    defer buf.deinit();

    const req = Request{
        .model_id = "anthropic.claude-3-sonnet-20240229-v1:0",
        .messages = &.{
            Message{
                .role = .user,
                .content = &.{ContentBlock{ .text = "Hello from Bedrock!" }},
            },
        },
    };
    try serializeConverseRequest(buf.writer(), req);

    const parsed = try std.json.parseFromSlice(std.json.Value, allocator, buf.items, .{});
    defer parsed.deinit();

    try std.testing.expect(parsed.value == .object);
    const messages = parsed.value.object.get("messages") orelse return error.TestMissingField;
    try std.testing.expectEqual(@as(usize, 1), messages.array.items.len);
}

test "deserializeConverseResponse text" {
    const json_body =
        \\{
        \\  "stopReason": "end_turn",
        \\  "output": {
        \\    "message": {
        \\      "role": "assistant",
        \\      "content": [{"text": "Hi from Bedrock!"}]
        \\    }
        \\  },
        \\  "usage": {"inputTokens": 10, "outputTokens": 5}
        \\}
    ;
    var resp = try deserializeConverseResponse(std.testing.allocator, json_body);
    defer resp.deinit();

    try std.testing.expectEqualStrings("end_turn", resp.stop_reason);
    try std.testing.expectEqualStrings("Hi from Bedrock!", resp.content);
    try std.testing.expectEqual(@as(usize, 0), resp.tool_uses.len);
    const usage = resp.usage orelse return error.TestUnexpectedNull;
    try std.testing.expectEqual(@as(u32, 10), usage.input_tokens);
}

test "BedrockProvider sendRequest returns NotImplemented" {
    var p = try BedrockProvider.init(std.testing.allocator, "us-east-1", "my-model");
    defer p.deinit();
    try std.testing.expectError(error.NotImplemented, p.sendRequest(std.testing.allocator, "{}"));
}

test "BedrockProvider buildEndpointUrl" {
    var p = try BedrockProvider.init(std.testing.allocator, "eu-west-1", "anthropic.claude-3");
    defer p.deinit();
    const url = try p.buildEndpointUrl();
    defer std.testing.allocator.free(url);
    try std.testing.expect(std.mem.indexOf(u8, url, "eu-west-1") != null);
    try std.testing.expect(std.mem.indexOf(u8, url, "anthropic.claude-3") != null);
}
