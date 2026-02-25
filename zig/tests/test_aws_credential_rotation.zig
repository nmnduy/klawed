//! tests/test_aws_credential_rotation.zig — Zig port of tests/test_aws_credential_rotation.c
//!
//! Tests AWS credential loading and rotation behaviour via providers/bedrock.zig:
//! - loadCredentials returns null when env vars not set
//! - loadCredentials returns credentials from environment variables
//! - Credential fields (access_key_id, secret_access_key, session_token)
//! - freeCredentials safely releases all memory
//! - SigV4 signing produces different results when credentials change
//! - Multiple rotation cycles produce different signatures

const std = @import("std");
const bedrock = @import("../providers/bedrock.zig");

// ---------------------------------------------------------------------------
// Test 1: No credentials → loadCredentials returns null
// ---------------------------------------------------------------------------

test "aws credential rotation: no env vars → loadCredentials returns null" {
    const alloc = std.testing.allocator;

    // Only run if AWS_ACCESS_KEY_ID is not in the environment.
    const key = std.process.getEnvVarOwned(alloc, "AWS_ACCESS_KEY_ID") catch null;
    defer if (key) |k| alloc.free(k);

    if (key != null) return error.SkipZigTest;

    const creds_opt = try bedrock.loadCredentials(alloc);
    try std.testing.expect(creds_opt == null);
}

// ---------------------------------------------------------------------------
// Test 2: Credentials loaded from env vars
// ---------------------------------------------------------------------------

test "aws credential rotation: env vars → loadCredentials returns credentials" {
    const alloc = std.testing.allocator;

    // Skip if creds not configured.
    const key_id = std.process.getEnvVarOwned(alloc, "AWS_ACCESS_KEY_ID") catch {
        return error.SkipZigTest;
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

    // Should have non-empty key and secret.
    try std.testing.expect(creds.access_key_id.len > 0);
    try std.testing.expect(creds.secret_access_key.len > 0);
    // access_key_id should match the environment variable.
    try std.testing.expectEqualStrings(key_id, creds.access_key_id);
}

// ---------------------------------------------------------------------------
// Test 3: Session token is optional
// ---------------------------------------------------------------------------

test "aws credential rotation: session token is optional" {
    const alloc = std.testing.allocator;

    const key_id = std.process.getEnvVarOwned(alloc, "AWS_ACCESS_KEY_ID") catch {
        return error.SkipZigTest;
    };
    defer alloc.free(key_id);
    const secret = std.process.getEnvVarOwned(alloc, "AWS_SECRET_ACCESS_KEY") catch {
        return error.SkipZigTest;
    };
    defer alloc.free(secret);

    var creds_opt = try bedrock.loadCredentials(alloc);
    defer if (creds_opt) |*c| bedrock.freeCredentials(alloc, c);

    // session_token may be null (not set in env) or non-null (set in env).
    // Either way, the struct must be valid.
    if (creds_opt) |creds| {
        _ = creds.session_token; // Just access — no assertion on value
    }
}

// ---------------------------------------------------------------------------
// Test 4: freeCredentials is safe to call multiple times with zeroed struct
// ---------------------------------------------------------------------------

test "aws credential rotation: freeCredentials safe with allocated credentials" {
    const alloc = std.testing.allocator;

    // Construct credentials manually (simulate what loadCredentials returns).
    var creds = bedrock.Credentials{
        .access_key_id = try alloc.dupe(u8, "AKIDTEST"),
        .secret_access_key = try alloc.dupe(u8, "SECRETTEST"),
        .session_token = null,
    };
    bedrock.freeCredentials(alloc, &creds);
    // Must not crash — no double-free because freeCredentials frees each field once
}

test "aws credential rotation: freeCredentials safe with session token" {
    const alloc = std.testing.allocator;

    var creds = bedrock.Credentials{
        .access_key_id = try alloc.dupe(u8, "AKIDTEST"),
        .secret_access_key = try alloc.dupe(u8, "SECRETTEST"),
        .session_token = try alloc.dupe(u8, "SESSIONTOKEN"),
    };
    bedrock.freeCredentials(alloc, &creds);
    // No crash, no leak
}

// ---------------------------------------------------------------------------
// Test 5: SigV4 signature changes when credentials change (rotation simulation)
// ---------------------------------------------------------------------------

test "aws credential rotation: different credentials produce different signatures" {
    const alloc = std.testing.allocator;

    const creds_v1 = bedrock.Credentials{
        .access_key_id = "AKID_VERSION_1",
        .secret_access_key = "SECRET_VERSION_1",
        .session_token = null,
    };
    const creds_v2 = bedrock.Credentials{
        .access_key_id = "AKID_VERSION_2",
        .secret_access_key = "SECRET_VERSION_2",
        .session_token = null,
    };

    var signed1 = try bedrock.signRequest(
        alloc,
        "POST",
        "https://bedrock-runtime.us-east-1.amazonaws.com/model/test/converse",
        "{}",
        creds_v1,
        "us-east-1",
        "bedrock",
        "20231201T120000Z",
    );
    defer signed1.deinit(alloc);

    var signed2 = try bedrock.signRequest(
        alloc,
        "POST",
        "https://bedrock-runtime.us-east-1.amazonaws.com/model/test/converse",
        "{}",
        creds_v2,
        "us-east-1",
        "bedrock",
        "20231201T120000Z",
    );
    defer signed2.deinit(alloc);

    // Different credentials must produce different Authorization headers.
    try std.testing.expect(!std.mem.eql(u8, signed1.authorization, signed2.authorization));
    // Both must contain their respective access key IDs.
    try std.testing.expect(std.mem.indexOf(u8, signed1.authorization, "AKID_VERSION_1") != null);
    try std.testing.expect(std.mem.indexOf(u8, signed2.authorization, "AKID_VERSION_2") != null);
}

// ---------------------------------------------------------------------------
// Test 6: Multiple rotation cycles — each produces unique signatures
// ---------------------------------------------------------------------------

test "aws credential rotation: multiple rotation cycles produce unique signatures" {
    const alloc = std.testing.allocator;

    const credential_versions = [_]bedrock.Credentials{
        .{ .access_key_id = "AKIA_V1", .secret_access_key = "SECRET_V1", .session_token = null },
        .{ .access_key_id = "AKIA_V2", .secret_access_key = "SECRET_V2", .session_token = null },
        .{ .access_key_id = "AKIA_V3", .secret_access_key = "SECRET_V3", .session_token = null },
    };

    var signatures: [3][]u8 = undefined;
    var sig_count: usize = 0;

    for (credential_versions, 0..) |creds, i| {
        var signed = try bedrock.signRequest(
            alloc,
            "POST",
            "https://bedrock-runtime.us-west-2.amazonaws.com/model/test/converse",
            "{\"messages\":[]}",
            creds,
            "us-west-2",
            "bedrock",
            "20231201T120000Z",
        );
        // Copy the authorization header before deinitialization
        signatures[i] = try alloc.dupe(u8, signed.authorization);
        signed.deinit(alloc);
        sig_count += 1;
    }

    defer for (signatures[0..sig_count]) |sig| alloc.free(sig);

    // All three signatures must be distinct.
    try std.testing.expect(!std.mem.eql(u8, signatures[0], signatures[1]));
    try std.testing.expect(!std.mem.eql(u8, signatures[1], signatures[2]));
    try std.testing.expect(!std.mem.eql(u8, signatures[0], signatures[2]));
}

// ---------------------------------------------------------------------------
// Test 7: SigV4 — same credentials, different payloads → different signatures
// ---------------------------------------------------------------------------

test "aws credential rotation: different payloads produce different signatures" {
    const alloc = std.testing.allocator;

    const creds = bedrock.Credentials{
        .access_key_id = "AKIDEXAMPLE",
        .secret_access_key = "wJalrXUtnFEMI/K7MDENG+bPxRfiCYEXAMPLEKEY",
        .session_token = null,
    };

    var signed1 = try bedrock.signRequest(
        alloc,
        "POST",
        "https://bedrock-runtime.us-east-1.amazonaws.com/model/test/converse",
        "{\"messages\":[]}",
        creds,
        "us-east-1",
        "bedrock",
        "20231201T120000Z",
    );
    defer signed1.deinit(alloc);

    var signed2 = try bedrock.signRequest(
        alloc,
        "POST",
        "https://bedrock-runtime.us-east-1.amazonaws.com/model/test/converse",
        "{\"messages\":[{\"role\":\"user\",\"content\":\"hello\"}]}",
        creds,
        "us-east-1",
        "bedrock",
        "20231201T120000Z",
    );
    defer signed2.deinit(alloc);

    // Signatures should differ because the payload hash differs.
    try std.testing.expect(!std.mem.eql(u8, signed1.authorization, signed2.authorization));
}

// ---------------------------------------------------------------------------
// Test 8: Credentials struct fields are accessible
// ---------------------------------------------------------------------------

test "aws credential rotation: Credentials struct has expected fields" {
    const creds = bedrock.Credentials{
        .access_key_id = "AKID",
        .secret_access_key = "SECRET",
        .session_token = "TOKEN",
    };

    try std.testing.expectEqualStrings("AKID", creds.access_key_id);
    try std.testing.expectEqualStrings("SECRET", creds.secret_access_key);
    try std.testing.expect(creds.session_token != null);
    try std.testing.expectEqualStrings("TOKEN", creds.session_token.?);
}

test "aws credential rotation: Credentials session_token is null by default" {
    const creds = bedrock.Credentials{
        .access_key_id = "AKID",
        .secret_access_key = "SECRET",
    };
    try std.testing.expect(creds.session_token == null);
}
