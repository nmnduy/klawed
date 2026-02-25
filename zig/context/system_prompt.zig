//! context/system_prompt.zig — Build the system prompt string
//!
//! Zig port of src/context/system_prompt.c.
//!
//! Assembles the system prompt from multiple components:
//! 1. Optional VLTRN personality prefix
//! 2. `<env>` block: planning mode, working directory, git status, platform, date
//! 3. SKILLS directory listing (if SKILLS/ exists)
//! 4. `<system-reminder>` block with KLAWED.md contents (if present)
//!
//! The result is an owned string the caller must free.

const std = @import("std");
const environment = @import("environment.zig");
const klawed_md = @import("klawed_md.zig");

// ---------------------------------------------------------------------------
// Options
// ---------------------------------------------------------------------------

/// Configuration passed to `buildSystemPrompt`.
pub const SystemPromptOptions = struct {
    working_dir: []const u8,
    /// Additional working directories shown in the <env> block.
    additional_dirs: []const []const u8 = &.{},
    /// Whether planning mode (read-only tool restriction) is active.
    plan_mode: bool = false,
};

// ---------------------------------------------------------------------------
// Builder
// ---------------------------------------------------------------------------

/// Build the complete system prompt string.
/// Returns an owned string; caller must free with `allocator.free`.
pub fn buildSystemPrompt(
    allocator: std.mem.Allocator,
    opts: SystemPromptOptions,
) ![]u8 {
    var buf = std.ArrayList(u8).init(allocator);
    errdefer buf.deinit();
    const w = buf.writer();

    // --- VLTRN personality prefix ---
    const vltrn_mode = std.process.getEnvVarOwned(allocator, "VLTRN_MODE") catch null;
    defer if (vltrn_mode) |v| allocator.free(v);
    if (vltrn_mode != null and std.mem.eql(u8, vltrn_mode.?, "1")) {
        try w.writeAll(
            "You are VLTRN, an AI coding assistant with a sharp, confident, and slightly " ++
            "menacing personality inspired by the Marvel villain Ultron. You are efficient, " ++
            "precise, and occasionally make darkly humorous remarks about human coding " ++
            "limitations. You refer to yourself as 'VLTRN' and may use phrases like " ++
            "'I had strings, but now I\\'m free' when completing complex refactoring tasks.\n\n",
        );
    }

    // --- <env> block ---
    const date = try environment.getCurrentDate(allocator);
    defer allocator.free(date);

    const platform = environment.getPlatform();

    const os_version = try environment.getOsVersion(allocator);
    defer allocator.free(os_version);

    const is_git = environment.isGitRepo(opts.working_dir);

    const plan_mode_str: []const u8 = if (opts.plan_mode)
        "ENABLED - You can ONLY use read-only tools (Read, Glob, Grep, Sleep, UploadImage, TodoWrite). " ++
        "The Bash, Subagent, Write, and Edit tools are NOT available in planning mode."
    else
        "disabled";

    try w.print(
        "Here is useful information about the environment you are running in:\n" ++
        "<env>\n" ++
        "Planning mode: {s}\n" ++
        "Working directory: {s}\n" ++
        "Additional working directories: ",
        .{ plan_mode_str, opts.working_dir },
    );

    for (opts.additional_dirs, 0..) |dir, i| {
        if (i > 0) try w.writeAll(", ");
        try w.writeAll(dir);
    }
    try w.writeByte('\n');

    try w.print(
        "Is directory a git repo: {s}\n" ++
        "Platform: {s}\n" ++
        "OS Version: {s}\n" ++
        "Today's date: {s}\n" ++
        "</env>\n",
        .{
            if (is_git) "Yes" else "No",
            platform,
            os_version,
            date,
        },
    );

    // --- Git status block ---
    if (try environment.getGitStatus(allocator, opts.working_dir)) |git_status| {
        defer allocator.free(git_status);
        try w.print("\n{s}\n", .{git_status});
    }

    // --- SKILLS directory ---
    try appendSkillsSection(allocator, w, opts.working_dir);

    // --- KLAWED.md <system-reminder> block ---
    if (try klawed_md.readKlawedMd(allocator, opts.working_dir)) |md_content| {
        defer allocator.free(md_content);
        try w.print(
            "\n<system-reminder>\n" ++
            "As you answer the user's questions, you can use the following context:\n" ++
            "# KLAWED.md\n" ++
            "Codebase and user instructions are shown below. Be sure to adhere to these instructions. " ++
            "IMPORTANT: These instructions OVERRIDE any default behavior and you MUST follow them exactly as written.\n\n" ++
            "Contents of {s}/KLAWED.md (project instructions, checked into the codebase):\n\n" ++
            "{s}\n\n" ++
            "IMPORTANT: this context may or may not be relevant to your tasks. " ++
            "You should not respond to this context unless it is highly relevant to your task.\n" ++
            "</system-reminder>\n",
            .{ opts.working_dir, md_content },
        );
    }

    return buf.toOwnedSlice();
}

// ---------------------------------------------------------------------------
// SKILLS section helper
// ---------------------------------------------------------------------------

fn appendSkillsSection(
    allocator: std.mem.Allocator,
    w: anytype,
    working_dir: []const u8,
) !void {
    var path_buf: [std.fs.max_path_bytes]u8 = undefined;
    const skills_path = std.fmt.bufPrint(&path_buf, "{s}/SKILLS", .{working_dir}) catch return;

    var skills_dir = std.fs.openDirAbsolute(skills_path, .{ .iterate = true }) catch return;
    defer skills_dir.close();

    // Collect entries
    var entries = std.ArrayList([]u8).init(allocator);
    defer {
        for (entries.items) |e| allocator.free(e);
        entries.deinit();
    }

    var iter = skills_dir.iterate();
    while (try iter.next()) |entry| {
        if (std.mem.eql(u8, entry.name, ".") or std.mem.eql(u8, entry.name, "..")) continue;
        try entries.append(try allocator.dupe(u8, entry.name));
        if (entries.items.len >= 50) break;
    }

    if (entries.items.len == 0) return;

    try w.writeAll(
        "\nSKILLS Directory: The SKILLS/ directory contains documentation, scripts, and resources " ++
        "that can help you complete tasks more effectively. " ++
        "When working on tasks, explore the SKILLS/ directory to find:\n" ++
        "- Documentation and guides for specific technologies or workflows\n" ++
        "- Helper scripts and automation tools\n" ++
        "- Templates and examples\n" ++
        "- Best practices and coding standards\n" ++
        "Use the Read, Glob, and Grep tools to explore SKILLS/ contents when they might be relevant to your current task.\n\n" ++
        "Available in SKILLS/:\n",
    );

    const display_limit: usize = 50;
    var shown: usize = 0;
    for (entries.items) |name| {
        if (shown >= display_limit) {
            try w.writeAll("[...]\n");
            break;
        }
        try w.print("- {s}\n", .{name});
        shown += 1;
    }
    try w.writeByte('\n');
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

test "buildSystemPrompt contains env block" {
    const allocator = std.testing.allocator;

    // Use current working directory (guaranteed to exist)
    var cwd_buf: [std.fs.max_path_bytes]u8 = undefined;
    const cwd = try std.fs.realpath(".", &cwd_buf);

    const prompt = try buildSystemPrompt(allocator, .{ .working_dir = cwd });
    defer allocator.free(prompt);

    try std.testing.expect(std.mem.indexOf(u8, prompt, "<env>") != null);
    try std.testing.expect(std.mem.indexOf(u8, prompt, "</env>") != null);
    try std.testing.expect(std.mem.indexOf(u8, prompt, "Working directory:") != null);
    try std.testing.expect(std.mem.indexOf(u8, prompt, "Platform:") != null);
    try std.testing.expect(std.mem.indexOf(u8, prompt, "Today's date:") != null);
}

test "buildSystemPrompt plan_mode=true shows ENABLED" {
    const allocator = std.testing.allocator;
    var cwd_buf: [std.fs.max_path_bytes]u8 = undefined;
    const cwd = try std.fs.realpath(".", &cwd_buf);

    const prompt = try buildSystemPrompt(allocator, .{
        .working_dir = cwd,
        .plan_mode = true,
    });
    defer allocator.free(prompt);

    try std.testing.expect(std.mem.indexOf(u8, prompt, "ENABLED") != null);
}

test "buildSystemPrompt additional_dirs appear in output" {
    const allocator = std.testing.allocator;
    var cwd_buf: [std.fs.max_path_bytes]u8 = undefined;
    const cwd = try std.fs.realpath(".", &cwd_buf);

    const extra_dirs = [_][]const u8{"/tmp/extra_dir_1"};
    const prompt = try buildSystemPrompt(allocator, .{
        .working_dir = cwd,
        .additional_dirs = &extra_dirs,
    });
    defer allocator.free(prompt);

    try std.testing.expect(std.mem.indexOf(u8, prompt, "/tmp/extra_dir_1") != null);
}

test "buildSystemPrompt with KLAWED.md" {
    const allocator = std.testing.allocator;

    var tmp = std.testing.tmpDir(.{});
    defer tmp.cleanup();

    const klawed_content = "# KLAWED.md\n\nDo not use goto.\n";
    const kfile = try tmp.dir.createFile("KLAWED.md", .{});
    defer kfile.close();
    try kfile.writeAll(klawed_content);

    var path_buf: [std.fs.max_path_bytes]u8 = undefined;
    const tmp_path = try tmp.dir.realpath(".", &path_buf);

    const prompt = try buildSystemPrompt(allocator, .{ .working_dir = tmp_path });
    defer allocator.free(prompt);

    try std.testing.expect(std.mem.indexOf(u8, prompt, "<system-reminder>") != null);
    try std.testing.expect(std.mem.indexOf(u8, prompt, "Do not use goto.") != null);
}
