//! main.zig — Zig-native klawed entry point
//!
//! This is the Phase 8 main entry point.  It wires together all previous
//! phases to produce a working binary (`klawed-zig`).
//!
//! ## What this entry point does
//!   1. Parses command-line arguments.
//!   2. Loads configuration (config.zig + provider_config_loader.zig).
//!   3. Initialises the provider (provider.zig).
//!   4. Sets up a ConversationState (conversation/state.zig).
//!   5. Builds the system prompt (context/system_prompt.zig).
//!   6. If a prompt argument was given → one-shot mode.
//!   7. Otherwise → stub interactive loop (Phase 9 adds TUI).
//!
//! ## TUI note
//! The ncurses TUI is Phase 9.  For now the interactive mode uses a plain
//! stdin readline loop from `interactive/interactive_loop.zig`.

const std = @import("std");
const builtin = @import("builtin");

// Core modules from previous phases.
const config_mod = @import("config.zig");
const provider_mod = @import("provider.zig");
const conversation_state = @import("conversation/state.zig");
const version_mod = @import("version.zig");

// Phase 8 modules.
const commands_mod = @import("commands.zig");
const interactive_loop = @import("interactive/interactive_loop.zig");
const oneshot_mode = @import("oneshot/mode.zig");
const websocket_mode = @import("websocket_mode.zig");

// ---------------------------------------------------------------------------
// Version
// ---------------------------------------------------------------------------

const KLAWED_VERSION = version_mod.VERSION;

// ---------------------------------------------------------------------------
// Argument parsing
// ---------------------------------------------------------------------------

pub const CliArgs = struct {
    /// One-shot prompt (if provided).
    prompt: ?[]const u8 = null,
    /// Show version and exit.
    show_version: bool = false,
    /// Show help and exit.
    show_help: bool = false,
    /// Enable auto-compaction.
    auto_compact: bool = false,
    /// Provider name override.
    provider_name: ?[]const u8 = null,
    /// Session to resume.
    resume_session: ?[]const u8 = null,
    /// True if the user requested --resume with no ID.
    resume_latest: bool = false,
    /// WebSocket listen address "host:port" or just ":port".
    /// When set, run in WebSocket daemon mode.
    ws_addr: ?[]const u8 = null,
};

pub fn parseArgs(args: []const []const u8) CliArgs {
    var result = CliArgs{};
    var i: usize = 1; // skip argv[0]

    while (i < args.len) : (i += 1) {
        const arg = args[i];

        if (std.mem.eql(u8, arg, "--version")) {
            result.show_version = true;
        } else if (std.mem.eql(u8, arg, "-h") or std.mem.eql(u8, arg, "--help")) {
            result.show_help = true;
        } else if (std.mem.eql(u8, arg, "--auto-compact")) {
            result.auto_compact = true;
        } else if (std.mem.eql(u8, arg, "-p") or std.mem.eql(u8, arg, "--provider")) {
            i += 1;
            if (i < args.len) result.provider_name = args[i];
        } else if (std.mem.eql(u8, arg, "-r") or std.mem.eql(u8, arg, "--resume")) {
            if (i + 1 < args.len and args[i + 1][0] != '-') {
                i += 1;
                result.resume_session = args[i];
            } else {
                result.resume_latest = true;
            }
        } else if (std.mem.eql(u8, arg, "--websocket") or std.mem.eql(u8, arg, "-w")) {
            i += 1;
            if (i < args.len) result.ws_addr = args[i];
        } else if (arg.len > 0 and arg[0] != '-') {
            // First positional argument is the one-shot prompt.
            if (result.prompt == null) {
                result.prompt = arg;
            }
        }
    }

    return result;
}

// ---------------------------------------------------------------------------
// Help text
// ---------------------------------------------------------------------------

fn printHelp(prog_name: []const u8, writer: anytype) !void {
    try writer.print(
        \\Klawed — Zig-native AI coding agent
        \\Version: {s}
        \\
        \\Usage:
        \\  {s}                          Start interactive mode
        \\  {s} "PROMPT"                 Execute single command and exit
        \\  {s} -p, --provider NAME      Use named provider from config
        \\  {s} -r, --resume [ID]        Resume a previous session
        \\  {s} -w, --websocket ADDR     Run WebSocket daemon (e.g. :9999)
        \\  {s} -h, --help               Show this help
        \\  {s} --version                Show version
        \\  {s} --auto-compact           Enable auto context compaction
        \\
        \\Environment Variables:
        \\  OPENAI_API_KEY       Required: Your API key
        \\  OPENAI_API_BASE      Optional: API base URL
        \\  OPENAI_MODEL         Optional: Model name
        \\  KLAWED_LLM_PROVIDER  Optional: Named provider from config
        \\  KLAWED_LOG_LEVEL     Optional: DEBUG/INFO/WARN/ERROR
        \\  KLAWED_BASH_TIMEOUT  Optional: Bash command timeout (seconds)
        \\  KLAWED_AUTO_COMPACT  Optional: 1 to enable auto-compaction
        \\  KLAWED_WS_HOST       Optional: WebSocket bind host (default 0.0.0.0)
        \\  KLAWED_WS_PORT       Optional: WebSocket bind port (default 9999)
        \\
        \\Interactive Tips:
        \\  Type /help for slash commands (/clear, /exit, /provider, ...)
        \\  Type :q or :quit to exit
        \\  Ctrl+D or EOF to exit
        \\
    , .{
        KLAWED_VERSION,
        prog_name,
        prog_name,
        prog_name,
        prog_name,
        prog_name,
        prog_name,
        prog_name,
        prog_name,
    });
}

// ---------------------------------------------------------------------------
// Stub API callbacks (Phase 8 — no live API calls without full wiring)
// ---------------------------------------------------------------------------

/// Stub initial-call callback for one-shot mode.
/// In Phase 8 this is a placeholder; real API wiring happens when the provider
/// and conversation state are fully connected.
fn stubInitialCall(
    _: std.mem.Allocator,
    prompt: []const u8,
    _: ?*anyopaque,
) anyerror!?oneshot_mode.AssistantResponse {
    const stdout = std.io.getStdOut().writer();
    try stdout.print("[klawed-zig] One-shot mode stub: prompt received ({} chars)\n", .{prompt.len});
    try stdout.print("[klawed-zig] Note: API not yet wired in Phase 8. Run the C binary for live calls.\n", .{});
    return oneshot_mode.AssistantResponse{
        .text = "[Stub response — Phase 9 will wire the real API]",
        .tool_calls = &.{},
    };
}

fn stubExecutor(
    _: std.mem.Allocator,
    call: oneshot_mode.ToolCall,
    _: ?*anyopaque,
) anyerror!oneshot_mode.ToolResult {
    return oneshot_mode.ToolResult{
        .tool_id = call.id,
        .tool_name = call.name,
        .result_json = "{}",
        .is_error = false,
    };
}

fn stubFollowup(
    _: std.mem.Allocator,
    _: ?*anyopaque,
) anyerror!?oneshot_mode.AssistantResponse {
    return null;
}

// ---------------------------------------------------------------------------
// Stub submit callback for interactive mode
// ---------------------------------------------------------------------------

fn stubSubmit(
    _: std.mem.Allocator,
    text: []const u8,
    _: ?*anyopaque,
) anyerror!bool {
    const stdout = std.io.getStdOut().writer();
    try stdout.print("[klawed-zig] Message received ({} chars). API not yet wired in Phase 8.\n", .{text.len});
    return true; // Continue loop.
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

pub fn main() !void {
    // Use a general-purpose allocator.
    var gpa = std.heap.GeneralPurposeAllocator(.{}){};
    defer _ = gpa.deinit();
    const allocator = gpa.allocator();

    // Process args.
    const process_args = try std.process.argsAlloc(allocator);
    defer std.process.argsFree(allocator, process_args);

    const cli = parseArgs(process_args);

    const stdout = std.io.getStdOut().writer();
    const stderr = std.io.getStdErr().writer();

    // --version
    if (cli.show_version) {
        try stdout.print("Klawed version {s} (Zig)\n", .{KLAWED_VERSION});
        return;
    }

    // --help
    if (cli.show_help) {
        const prog = if (process_args.len > 0) process_args[0] else "klawed-zig";
        try printHelp(prog, stdout);
        return;
    }

    // Load config (ignore errors — fall back to defaults).
    var cfg = config_mod.Config.load(allocator) catch config_mod.Config.init(allocator);
    defer cfg.deinit();

    // Apply CLI overrides.
    if (cli.provider_name) |pname| {
        // TODO: set KLAWED_LLM_PROVIDER env var when setenv is available.
        _ = pname;
    }

    // Check for auto-compact from env.
    var auto_compact = cli.auto_compact;
    if (!auto_compact) {
        if (std.posix.getenv("KLAWED_AUTO_COMPACT")) |v| {
            if (std.mem.eql(u8, v, "1") or
                std.ascii.eqlIgnoreCase(v, "true") or
                std.ascii.eqlIgnoreCase(v, "yes"))
            {
                auto_compact = true;
            }
        }
    }

    // Print startup info.
    try stdout.print("klawed-zig {s}\n", .{KLAWED_VERSION});
    if (auto_compact) {
        try stdout.print("[auto-compact enabled]\n", .{});
    }

    // ---- WEBSOCKET DAEMON MODE ----
    if (cli.ws_addr != null or std.posix.getenv("KLAWED_WS_PORT") != null) {
        var ws_cfg = websocket_mode.WsConfig.fromEnv();

        // Parse host:port from --websocket ADDR if provided.
        if (cli.ws_addr) |addr_str| {
            // Accept ":PORT", "HOST:PORT", or just "PORT".
            if (std.mem.lastIndexOf(u8, addr_str, ":")) |colon| {
                if (colon > 0) ws_cfg.host = addr_str[0..colon];
                const port_str = addr_str[colon + 1 ..];
                ws_cfg.port = std.fmt.parseInt(u16, port_str, 10) catch ws_cfg.port;
            } else {
                ws_cfg.port = std.fmt.parseInt(u16, addr_str, 10) catch ws_cfg.port;
            }
        }

        try stdout.print("klawed-zig {s} [websocket mode] listening on {s}:{d}\n", .{
            KLAWED_VERSION,
            ws_cfg.host,
            ws_cfg.port,
        });

        var global_shutdown = std.atomic.Value(bool).init(false);

        // Stub process callback (real AI wiring is done in the TUI phase).
        const stubProcessMessage: websocket_mode.ProcessMessageFn = struct {
            fn process(
                alloc: std.mem.Allocator,
                json: []const u8,
                out: *websocket_mode.WsOutChannel,
                interrupt: *websocket_mode.InterruptFlag,
                _: ?*anyopaque,
            ) void {
                _ = interrupt;
                const content = websocket_mode.extractContent(json) orelse "(no content)";
                const reply = websocket_mode.buildTextMessage(alloc, "TEXT", content) catch return;
                defer alloc.free(reply);
                out.push(reply) catch {};
                const end = websocket_mode.buildEndTurnMessage(alloc) catch return;
                defer alloc.free(end);
                out.push(end) catch {};
            }
        }.process;

        try websocket_mode.runWsDaemon(
            allocator,
            ws_cfg,
            stubProcessMessage,
            null,
            &global_shutdown,
        );
        return;
    }

    // ---- ONE-SHOT MODE ----
    if (cli.prompt) |prompt| {
        const oneshot_cfg = oneshot_mode.configFromEnv();
        const exit_code = try oneshot_mode.executeOneshot(
            allocator,
            prompt,
            stubInitialCall,
            stubExecutor,
            stubFollowup,
            null,
            oneshot_cfg,
        );
        if (exit_code != 0) {
            try stderr.print("Error: one-shot execution failed (code {})\n", .{exit_code});
            std.process.exit(@intCast(exit_code));
        }
        return;
    }

    // ---- INTERACTIVE MODE ----
    try stdout.print("Type /help for commands, :q to quit, Ctrl+D to exit.\n", .{});
    try stdout.print("Note: API not yet wired in Phase 8 — Phase 9 adds full TUI.\n\n", .{});

    // Set up command registry.
    var reg = commands_mod.CommandRegistry.init(allocator);
    defer reg.deinit();
    try reg.registerBuiltins();

    const loop_cfg = interactive_loop.LoopConfig{
        .prompt = ">>> ",
        .echo_input = true,
        .command_registry = &reg,
    };

    try interactive_loop.runInteractiveLoop(
        allocator,
        loop_cfg,
        stubSubmit,
        null,
    );

    try stdout.print("Goodbye!\n", .{});
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

test "parseArgs: empty args" {
    const args: []const []const u8 = &.{"klawed-zig"};
    const cli = parseArgs(args);
    try std.testing.expect(!cli.show_version);
    try std.testing.expect(!cli.show_help);
    try std.testing.expectEqual(@as(?[]const u8, null), cli.prompt);
}

test "parseArgs: --version flag" {
    const args: []const []const u8 = &.{ "klawed-zig", "--version" };
    const cli = parseArgs(args);
    try std.testing.expect(cli.show_version);
}

test "parseArgs: -h flag" {
    const args: []const []const u8 = &.{ "klawed-zig", "-h" };
    const cli = parseArgs(args);
    try std.testing.expect(cli.show_help);
}

test "parseArgs: --help flag" {
    const args: []const []const u8 = &.{ "klawed-zig", "--help" };
    const cli = parseArgs(args);
    try std.testing.expect(cli.show_help);
}

test "parseArgs: one-shot prompt" {
    const args: []const []const u8 = &.{ "klawed-zig", "write a test" };
    const cli = parseArgs(args);
    try std.testing.expect(cli.prompt != null);
    try std.testing.expectEqualStrings("write a test", cli.prompt.?);
}

test "parseArgs: --provider flag" {
    const args: []const []const u8 = &.{ "klawed-zig", "--provider", "anthropic" };
    const cli = parseArgs(args);
    try std.testing.expectEqualStrings("anthropic", cli.provider_name.?);
}

test "parseArgs: -p flag" {
    const args: []const []const u8 = &.{ "klawed-zig", "-p", "openai" };
    const cli = parseArgs(args);
    try std.testing.expectEqualStrings("openai", cli.provider_name.?);
}

test "parseArgs: --auto-compact" {
    const args: []const []const u8 = &.{ "klawed-zig", "--auto-compact" };
    const cli = parseArgs(args);
    try std.testing.expect(cli.auto_compact);
}

test "parseArgs: --resume with ID" {
    const args: []const []const u8 = &.{ "klawed-zig", "--resume", "abc123" };
    const cli = parseArgs(args);
    try std.testing.expectEqualStrings("abc123", cli.resume_session.?);
}

test "parseArgs: --resume without ID" {
    const args: []const []const u8 = &.{ "klawed-zig", "--resume" };
    const cli = parseArgs(args);
    try std.testing.expect(cli.resume_latest);
}

test "parseArgs: combined flags and prompt" {
    const args: []const []const u8 = &.{ "klawed-zig", "--auto-compact", "do a thing" };
    const cli = parseArgs(args);
    try std.testing.expect(cli.auto_compact);
    try std.testing.expectEqualStrings("do a thing", cli.prompt.?);
}

test "parseArgs: --websocket with address" {
    const args: []const []const u8 = &.{ "klawed-zig", "--websocket", "0.0.0.0:9999" };
    const cli = parseArgs(args);
    try std.testing.expectEqualStrings("0.0.0.0:9999", cli.ws_addr.?);
}

test "parseArgs: -w short flag" {
    const args: []const []const u8 = &.{ "klawed-zig", "-w", ":8080" };
    const cli = parseArgs(args);
    try std.testing.expectEqualStrings(":8080", cli.ws_addr.?);
}

test "parseArgs: --websocket without address leaves null" {
    const args: []const []const u8 = &.{ "klawed-zig", "--websocket" };
    const cli = parseArgs(args);
    try std.testing.expectEqual(@as(?[]const u8, null), cli.ws_addr);
}
