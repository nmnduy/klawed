//! providers/kimi.zig — Kimi Coding Plan provider with OAuth 2.0 device flow
//!
//! Zig port of src/kimi_oauth.c and src/kimi_coding_plan_provider.c.
//!
//! Kimi uses OAuth 2.0 Device Authorization Grant (RFC 8628) for
//! authentication and an OpenAI-compatible API with reasoning_content
//! preservation (same as Moonshot).
//!
//! ## Token lifecycle
//!   1. Check disk cache (~/.kimi/credentials/)
//!   2. If expired / missing, initiate device authorization flow
//!   3. Poll token endpoint until user authenticates
//!   4. Persist token to disk for future runs
//!   5. Background refresh 5 minutes before expiry
//!
//! ## Phase 5 note
//! HTTP calls (device auth, token polling, API requests) are stubbed and
//! return `error.NotImplemented`. The refresh thread is also stubbed.

const std = @import("std");
const openai = @import("openai.zig");

// ---------------------------------------------------------------------------
// OAuth constants
// ---------------------------------------------------------------------------

pub const oauth_client_id = "17e5f671-d194-4dfb-9706-5516cb48c098";
pub const kimi_version = "1.8.0";
pub const oauth_host = "https://auth.kimi.com";
pub const api_base = "https://api.kimi.com/coding/v1";
pub const default_model = "kimi-for-coding";

pub const device_auth_endpoint = oauth_host ++ "/api/oauth/device_authorization";
pub const token_endpoint = oauth_host ++ "/api/oauth/token";

/// Seconds before expiry at which to refresh the token.
pub const token_refresh_threshold_secs: i64 = 300; // 5 minutes

// ---------------------------------------------------------------------------
// OAuth token
// ---------------------------------------------------------------------------

pub const OAuthToken = struct {
    allocator: std.mem.Allocator,
    access_token: []const u8,
    refresh_token: []const u8,
    /// Unix timestamp when this token expires.
    expires_at: i64,
    token_type: []const u8,
    scope: []const u8,

    pub fn deinit(self: *OAuthToken) void {
        const a = self.allocator;
        a.free(self.access_token);
        a.free(self.refresh_token);
        a.free(self.token_type);
        a.free(self.scope);
    }

    /// Returns true when the token needs refreshing.
    pub fn needsRefresh(self: *const OAuthToken) bool {
        const now = std.time.timestamp();
        return now >= self.expires_at - token_refresh_threshold_secs;
    }

    /// Parse an OAuthToken from a JSON response body.
    pub fn fromJson(allocator: std.mem.Allocator, json_body: []const u8) !OAuthToken {
        const parsed = try std.json.parseFromSlice(std.json.Value, allocator, json_body, .{});
        defer parsed.deinit();

        if (parsed.value != .object) return error.InvalidTokenResponse;
        const root = parsed.value;

        const access_token = blk: {
            const v = root.object.get("access_token") orelse return error.MissingAccessToken;
            break :blk if (v == .string) v.string else return error.MissingAccessToken;
        };
        const refresh_token = blk: {
            const v = root.object.get("refresh_token") orelse break :blk "";
            break :blk if (v == .string) v.string else "";
        };
        const expires_in: i64 = blk: {
            const v = root.object.get("expires_in") orelse break :blk 3600;
            break :blk switch (v) {
                .integer => v.integer,
                .float => @intFromFloat(v.float),
                else => 3600,
            };
        };
        const token_type = blk: {
            const v = root.object.get("token_type") orelse break :blk "Bearer";
            break :blk if (v == .string) v.string else "Bearer";
        };
        const scope = blk: {
            const v = root.object.get("scope") orelse break :blk "";
            break :blk if (v == .string) v.string else "";
        };

        return OAuthToken{
            .allocator = allocator,
            .access_token = try allocator.dupe(u8, access_token),
            .refresh_token = try allocator.dupe(u8, refresh_token),
            .expires_at = std.time.timestamp() + expires_in,
            .token_type = try allocator.dupe(u8, token_type),
            .scope = try allocator.dupe(u8, scope),
        };
    }

    /// Serialize this token to JSON for disk persistence.
    pub fn toJson(self: *const OAuthToken, writer: anytype) !void {
        var jw = std.json.writeStream(writer, .{ .whitespace = .indent_2 });
        try jw.beginObject();
        try jw.objectField("access_token");
        try jw.write(self.access_token);
        try jw.objectField("refresh_token");
        try jw.write(self.refresh_token);
        try jw.objectField("expires_at");
        try jw.write(self.expires_at);
        try jw.objectField("token_type");
        try jw.write(self.token_type);
        try jw.objectField("scope");
        try jw.write(self.scope);
        try jw.endObject();
    }

    /// Load from a persisted JSON file.
    pub fn fromDiskFile(allocator: std.mem.Allocator, path: []const u8) !OAuthToken {
        const file = try std.fs.openFileAbsolute(path, .{});
        defer file.close();
        const contents = try file.readToEndAlloc(allocator, 64 * 1024);
        defer allocator.free(contents);
        const parsed = try std.json.parseFromSlice(std.json.Value, allocator, contents, .{});
        defer parsed.deinit();

        if (parsed.value != .object) return error.InvalidTokenFile;
        const root = parsed.value;

        const access_token = blk: {
            const v = root.object.get("access_token") orelse return error.MissingAccessToken;
            break :blk if (v == .string) v.string else return error.MissingAccessToken;
        };
        const refresh_token = blk: {
            const v = root.object.get("refresh_token") orelse break :blk "";
            break :blk if (v == .string) v.string else "";
        };
        const expires_at: i64 = blk: {
            const v = root.object.get("expires_at") orelse break :blk 0;
            break :blk switch (v) {
                .integer => v.integer,
                .float => @intFromFloat(v.float),
                else => 0,
            };
        };
        const token_type = blk: {
            const v = root.object.get("token_type") orelse break :blk "Bearer";
            break :blk if (v == .string) v.string else "Bearer";
        };
        const scope = blk: {
            const v = root.object.get("scope") orelse break :blk "";
            break :blk if (v == .string) v.string else "";
        };

        return OAuthToken{
            .allocator = allocator,
            .access_token = try allocator.dupe(u8, access_token),
            .refresh_token = try allocator.dupe(u8, refresh_token),
            .expires_at = expires_at,
            .token_type = try allocator.dupe(u8, token_type),
            .scope = try allocator.dupe(u8, scope),
        };
    }
};

// ---------------------------------------------------------------------------
// Device authorization
// ---------------------------------------------------------------------------

pub const DeviceAuth = struct {
    allocator: std.mem.Allocator,
    user_code: []const u8,
    device_code: []const u8,
    verification_uri: []const u8,
    verification_uri_complete: []const u8,
    expires_in: i64,
    interval: i64,

    pub fn deinit(self: *DeviceAuth) void {
        const a = self.allocator;
        a.free(self.user_code);
        a.free(self.device_code);
        a.free(self.verification_uri);
        a.free(self.verification_uri_complete);
    }

    /// Parse device auth response JSON.
    pub fn fromJson(allocator: std.mem.Allocator, json_body: []const u8) !DeviceAuth {
        const parsed = try std.json.parseFromSlice(std.json.Value, allocator, json_body, .{});
        defer parsed.deinit();

        if (parsed.value != .object) return error.InvalidDeviceAuthResponse;
        const root = parsed.value;

        const user_code = blk: {
            const v = root.object.get("user_code") orelse return error.MissingUserCode;
            break :blk if (v == .string) v.string else return error.MissingUserCode;
        };
        const device_code = blk: {
            const v = root.object.get("device_code") orelse return error.MissingDeviceCode;
            break :blk if (v == .string) v.string else return error.MissingDeviceCode;
        };
        const verification_uri = blk: {
            const v = root.object.get("verification_uri") orelse break :blk "";
            break :blk if (v == .string) v.string else "";
        };
        const verification_uri_complete = blk: {
            const v = root.object.get("verification_uri_complete") orelse break :blk verification_uri;
            break :blk if (v == .string) v.string else verification_uri;
        };
        const expires_in: i64 = blk: {
            const v = root.object.get("expires_in") orelse break :blk 900;
            break :blk switch (v) {
                .integer => v.integer,
                .float => @intFromFloat(v.float),
                else => 900,
            };
        };
        const interval: i64 = blk: {
            const v = root.object.get("interval") orelse break :blk 5;
            break :blk switch (v) {
                .integer => v.integer,
                .float => @intFromFloat(v.float),
                else => 5,
            };
        };

        return DeviceAuth{
            .allocator = allocator,
            .user_code = try allocator.dupe(u8, user_code),
            .device_code = try allocator.dupe(u8, device_code),
            .verification_uri = try allocator.dupe(u8, verification_uri),
            .verification_uri_complete = try allocator.dupe(u8, verification_uri_complete),
            .expires_in = expires_in,
            .interval = interval,
        };
    }
};

// ---------------------------------------------------------------------------
// KimiProvider
// ---------------------------------------------------------------------------

// Re-export OpenAI types used for request/response
pub const Request = openai.Request;
pub const Response = openai.Response;
pub const Message = openai.Message;
pub const ContentBlock = openai.ContentBlock;
pub const ToolDefinition = openai.ToolDefinition;
pub const Role = openai.Role;

pub const KimiProvider = struct {
    allocator: std.mem.Allocator,
    model: []const u8,
    /// Current OAuth token (null = not yet authenticated).
    token: ?OAuthToken,
    /// Mutex protecting `token`.
    token_mutex: std.Thread.Mutex,
    /// Inner OpenAI-compatible provider.
    inner: openai.OpenAIProvider,

    pub fn init(allocator: std.mem.Allocator, model: []const u8) !KimiProvider {
        const model_str = if (model.len > 0) model else default_model;
        const api_endpoint = api_base ++ "/chat/completions";
        return KimiProvider{
            .allocator = allocator,
            .model = try allocator.dupe(u8, model_str),
            .token = null,
            .token_mutex = .{},
            .inner = try openai.OpenAIProvider.init(
                allocator,
                "", // api_key is set dynamically from OAuth token
                api_endpoint,
                .preserve, // Kimi: preserve reasoning_content
            ),
        };
    }

    pub fn deinit(self: *KimiProvider) void {
        self.allocator.free(self.model);
        if (self.token) |*t| t.deinit();
        self.inner.deinit();
    }

    /// Check if a valid non-expired token is available.
    pub fn hasValidToken(self: *KimiProvider) bool {
        self.token_mutex.lock();
        defer self.token_mutex.unlock();
        const t = self.token orelse return false;
        return !t.needsRefresh();
    }

    /// Request device authorization (HTTP stub — Phase 5).
    pub fn requestDeviceAuth(
        self: *KimiProvider,
        allocator: std.mem.Allocator,
    ) !DeviceAuth {
        _ = self;
        _ = allocator;
        return error.NotImplemented;
    }

    /// Poll for token after device authorization (HTTP stub — Phase 5).
    pub fn pollForToken(
        self: *KimiProvider,
        allocator: std.mem.Allocator,
        device_code: []const u8,
        interval_secs: i64,
    ) !OAuthToken {
        _ = self;
        _ = allocator;
        _ = device_code;
        _ = interval_secs;
        return error.NotImplemented;
    }

    /// Refresh the access token (HTTP stub — Phase 5).
    pub fn refreshToken(
        self: *KimiProvider,
        allocator: std.mem.Allocator,
    ) !OAuthToken {
        _ = self;
        _ = allocator;
        return error.NotImplemented;
    }

    /// Build the Kimi API request body (delegates to OpenAI provider).
    pub fn buildRequestBody(self: *const KimiProvider, req: Request) ![]u8 {
        return self.inner.buildRequestBody(req);
    }

    /// HTTP stub — Phase 5 will implement this.
    pub fn sendRequest(
        self: *KimiProvider,
        allocator: std.mem.Allocator,
        body: []const u8,
    ) ![]u8 {
        _ = self;
        _ = allocator;
        _ = body;
        return error.NotImplemented;
    }

    pub fn parseResponse(self: *const KimiProvider, json_body: []const u8) !Response {
        return self.inner.parseResponse(json_body);
    }
};

// ---------------------------------------------------------------------------
// Credential file path helpers
// ---------------------------------------------------------------------------

/// Returns the path to the Kimi credentials file: ~/.kimi/credentials/token.json
/// Caller must free.
pub fn credentialsPath(allocator: std.mem.Allocator) ![]u8 {
    const home = std.process.getEnvVarOwned(allocator, "HOME") catch return error.HomeNotSet;
    defer allocator.free(home);
    return std.fs.path.join(allocator, &.{ home, ".kimi", "credentials", "token.json" });
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

test "OAuthToken.fromJson basic" {
    const json =
        \\{
        \\  "access_token": "eyJ...",
        \\  "refresh_token": "rft...",
        \\  "expires_in": 3600,
        \\  "token_type": "Bearer",
        \\  "scope": "all"
        \\}
    ;
    var token = try OAuthToken.fromJson(std.testing.allocator, json);
    defer token.deinit();

    try std.testing.expectEqualStrings("eyJ...", token.access_token);
    try std.testing.expectEqualStrings("rft...", token.refresh_token);
    try std.testing.expectEqualStrings("Bearer", token.token_type);
    try std.testing.expectEqualStrings("all", token.scope);
    // expires_at should be roughly now + 3600
    const now = std.time.timestamp();
    try std.testing.expect(token.expires_at > now);
    try std.testing.expect(token.expires_at <= now + 3601);
}

test "OAuthToken.needsRefresh when expired" {
    var token = OAuthToken{
        .allocator = std.testing.allocator,
        .access_token = "",
        .refresh_token = "",
        .expires_at = std.time.timestamp() - 1, // already expired
        .token_type = "Bearer",
        .scope = "",
    };
    // Don't call deinit — strings are literals
    try std.testing.expect(token.needsRefresh());
}

test "OAuthToken.needsRefresh when fresh" {
    var token = OAuthToken{
        .allocator = std.testing.allocator,
        .access_token = "",
        .refresh_token = "",
        .expires_at = std.time.timestamp() + 3600, // valid for an hour
        .token_type = "Bearer",
        .scope = "",
    };
    try std.testing.expect(!token.needsRefresh());
}

test "OAuthToken toJson round-trip" {
    var token = try OAuthToken.fromJson(std.testing.allocator,
        \\{"access_token":"acc","refresh_token":"ref","expires_in":3600,"token_type":"Bearer","scope":"all"}
    );
    defer token.deinit();

    var buf = std.ArrayList(u8).init(std.testing.allocator);
    defer buf.deinit();
    try token.toJson(buf.writer());

    // Re-parse the serialized form
    const parsed = try std.json.parseFromSlice(std.json.Value, std.testing.allocator, buf.items, .{});
    defer parsed.deinit();
    try std.testing.expectEqualStrings("acc", parsed.value.object.get("access_token").?.string);
    try std.testing.expectEqualStrings("ref", parsed.value.object.get("refresh_token").?.string);
}

test "DeviceAuth.fromJson" {
    const json =
        \\{
        \\  "user_code": "ABCD-1234",
        \\  "device_code": "device_xyz",
        \\  "verification_uri": "https://auth.kimi.com/activate",
        \\  "verification_uri_complete": "https://auth.kimi.com/activate?user_code=ABCD-1234",
        \\  "expires_in": 900,
        \\  "interval": 5
        \\}
    ;
    var da = try DeviceAuth.fromJson(std.testing.allocator, json);
    defer da.deinit();

    try std.testing.expectEqualStrings("ABCD-1234", da.user_code);
    try std.testing.expectEqualStrings("device_xyz", da.device_code);
    try std.testing.expectEqual(@as(i64, 900), da.expires_in);
    try std.testing.expectEqual(@as(i64, 5), da.interval);
}

test "KimiProvider init and deinit" {
    var p = try KimiProvider.init(std.testing.allocator, "kimi-for-coding");
    defer p.deinit();

    try std.testing.expectEqualStrings("kimi-for-coding", p.model);
    try std.testing.expect(!p.hasValidToken());
}

test "KimiProvider uses preserve reasoning mode" {
    var p = try KimiProvider.init(std.testing.allocator, "");
    defer p.deinit();
    try std.testing.expectEqual(openai.ReasoningMode.preserve, p.inner.reasoning_mode);
}

test "KimiProvider sendRequest returns NotImplemented" {
    var p = try KimiProvider.init(std.testing.allocator, "");
    defer p.deinit();
    try std.testing.expectError(error.NotImplemented, p.sendRequest(std.testing.allocator, "{}"));
}

test "KimiProvider buildRequestBody" {
    var p = try KimiProvider.init(std.testing.allocator, "");
    defer p.deinit();

    const body = try p.buildRequestBody(Request{
        .model = default_model,
        .messages = &.{},
    });
    defer std.testing.allocator.free(body);
    try std.testing.expect(std.mem.indexOf(u8, body, default_model) != null);
}
