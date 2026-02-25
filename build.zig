const std = @import("std");

pub fn build(b: *std.Build) void {
    const target = b.standardTargetOptions(.{});
    const optimize = b.standardOptimizeOption(.{});

    // Voice input flag: -Dvoice=true to enable whisper.cpp voice input.
    // Default is false — uses the stub implementation.
    const voice = b.option(bool, "voice", "Enable voice input (requires whisper.cpp)") orelse false;

    const exe = b.addExecutable(.{
        .name = "klawed",
        .target = target,
        .optimize = optimize,
    });

    // Compiler flags matching the Makefile (minus -Werror — the C code may
    // emit warnings under Zig's bundled clang that are not present under gcc).
    const c_flags = &[_][]const u8{
        "-std=c11",
        "-D_POSIX_C_SOURCE=200809L",
        "-D_DEFAULT_SOURCE=1",
        "-Wall",
        "-Wextra",
        "-O2",
        "-fstack-protector-strong",
        "-D_FORTIFY_SOURCE=2",
        // Voice flag propagated into C code
        if (voice) "-DHAVE_WHISPER=1" else "-DDISABLE_VOICE=1",
    };

    // -------------------------------------------------------------------------
    // ncurses_input.c contains '↵' (U+21B5) as a char literal. Zig's bundled
    // clang 17 hard-errors on multi-byte character literals that don't fit in
    // char; gcc only warns. We compile this one file via the system gcc and link
    // the resulting object file instead.
    // -------------------------------------------------------------------------
    const gcc_ncurses_input = b.addSystemCommand(&.{
        "gcc",
        "-std=c11",
        "-D_POSIX_C_SOURCE=200809L",
        "-D_DEFAULT_SOURCE=1",
        "-Wno-multichar",
        "-O2",
        "-fstack-protector-strong",
        "-D_FORTIFY_SOURCE=2",
        if (voice) "-DHAVE_WHISPER=1" else "-DDISABLE_VOICE=1",
        "-I", "src",
        "-c",
        "src/ncurses_input.c",
        "-o",
    });
    const ncurses_input_obj = gcc_ncurses_input.addOutputFileArg("ncurses_input.o");
    exe.addObjectFile(ncurses_input_obj);

    // All C source files (no-voice build: voice_stub.c instead of voice_input.c).
    // Generated from: find src/ -name "*.c" | sort
    // voice_input.c is excluded here and added conditionally below.
    const c_sources = &[_][]const u8{
        "src/ai_worker.c",
        "src/anthropic_provider.c",
        "src/api/api_builder.c",
        "src/api/api_client.c",
        "src/api/api_response.c",
        "src/array_resize.c",
        "src/aws_bedrock.c",
        "src/background_init.c",
        "src/base64.c",
        "src/bedrock_converse.c",
        "src/bedrock_provider.c",
        "src/builtin_themes.c",
        "src/commands.c",
        "src/compaction.c",
        "src/completion.c",
        "src/config.c",
        "src/config_command.c",
        "src/context/environment.c",
        "src/context/klawed_md.c",
        "src/context/memory_injection.c",
        "src/context/system_prompt.c",
        "src/conversation/content_types.c",
        "src/conversation/conversation_processor.c",
        "src/conversation/conversation_state.c",
        "src/conversation/message_builder.c",
        "src/conversation/message_parser.c",
        "src/data_dir.c",
        "src/deepseek_provider.c",
        "src/dump_utils.c",
        "src/dynamic_tools.c",
        "src/explore_tools.c",
        "src/file_search.c",
        "src/help_modal.c",
        "src/history_file.c",
        "src/history_search.c",
        "src/http_client.c",
        "src/interactive/command_dispatch.c",
        "src/interactive/input_handler.c",
        "src/interactive/interactive_loop.c",
        "src/interactive/response_processor.c",
        "src/kimi_coding_plan_provider.c",
        "src/kimi_oauth.c",
        "src/klawed.c",
        "src/logger.c",
        "src/mcp.c",
        "src/memory_db.c",
        "src/message_queue.c",
        "src/migrations.c",
        "src/moonshot_provider.c",
        // ncurses_input.c is compiled via gcc (see below) due to U+21B5 char literal
        "src/oneshot/oneshot_mode.c",
        "src/oneshot/oneshot_output.c",
        "src/oneshot/oneshot_processor.c",
        "src/oneshot/oneshot_ui.c",
        "src/openai_messages.c",
        "src/openai_provider.c",
        "src/openai_responses.c",
        "src/persistence.c",
        "src/process_utils.c",
        "src/provider.c",
        "src/provider_command.c",
        "src/provider_config_loader.c",
        "src/retry_logic.c",
        "src/session.c",
        "src/session/session_persistence.c",
        "src/session/token_usage.c",
        "src/sqlite_queue.c",
        "src/subagent_manager.c",
        "src/theme_explorer.c",
        "src/todo.c",
        "src/token_usage_db.c",
        "src/token_usage_db_migrations.c",
        "src/tools/tool_bash.c",
        "src/tools/tool_definitions.c",
        "src/tools/tool_executor.c",
        "src/tools/tool_filesystem.c",
        "src/tools/tool_image.c",
        "src/tools/tool_registry.c",
        "src/tools/tool_search.c",
        "src/tools/tool_sleep.c",
        "src/tools/tool_subagent.c",
        "src/tools/tool_todo.c",
        "src/tool_utils.c",
        "src/tui.c",
        "src/tui_completion.c",
        "src/tui_conversation.c",
        "src/tui_core.c",
        "src/tui_history.c",
        "src/tui_input.c",
        "src/tui_modes.c",
        "src/tui_paste.c",
        "src/tui_render.c",
        "src/tui_search.c",
        "src/tui_window.c",
        "src/ui/print_helpers.c",
        "src/ui/tool_output_display.c",
        "src/ui/ui_output.c",
        "src/util/diff_utils.c",
        "src/util/env_utils.c",
        "src/util/file_utils.c",
        "src/util/format_utils.c",
        "src/util/output_utils.c",
        "src/util/string_utils.c",
        "src/util/timestamp_utils.c",
        "src/vltrn_banner.c",
        "src/window_manager.c",
    };

    exe.addCSourceFiles(.{
        .files = c_sources,
        .flags = c_flags,
    });

    // Voice input: conditionally include voice_input.c or voice_stub.c
    if (voice) {
        exe.addCSourceFiles(.{
            .files = &.{"src/voice_input.c"},
            .flags = c_flags,
        });
    } else {
        exe.addCSourceFiles(.{
            .files = &.{"src/voice_stub.c"},
            .flags = c_flags,
        });
    }

    // Include path so that #include "header.h" and subdirectory includes resolve
    exe.addIncludePath(b.path("src"));

    // System library links — matching Makefile LDFLAGS
    exe.linkSystemLibrary("curl");
    exe.linkSystemLibrary("pthread");
    exe.linkSystemLibrary("sqlite3");
    exe.linkSystemLibrary("ssl");
    exe.linkSystemLibrary("crypto");
    exe.linkSystemLibrary("bsd");
    exe.linkSystemLibrary("m");
    exe.linkSystemLibrary("cjson");

    // ncursesw on Linux, ncurses on macOS
    const target_result = target.result;
    if (target_result.os.tag == .macos) {
        exe.linkSystemLibrary("ncurses");
    } else {
        exe.linkSystemLibrary("ncursesw");
    }

    // Must link libc for the C sources
    exe.linkLibC();

    b.installArtifact(exe);

    // ---------------------------------------------------------------------------
    // "test" step — Phase 2 + Phase 3: run Zig unit tests for all zig/ modules
    // ---------------------------------------------------------------------------
    const unit_tests = b.addTest(.{
        .root_source_file = b.path("zig/tests.zig"),
        .target = target,
        .optimize = optimize,
        // Link libc so that @cImport (used in timestamp_utils, logger, env_utils,
        // and the Phase 3 sqlite3-backed modules) can find system headers.
        .link_libc = true,
    });
    // Phase 3 modules use sqlite3 via @cImport.
    unit_tests.linkSystemLibrary("sqlite3");
    const run_unit_tests = b.addRunArtifact(unit_tests);
    const test_step = b.step("test", "Run Zig unit tests (zig/tests.zig)");
    test_step.dependOn(&run_unit_tests.step);

    // ---------------------------------------------------------------------------
    // "debug" step — build with debug optimizations (mirrors Makefile debug target)
    // ---------------------------------------------------------------------------
    const debug_exe = b.addExecutable(.{
        .name = "klawed-debug",
        .target = target,
        .optimize = .Debug,
    });

    const debug_c_flags = &[_][]const u8{
        "-std=c11",
        "-D_POSIX_C_SOURCE=200809L",
        "-D_DEFAULT_SOURCE=1",
        "-Wall",
        "-Wextra",
        "-g",
        "-O0",
        "-fstack-protector-strong",
        "-D_FORTIFY_SOURCE=2",
        if (voice) "-DHAVE_WHISPER=1" else "-DDISABLE_VOICE=1",
    };

    debug_exe.addCSourceFiles(.{
        .files = c_sources,
        .flags = debug_c_flags,
    });
    if (voice) {
        debug_exe.addCSourceFiles(.{
            .files = &.{"src/voice_input.c"},
            .flags = debug_c_flags,
        });
    } else {
        debug_exe.addCSourceFiles(.{
            .files = &.{"src/voice_stub.c"},
            .flags = debug_c_flags,
        });
    }
    debug_exe.addIncludePath(b.path("src"));
    debug_exe.linkSystemLibrary("curl");
    debug_exe.linkSystemLibrary("pthread");
    debug_exe.linkSystemLibrary("sqlite3");
    debug_exe.linkSystemLibrary("ssl");
    debug_exe.linkSystemLibrary("crypto");
    debug_exe.linkSystemLibrary("bsd");
    debug_exe.linkSystemLibrary("m");
    debug_exe.linkSystemLibrary("cjson");
    if (target_result.os.tag == .macos) {
        debug_exe.linkSystemLibrary("ncurses");
    } else {
        debug_exe.linkSystemLibrary("ncursesw");
    }
    debug_exe.linkLibC();

    const debug_step = b.step("debug", "Build debug binary (klawed-debug)");
    const debug_install = b.addInstallArtifact(debug_exe, .{});
    debug_step.dependOn(&debug_install.step);
}
