//! tools/image.zig — UploadImage tool implementation
//!
//! Zig port of src/tools/tool_image.c
//!
//! Reads an image file, detects its MIME type from magic bytes (with extension
//! fallback), base64-encodes the data, and returns a JSON result with the
//! encoded image that can be embedded as an image content block.

const std = @import("std");
const utils = @import("utils.zig");

pub const ToolResult = utils.ToolResult;

/// Maximum image file size (20 MiB).
pub const max_image_size: usize = 20 * 1024 * 1024;

// ---------------------------------------------------------------------------
// MIME type detection
// ---------------------------------------------------------------------------

/// Detect MIME type from file extension (case-insensitive).
fn mimeFromExtension(path: []const u8) []const u8 {
    const ext = std.fs.path.extension(path);
    if (ext.len == 0) return "image/jpeg";

    // Case-insensitive comparison via toLower on the 4-char buffer
    var lower: [8]u8 = undefined;
    const n = @min(ext.len, lower.len);
    for (ext[0..n], 0..) |c, i| lower[i] = std.ascii.toLower(c);
    const lo = lower[0..n];

    if (std.mem.eql(u8, lo, ".png")) return "image/png";
    if (std.mem.eql(u8, lo, ".jpg") or std.mem.eql(u8, lo, ".jpeg")) return "image/jpeg";
    if (std.mem.eql(u8, lo, ".gif")) return "image/gif";
    if (std.mem.eql(u8, lo, ".webp")) return "image/webp";
    if (std.mem.eql(u8, lo, ".bmp")) return "image/bmp";
    if (std.mem.eql(u8, lo, ".tiff") or std.mem.eql(u8, lo, ".tif")) return "image/tiff";
    if (std.mem.eql(u8, lo, ".svg")) return "image/svg+xml";
    return "image/jpeg";
}

/// Detect MIME type from magic bytes (file signature).
/// Returns null if the signature is not recognised.
fn mimeFromMagic(data: []const u8) ?[]const u8 {
    if (data.len >= 8) {
        // PNG: \x89PNG\r\n\x1a\n
        if (data[0] == 0x89 and data[1] == 'P' and data[2] == 'N' and data[3] == 'G' and
            data[4] == 0x0D and data[5] == 0x0A and data[6] == 0x1A and data[7] == 0x0A)
        {
            return "image/png";
        }
        // JPEG: \xff\xd8\xff
        if (data[0] == 0xFF and data[1] == 0xD8 and data[2] == 0xFF) return "image/jpeg";
        // GIF: GIF87a or GIF89a
        if (data[0] == 'G' and data[1] == 'I' and data[2] == 'F' and data[3] == '8' and
            (data[4] == '7' or data[4] == '9') and data[5] == 'a')
        {
            return "image/gif";
        }
        // BMP: BM
        if (data[0] == 'B' and data[1] == 'M') return "image/bmp";
        // TIFF: II or MM
        if ((data[0] == 'I' and data[1] == 'I') or (data[0] == 'M' and data[1] == 'M')) {
            return "image/tiff";
        }
    }
    if (data.len >= 12) {
        // WebP: RIFF....WEBP
        if (data[0] == 'R' and data[1] == 'I' and data[2] == 'F' and data[3] == 'F' and
            data[8] == 'W' and data[9] == 'E' and data[10] == 'B' and data[11] == 'P')
        {
            return "image/webp";
        }
    }
    return null;
}

// ---------------------------------------------------------------------------
// Execute
// ---------------------------------------------------------------------------

/// Execute the UploadImage tool.
///
/// Input: `{ "file_path": <path> }`
pub fn execute(allocator: std.mem.Allocator, input: std.json.Value) !ToolResult {
    const raw_path = utils.jsonString(input, "file_path") orelse {
        return utils.errLit("Missing 'file_path' parameter");
    };

    // Clean path: strip embedded newlines and trailing whitespace
    const cleaned = blk: {
        var buf = std.ArrayList(u8).init(allocator);
        defer buf.deinit();
        for (raw_path) |c| {
            if (c != '\n' and c != '\r') try buf.append(c);
        }
        break :blk try std.mem.Allocator.dupe(allocator, u8, std.mem.trimRight(u8, buf.items, &std.ascii.whitespace));
    };
    defer allocator.free(cleaned);

    // Open file
    const file = openAny(cleaned) catch |e| {
        return utils.errFmt(allocator, "Cannot read image file '{s}': {s}", .{ cleaned, @errorName(e) });
    };
    defer file.close();

    // Get file size
    const stat = file.stat() catch |e| {
        return utils.errFmt(allocator, "Cannot stat image file '{s}': {s}", .{ cleaned, @errorName(e) });
    };
    if (stat.size == 0) return utils.errLit("Image file is empty or invalid");
    if (stat.size > max_image_size) {
        return utils.errFmt(allocator, "Image file too large: {d} bytes (max {d})", .{ stat.size, max_image_size });
    }

    // Read file
    const image_data = file.readToEndAlloc(allocator, max_image_size) catch |e| {
        return utils.errFmt(allocator, "Failed to read image file: {s}", .{@errorName(e)});
    };
    defer allocator.free(image_data);

    // Determine MIME type: magic bytes override extension
    const mime_type = mimeFromMagic(image_data) orelse mimeFromExtension(cleaned);

    // Base64 encode
    const encoded_len = std.base64.standard.Encoder.calcSize(image_data.len);
    const base64_data = try allocator.alloc(u8, encoded_len);
    defer allocator.free(base64_data);
    _ = std.base64.standard.Encoder.encode(base64_data, image_data);

    // Build JSON result
    var out = std.ArrayList(u8).init(allocator);
    defer out.deinit();
    const w = out.writer();

    try w.writeAll("{\"status\":\"success\"");
    try w.writeAll(",\"message\":\"Image uploaded successfully\"");
    try w.writeAll(",\"file_path\":");
    try writeJsonString(w, cleaned);
    try w.writeAll(",\"original_path\":");
    try writeJsonString(w, raw_path);
    try w.writeAll(",\"mime_type\":");
    try writeJsonString(w, mime_type);
    try std.fmt.format(w, ",\"file_size_bytes\":{d}", .{image_data.len});
    try w.writeAll(",\"base64_data\":");
    try writeJsonString(w, base64_data);
    try w.writeAll(",\"content_type\":\"image\"");
    try w.writeByte('}');

    return utils.okOwned(try out.toOwnedSlice());
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

fn openAny(path: []const u8) !std.fs.File {
    if (std.fs.path.isAbsolute(path)) {
        return std.fs.openFileAbsolute(path, .{});
    }
    return std.fs.cwd().openFile(path, .{});
}

fn writeJsonString(writer: anytype, s: []const u8) !void {
    try writer.writeByte('"');
    for (s) |c| {
        switch (c) {
            '"' => try writer.writeAll("\\\""),
            '\\' => try writer.writeAll("\\\\"),
            '\n' => try writer.writeAll("\\n"),
            '\r' => try writer.writeAll("\\r"),
            '\t' => try writer.writeAll("\\t"),
            0x00...0x08, 0x0B...0x0C, 0x0E...0x1F => try std.fmt.format(writer, "\\u{X:0>4}", .{c}),
            else => try writer.writeByte(c),
        }
    }
    try writer.writeByte('"');
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

test "mimeFromExtension" {
    try std.testing.expectEqualStrings("image/png", mimeFromExtension("foo.PNG"));
    try std.testing.expectEqualStrings("image/jpeg", mimeFromExtension("bar.jpg"));
    try std.testing.expectEqualStrings("image/gif", mimeFromExtension("anim.GIF"));
    try std.testing.expectEqualStrings("image/webp", mimeFromExtension("photo.webp"));
    try std.testing.expectEqualStrings("image/jpeg", mimeFromExtension("noext"));
}

test "mimeFromMagic: PNG" {
    const png_magic = [_]u8{ 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };
    try std.testing.expectEqualStrings("image/png", mimeFromMagic(&png_magic).?);
}

test "mimeFromMagic: JPEG" {
    const jpeg_magic = [_]u8{ 0xFF, 0xD8, 0xFF, 0xE0, 0, 0, 0, 0 };
    try std.testing.expectEqualStrings("image/jpeg", mimeFromMagic(&jpeg_magic).?);
}

test "mimeFromMagic: unknown returns null" {
    const unknown = [_]u8{ 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07 };
    try std.testing.expect(mimeFromMagic(&unknown) == null);
}

test "execute image tool: missing file_path" {
    const allocator = std.testing.allocator;
    const parsed = try std.json.parseFromSlice(std.json.Value, allocator, "{}", .{});
    defer parsed.deinit();

    const result = try execute(allocator, parsed.value);
    try std.testing.expect(result.is_error);
}

test "execute image tool: nonexistent file returns error" {
    const allocator = std.testing.allocator;
    const json_text =
        \\{"file_path": "/tmp/klawed_test_nonexistent_image.png"}
    ;
    const parsed = try std.json.parseFromSlice(std.json.Value, allocator, json_text, .{});
    defer parsed.deinit();

    var result = try execute(allocator, parsed.value);
    defer result.deinit(allocator);
    try std.testing.expect(result.is_error);
}

test "execute image tool: reads and encodes a real file" {
    var tmp = std.testing.tmpDir(.{});
    defer tmp.cleanup();

    // Write a minimal valid PNG (1x1 pixel)
    const tiny_png = [_]u8{
        0x89, 'P',  'N',  'G',  0x0D, 0x0A, 0x1A, 0x0A, // PNG signature
        0x00, 0x00, 0x00, 0x0D, 'I',  'H',  'D',  'R',  // IHDR chunk
        0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01,
        0x08, 0x02, 0x00, 0x00, 0x00, 0x90, 0x77, 0x53,
        0xDE, 0x00, 0x00, 0x00, 0x0C, 'I',  'D',  'A',
        'T',  0x08, 0xD7, 0x63, 0xF8, 0xCF, 0xC0, 0x00,
        0x00, 0x00, 0x02, 0x00, 0x01, 0xE2, 0x21, 0xBC,
        0x33, 0x00, 0x00, 0x00, 0x00, 'I',  'E',  'N',
        'D',  0xAE, 0x42, 0x60, 0x82,
    };
    try tmp.dir.writeFile("test.png", &tiny_png);

    var path_buf: [std.fs.MAX_PATH_BYTES]u8 = undefined;
    const tmp_path = try tmp.dir.realpath(".", &path_buf);

    const allocator = std.testing.allocator;
    const json_text = try std.fmt.allocPrint(
        allocator,
        "{{\"file_path\":\"{s}/test.png\"}}",
        .{tmp_path},
    );
    defer allocator.free(json_text);

    const parsed = try std.json.parseFromSlice(std.json.Value, allocator, json_text, .{});
    defer parsed.deinit();

    var result = try execute(allocator, parsed.value);
    defer result.deinit(allocator);

    try std.testing.expect(!result.is_error);
    try std.testing.expect(std.mem.indexOf(u8, result.content, "image/png") != null);
    try std.testing.expect(std.mem.indexOf(u8, result.content, "base64_data") != null);
    try std.testing.expect(std.mem.indexOf(u8, result.content, "content_type") != null);
}
