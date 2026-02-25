//! tests/test_bedrock_auth.zig — Zig port of tests/test_bedrock_auth.c
//!
//! Tests AWS Bedrock credential loading and AWS SigV4 signing:
//! - loadCredentials reads from environment variables
//! - freeCredentials safely releases memory
//! - signRequest produces correctly-formatted Authorization headers
//! - BedrockProvider init / endpoint URL construction
//! - Region defaults and environment variable precedence

const std = @import("std");
const bedrock = @import("../providers/bedrock.zig");

// ---------------------------------------------------------------------------
// Credential loading from environment
// ---------------------------------------------------------------------------

test "bedrock auth: loadCredentials returns null when AWS_ACCESS_KEY_ID not set" {
    // Make sure the env var is absent — save original value.
    const alloc = std.testing.allocator;

    // We can't unsetenv in Zig stdlib portably, but we can skip if set.
    const key = std.process.getEnvVarOwned(alloc, "AWS_ACCESS_KEY_ID") catch null;
    defer if (key) |k| alloc.free(k);

    if (key != null) return error.SkipZigTest; // env var is set; skip

    const creds = try bedrock.loadCredentials(alloc);
    try std.testing.expect(creds == null);
}

test "bedrock auth: loadCredentials returns credentials when env vars set" {
    const alloc = std.testing.allocator;

    // Check whether AWS creds are available in env.
    const key_id = std.process.getEnvVarOwned(alloc, "AWS_ACCESS_KEY_ID") catch {
        return error.SkipZigTest; // creds not set — skip
    };
    defer alloc.free(key_id);
    const secret = std.process.getEnvVarOwned(alloc, "AWS_SECRET_ACCESS_KEY") catch {
        return error.SkipZigTest;
    };
    defer alloc.free(secret);

    const creds_opt = try bedrock.loadCredentials(alloc);
    try std.testing.expect(creds_opt != null);
    var creds = creds_opt.?;
    defer bedrock.freeCredentials(alloc, &creds);

    try std.testing.expect(creds.access_key_id.len > 0);
    try std.testing.expect(creds.secret_access_key.len > 0);
}

// ---------------------------------------------------------------------------
// SigV4 helpers — deterministic tests
// ---------------------------------------------------------------------------

test "bedrock auth: hmacSha256 produces 32-byte output" {
    const mac = bedrock.hmacSha256("secret", "data");
    try std.testing.expectEqual(@as(usize, 32), mac.len);
}

test "bedrock auth: sha256 produces 32-byte output" {
    const hash = bedrock.sha256("hello world");
    try std.testing.expectEqual(@as(usize, 32), hash.len);
}

test "bedrock auth: sha256 known vector (empty string)" {
    // SHA-256("") = e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855
    const hash = bedrock.sha256("");
    const alloc = std.testing.allocator;
    const hex = try bedrock.hexEncode(alloc, &hash);
    defer alloc.free(hex);
    try std.testing.expectEqualStrings(
        "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
        hex,
    );
}

test "bedrock auth: hexEncode produces lowercase hex" {
    const alloc = std.testing.allocator;
    const bytes = [_]u8{ 0xDE, 0xAD, 0xBE, 0xEF };
    const hex = try bedrock.hexEncode(alloc, &bytes);
    defer alloc.free(hex);
    try std.testing.expectEqualStrings("deadbeef", hex);
}

test "bedrock auth: urlEncode encodes special characters" {
    const alloc = std.testing.allocator;
    const enc = try bedrock.urlEncode(alloc, "hello world/path", false);
    defer alloc.free(enc);
    // Space → %20, slash not encoded when encode_slash=false
    try std.testing.expect(std.mem.indexOf(u8, enc, "%20") != null);
    try std.testing.expect(std.mem.indexOf(u8, enc, "/") != null);
}

test "bedrock auth: urlEncode encodes slash when encode_slash=true" {
    const alloc = std.testing.allocator;
    const enc = try bedrock.urlEncode(alloc, "/path/to", true);
    defer alloc.free(enc);
    try std.testing.expect(std.mem.indexOf(u8, enc, "%2F") != null);
    // Original slashes should be gone
    try std.testing.expect(std.mem.indexOf(u8, enc, "/") == null);
}

test "bedrock auth: dateStamp returns first 8 chars" {
    const ts = "20230601T120000Z";
    const ds = bedrock.dateStamp(ts);
    try std.testing.expectEqualStrings("20230601", ds);
}

// ---------------------------------------------------------------------------
// signRequest — deterministic test with known inputs
// ---------------------------------------------------------------------------

test "bedrock auth: signRequest produces valid Authorization header" {
    const alloc = std.testing.allocator;

    const creds = bedrock.Credentials{
        .access_key_id = "AKIDEXAMPLE",
        .secret_access_key = "wJalrXUtnFEMI/K7MDENG+bPxRfiCYEXAMPLEKEY",
        .session_token = null,
    };

    var signed = try bedrock.signRequest(
        alloc,
        "POST",
        "https://bedrock-runtime.us-east-1.amazonaws.com/model/anthropic.claude-3-sonnet/converse",
        "{\"messages\":[]}",
        creds,
        "us-east-1",
        "bedrock",
        "20231201T120000Z", // deterministic timestamp
    );
    defer signed.deinit(alloc);

    // Authorization header must start with the right algorithm prefix.
    try std.testing.expect(std.mem.startsWith(u8, signed.authorization, "AWS4-HMAC-SHA256 "));
    // Must contain the credential scope.
    try std.testing.expect(std.mem.indexOf(u8, signed.authorization, "AKIDEXAMPLE") != null);
    try std.testing.expect(std.mem.indexOf(u8, signed.authorization, "us-east-1/bedrock/aws4_request") != null);
    // Must contain Signature=<hex>
    try std.testing.expect(std.mem.indexOf(u8, signed.authorization, "Signature=") != null);
    // x-amz-date must match the timestamp prefix.
    const date_str: []const u8 = &signed.x_amz_date;
    try std.testing.expectEqualStrings("20231201T120000Z", date_str);
}

test "bedrock auth: signRequest with session token includes security token header" {
    const alloc = std.testing.allocator;

    const creds = bedrock.Credentials{
        .access_key_id = "AKIDEXAMPLE",
        .secret_access_key = "secret",
        .session_token = "SESSION_TOKEN_HERE",
    };

    var signed = try bedrock.signRequest(
        alloc,
        "POST",
        "https://bedrock-runtime.us-west-2.amazonaws.com/model/test-model/converse",
        "{}",
        creds,
        "us-west-2",
        "bedrock",
        "20231201T120000Z",
    );
    defer signed.deinit(alloc);

    try std.testing.expect(signed.x_amz_security_token != null);
    try std.testing.expectEqualStrings("SESSION_TOKEN_HERE", signed.x_amz_security_token.?);
}

// ---------------------------------------------------------------------------
// BedrockProvider init and endpoint URL
// ---------------------------------------------------------------------------

test "bedrock auth: BedrockProvider.init stores region and model" {
    const alloc = std.testing.allocator;
    var p = try bedrock.BedrockProvider.init(alloc, "us-west-2", "anthropic.claude-3-sonnet");
    defer p.deinit();

    try std.testing.expectEqualStrings("us-west-2", p.region);
    try std.testing.expectEqualStrings("anthropic.claude-3-sonnet", p.model_id);
}

test "bedrock auth: BedrockProvider.init with empty region uses default" {
    const alloc = std.testing.allocator;
    var p = try bedrock.BedrockProvider.init(alloc, "", "test-model");
    defer p.deinit();

    try std.testing.expectEqualStrings(bedrock.default_region, p.region);
}

test "bedrock auth: BedrockProvider.buildEndpointUrl includes region and model" {
    const alloc = std.testing.allocator;
    var p = try bedrock.BedrockProvider.init(alloc, "eu-west-1", "anthropic.claude-3-haiku");
    defer p.deinit();

    const url = try p.buildEndpointUrl();
    defer alloc.free(url);

    try std.testing.expect(std.mem.indexOf(u8, url, "eu-west-1") != null);
    try std.testing.expect(std.mem.indexOf(u8, url, "anthropic.claude-3-haiku") != null);
    try std.testing.expect(std.mem.startsWith(u8, url, "https://"));
}

test "bedrock auth: BedrockProvider.buildEndpointUrl format is correct" {
    const alloc = std.testing.allocator;
    var p = try bedrock.BedrockProvider.init(alloc, "us-east-1", "my-model");
    defer p.deinit();

    const url = try p.buildEndpointUrl();
    defer alloc.free(url);

    const expected = "https://bedrock-runtime.us-east-1.amazonaws.com/model/my-model/converse";
    try std.testing.expectEqualStrings(expected, url);
}
