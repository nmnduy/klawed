const std = @import("std");

pub fn build(b: *std.Build) void {
    const target = b.standardTargetOptions(.{});
    const optimize = b.standardOptimizeOption(.{});

    // ---------------------------------------------------------------------------
    // Main binary — Zig-native (Phase 10 cutover: no more C sources)
    // ---------------------------------------------------------------------------
    const exe = b.addExecutable(.{
        .name = "klawed",
        .root_source_file = b.path("zig/main.zig"),
        .target = target,
        .optimize = optimize,
        .link_libc = true,
    });
    exe.linkSystemLibrary("sqlite3");
    exe.linkSystemLibrary("curl");
    exe.linkSystemLibrary("ssl");
    exe.linkSystemLibrary("crypto");

    // ncursesw on Linux, ncurses on macOS (TUI uses @cImport for ncurses)
    const target_result = target.result;
    if (target_result.os.tag == .macos) {
        exe.linkSystemLibrary("ncurses");
    } else {
        exe.linkSystemLibrary("ncursesw");
    }

    b.installArtifact(exe);

    // ---------------------------------------------------------------------------
    // "test" step — run Zig unit tests for all zig/ modules
    // ---------------------------------------------------------------------------
    const unit_tests = b.addTest(.{
        .root_source_file = b.path("zig/tests.zig"),
        .target = target,
        .optimize = optimize,
        // Link libc so that @cImport (used in timestamp_utils, logger, env_utils,
        // and the sqlite3-backed modules) can find system headers.
        .link_libc = true,
    });
    unit_tests.linkSystemLibrary("sqlite3");
    unit_tests.linkSystemLibrary("curl");
    const run_unit_tests = b.addRunArtifact(unit_tests);
    const test_step = b.step("test", "Run Zig unit tests (zig/tests.zig)");
    test_step.dependOn(&run_unit_tests.step);

    // ---------------------------------------------------------------------------
    // "debug" step — build with debug optimizations
    // ---------------------------------------------------------------------------
    const debug_exe = b.addExecutable(.{
        .name = "klawed-debug",
        .root_source_file = b.path("zig/main.zig"),
        .target = target,
        .optimize = .Debug,
        .link_libc = true,
    });
    debug_exe.linkSystemLibrary("sqlite3");
    debug_exe.linkSystemLibrary("curl");
    debug_exe.linkSystemLibrary("ssl");
    debug_exe.linkSystemLibrary("crypto");
    if (target_result.os.tag == .macos) {
        debug_exe.linkSystemLibrary("ncurses");
    } else {
        debug_exe.linkSystemLibrary("ncursesw");
    }

    const debug_step = b.step("debug", "Build debug binary (klawed-debug)");
    const debug_install = b.addInstallArtifact(debug_exe, .{});
    debug_step.dependOn(&debug_install.step);
}
