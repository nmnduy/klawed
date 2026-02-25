//! http_client.zig — HTTP client wrapping libcurl via @cImport
//!
//! Zig port of src/http_client.c / src/http_client.h.
//!
//! Keeps libcurl as a C library (via `@cImport`) and wraps the call sites with
//! Zig error unions so callers never touch raw curl error codes.
//!
//! ## Public surface
//! - `HttpClient` — owns a `CURL*` handle; call `init()`/`deinit()`
//! - `Request` — URL, method, headers, body, timeouts
//! - `Response` — status code + owned body bytes; call `deinit(allocator)`
//! - `streamRequest()` — SSE streaming variant; calls `StreamCallback` per line
//! - `globalInit()` / `globalCleanup()` — must wrap program lifetime

const std = @import("std");

const c = @cImport({
    @cInclude("curl/curl.h");
});

// ---------------------------------------------------------------------------
// Error set
// ---------------------------------------------------------------------------

pub const HttpError = error{
    /// `curl_global_init` failed.
    CurlGlobalInitFailed,
    /// `curl_easy_init` returned NULL.
    CurlInitFailed,
    /// A curl perform error occurred (non-abort).
    CurlError,
    /// The stream callback returned non-zero to abort.
    StreamAborted,
    /// Memory allocation failed.
    OutOfMemory,
};

// ---------------------------------------------------------------------------
// Types
// ---------------------------------------------------------------------------

pub const Method = enum {
    GET,
    POST,
    PUT,
    DELETE,
    PATCH,
};

pub const Header = struct {
    name: []const u8,
    value: []const u8,
};

pub const Request = struct {
    url: []const u8,
    method: Method = .POST,
    /// Slice of `Header` values; the client copies them into curl slist.
    headers: []const Header = &.{},
    /// Optional request body.
    body: ?[]const u8 = null,
    /// Connection timeout in milliseconds (0 = curl default 30 s).
    connect_timeout_ms: u32 = 30_000,
    /// Total transfer timeout in milliseconds (0 = curl default 5 min).
    total_timeout_ms: u32 = 300_000,
    /// Follow HTTP redirects.
    follow_redirects: bool = false,
    /// Enable curl verbose logging (useful for debugging).
    verbose: bool = false,
};

pub const Response = struct {
    status_code: u32,
    /// Owned bytes — caller must call `deinit(allocator)`.
    body: []u8,
    /// Request duration in milliseconds.
    duration_ms: i64,

    pub fn deinit(self: *Response, allocator: std.mem.Allocator) void {
        allocator.free(self.body);
    }
};

/// Callback type for SSE streaming.
/// `line` is a single SSE text line (not including the trailing newline).
/// Return `false` to continue streaming; `true` to abort.
pub const StreamCallback = *const fn (line: []const u8, userdata: ?*anyopaque) bool;

// ---------------------------------------------------------------------------
// Internal write-buffer context
// ---------------------------------------------------------------------------

const WriteCtx = struct {
    buf: std.ArrayList(u8),
};

fn writeCallback(
    data: [*]u8,
    size: usize,
    nmemb: usize,
    userdata: *anyopaque,
) callconv(.C) usize {
    const ctx = @as(*WriteCtx, @ptrCast(@alignCast(userdata)));
    const bytes = data[0 .. size * nmemb];
    ctx.buf.appendSlice(bytes) catch return 0;
    return size * nmemb;
}

// ---------------------------------------------------------------------------
// SSE streaming context
// ---------------------------------------------------------------------------

const StreamCtx = struct {
    callback: StreamCallback,
    userdata: ?*anyopaque,
    abort_requested: bool,
    /// Leftover bytes from a previous chunk that didn't end on a newline.
    partial: std.ArrayList(u8),
};

fn streamWriteCallback(
    data: [*]u8,
    size: usize,
    nmemb: usize,
    userdata: *anyopaque,
) callconv(.C) usize {
    const realsize = size * nmemb;
    const ctx = @as(*StreamCtx, @ptrCast(@alignCast(userdata)));

    if (ctx.abort_requested) return 0;

    const chunk = data[0..realsize];
    var pos: usize = 0;

    while (pos < chunk.len) {
        // Find next newline in the incoming chunk
        const eol = std.mem.indexOfScalar(u8, chunk[pos..], '\n');
        if (eol == null) {
            // No newline — buffer the rest for the next callback invocation
            ctx.partial.appendSlice(chunk[pos..]) catch {
                ctx.abort_requested = true;
                return 0;
            };
            break;
        }

        const nl_pos = pos + eol.?;
        // Assemble the line: partial + chunk up to (not including) newline
        ctx.partial.appendSlice(chunk[pos..nl_pos]) catch {
            ctx.abort_requested = true;
            return 0;
        };

        // Strip trailing \r if present
        var line = ctx.partial.items;
        if (line.len > 0 and line[line.len - 1] == '\r') {
            line = line[0 .. line.len - 1];
        }

        // Dispatch to callback
        if (ctx.callback(line, ctx.userdata)) {
            ctx.abort_requested = true;
            return 0;
        }

        ctx.partial.clearRetainingCapacity();
        pos = nl_pos + 1; // skip past \n
    }

    return realsize;
}

// ---------------------------------------------------------------------------
// Global init / cleanup
// ---------------------------------------------------------------------------

/// Must be called once before any HTTP requests (typically at program start).
pub fn globalInit() HttpError!void {
    const rc = c.curl_global_init(c.CURL_GLOBAL_DEFAULT);
    if (rc != c.CURLE_OK) return error.CurlGlobalInitFailed;
}

/// Must be called once at program exit.
pub fn globalCleanup() void {
    c.curl_global_cleanup();
}

// ---------------------------------------------------------------------------
// HttpClient
// ---------------------------------------------------------------------------

pub const HttpClient = struct {
    handle: *c.CURL,

    /// Create a new client.  The internal `CURL*` handle is reused across
    /// calls; each `request()` call resets options via `curl_easy_reset`.
    pub fn init() HttpError!HttpClient {
        const handle = c.curl_easy_init() orelse return error.CurlInitFailed;
        return HttpClient{ .handle = handle };
    }

    pub fn deinit(self: *HttpClient) void {
        c.curl_easy_cleanup(self.handle);
    }

    // ------------------------------------------------------------------
    // Non-streaming request
    // ------------------------------------------------------------------

    /// Perform a blocking HTTP request and return the full response body.
    /// The returned `Response.body` is owned by the caller (allocated with
    /// `allocator`); call `response.deinit(allocator)` to free it.
    pub fn request(
        self: *HttpClient,
        allocator: std.mem.Allocator,
        req: Request,
    ) (HttpError || std.mem.Allocator.Error)!Response {
        c.curl_easy_reset(self.handle);

        var write_ctx = WriteCtx{ .buf = std.ArrayList(u8).init(allocator) };
        defer write_ctx.buf.deinit();

        // Build null-terminated URL
        const url_z = try std.fmt.allocPrintZ(allocator, "{s}", .{req.url});
        defer allocator.free(url_z);

        _ = c.curl_easy_setopt(self.handle, c.CURLOPT_URL, url_z.ptr);

        // Method + body
        try setMethod(self.handle, req.method, req.body);

        // Headers
        var slist: ?*c.curl_slist = null;
        defer if (slist) |sl| c.curl_slist_free_all(sl);
        for (req.headers) |h| {
            const header_str = try std.fmt.allocPrintZ(allocator, "{s}: {s}", .{ h.name, h.value });
            defer allocator.free(header_str);
            slist = c.curl_slist_append(slist, header_str.ptr);
        }
        if (slist) |sl| _ = c.curl_easy_setopt(self.handle, c.CURLOPT_HTTPHEADER, sl);

        // Timeouts
        if (req.connect_timeout_ms > 0) {
            _ = c.curl_easy_setopt(self.handle, c.CURLOPT_CONNECTTIMEOUT_MS, @as(c_long, req.connect_timeout_ms));
        }
        if (req.total_timeout_ms > 0) {
            _ = c.curl_easy_setopt(self.handle, c.CURLOPT_TIMEOUT_MS, @as(c_long, req.total_timeout_ms));
        }

        _ = c.curl_easy_setopt(self.handle, c.CURLOPT_FOLLOWLOCATION, @as(c_long, if (req.follow_redirects) 1 else 0));
        _ = c.curl_easy_setopt(self.handle, c.CURLOPT_VERBOSE, @as(c_long, if (req.verbose) 1 else 0));
        _ = c.curl_easy_setopt(self.handle, c.CURLOPT_NOPROGRESS, @as(c_long, 1));

        // Write callback
        _ = c.curl_easy_setopt(self.handle, c.CURLOPT_WRITEFUNCTION, writeCallback);
        _ = c.curl_easy_setopt(self.handle, c.CURLOPT_WRITEDATA, &write_ctx);

        const start = std.time.milliTimestamp();
        const rc = c.curl_easy_perform(self.handle);
        const duration_ms = std.time.milliTimestamp() - start;

        if (rc != c.CURLE_OK) {
            if (rc == c.CURLE_ABORTED_BY_CALLBACK) return error.StreamAborted;
            return error.CurlError;
        }

        var status: c_long = 0;
        _ = c.curl_easy_getinfo(self.handle, c.CURLINFO_RESPONSE_CODE, &status);

        return Response{
            .status_code = @intCast(status),
            .body = try write_ctx.buf.toOwnedSlice(),
            .duration_ms = duration_ms,
        };
    }

    // ------------------------------------------------------------------
    // Streaming request (SSE)
    // ------------------------------------------------------------------

    /// Perform a streaming HTTP request.  `callback` is called for each line
    /// received, including empty lines (SSE event separators).
    /// Returns the HTTP status code; body is consumed by the callback.
    pub fn streamRequest(
        self: *HttpClient,
        allocator: std.mem.Allocator,
        req: Request,
        callback: StreamCallback,
        userdata: ?*anyopaque,
    ) (HttpError || std.mem.Allocator.Error)!u32 {
        c.curl_easy_reset(self.handle);

        var stream_ctx = StreamCtx{
            .callback = callback,
            .userdata = userdata,
            .abort_requested = false,
            .partial = std.ArrayList(u8).init(allocator),
        };
        defer stream_ctx.partial.deinit();

        const url_z = try std.fmt.allocPrintZ(allocator, "{s}", .{req.url});
        defer allocator.free(url_z);

        _ = c.curl_easy_setopt(self.handle, c.CURLOPT_URL, url_z.ptr);

        try setMethod(self.handle, req.method, req.body);

        var slist: ?*c.curl_slist = null;
        defer if (slist) |sl| c.curl_slist_free_all(sl);
        for (req.headers) |h| {
            const header_str = try std.fmt.allocPrintZ(allocator, "{s}: {s}", .{ h.name, h.value });
            defer allocator.free(header_str);
            slist = c.curl_slist_append(slist, header_str.ptr);
        }
        if (slist) |sl| _ = c.curl_easy_setopt(self.handle, c.CURLOPT_HTTPHEADER, sl);

        if (req.connect_timeout_ms > 0) {
            _ = c.curl_easy_setopt(self.handle, c.CURLOPT_CONNECTTIMEOUT_MS, @as(c_long, req.connect_timeout_ms));
        }
        if (req.total_timeout_ms > 0) {
            _ = c.curl_easy_setopt(self.handle, c.CURLOPT_TIMEOUT_MS, @as(c_long, req.total_timeout_ms));
        }

        _ = c.curl_easy_setopt(self.handle, c.CURLOPT_FOLLOWLOCATION, @as(c_long, if (req.follow_redirects) 1 else 0));
        _ = c.curl_easy_setopt(self.handle, c.CURLOPT_VERBOSE, @as(c_long, if (req.verbose) 1 else 0));
        _ = c.curl_easy_setopt(self.handle, c.CURLOPT_NOPROGRESS, @as(c_long, 1));

        _ = c.curl_easy_setopt(self.handle, c.CURLOPT_WRITEFUNCTION, streamWriteCallback);
        _ = c.curl_easy_setopt(self.handle, c.CURLOPT_WRITEDATA, &stream_ctx);

        const rc = c.curl_easy_perform(self.handle);

        if (stream_ctx.abort_requested) return error.StreamAborted;
        if (rc != c.CURLE_OK) {
            if (rc == c.CURLE_ABORTED_BY_CALLBACK) return error.StreamAborted;
            return error.CurlError;
        }

        var status: c_long = 0;
        _ = c.curl_easy_getinfo(self.handle, c.CURLINFO_RESPONSE_CODE, &status);
        return @intCast(status);
    }
};

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

fn setMethod(handle: *c.CURL, method: Method, body: ?[]const u8) HttpError!void {
    switch (method) {
        .GET => {
            _ = c.curl_easy_setopt(handle, c.CURLOPT_HTTPGET, @as(c_long, 1));
        },
        .POST => {
            _ = c.curl_easy_setopt(handle, c.CURLOPT_POST, @as(c_long, 1));
            if (body) |b| {
                _ = c.curl_easy_setopt(handle, c.CURLOPT_POSTFIELDS, b.ptr);
                _ = c.curl_easy_setopt(handle, c.CURLOPT_POSTFIELDSIZE, @as(c_long, @intCast(b.len)));
            } else {
                _ = c.curl_easy_setopt(handle, c.CURLOPT_POSTFIELDSIZE, @as(c_long, 0));
            }
        },
        .PUT => {
            _ = c.curl_easy_setopt(handle, c.CURLOPT_CUSTOMREQUEST, "PUT");
            if (body) |b| {
                _ = c.curl_easy_setopt(handle, c.CURLOPT_POSTFIELDS, b.ptr);
                _ = c.curl_easy_setopt(handle, c.CURLOPT_POSTFIELDSIZE, @as(c_long, @intCast(b.len)));
            }
        },
        .DELETE => {
            _ = c.curl_easy_setopt(handle, c.CURLOPT_CUSTOMREQUEST, "DELETE");
        },
        .PATCH => {
            _ = c.curl_easy_setopt(handle, c.CURLOPT_CUSTOMREQUEST, "PATCH");
            if (body) |b| {
                _ = c.curl_easy_setopt(handle, c.CURLOPT_POSTFIELDS, b.ptr);
                _ = c.curl_easy_setopt(handle, c.CURLOPT_POSTFIELDSIZE, @as(c_long, @intCast(b.len)));
            }
        },
    }
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

test "HttpClient init and deinit" {
    // Requires libcurl available at link time; just test the construction path.
    // If curl is not present this will fail at link time, not at test time.
    var client = try HttpClient.init();
    defer client.deinit();
    // If we got here curl is available.
    try std.testing.expect(true);
}

test "Request default values" {
    const req = Request{ .url = "https://example.com" };
    try std.testing.expect(req.method == .POST);
    try std.testing.expectEqual(@as(u32, 30_000), req.connect_timeout_ms);
    try std.testing.expectEqual(@as(u32, 300_000), req.total_timeout_ms);
    try std.testing.expect(!req.follow_redirects);
    try std.testing.expect(!req.verbose);
    try std.testing.expect(req.body == null);
    try std.testing.expectEqual(@as(usize, 0), req.headers.len);
}

test "Header struct" {
    const h = Header{ .name = "Authorization", .value = "Bearer token123" };
    try std.testing.expectEqualStrings("Authorization", h.name);
    try std.testing.expectEqualStrings("Bearer token123", h.value);
}

test "Method enum values" {
    const methods = [_]Method{ .GET, .POST, .PUT, .DELETE, .PATCH };
    try std.testing.expectEqual(@as(usize, 5), methods.len);
}
