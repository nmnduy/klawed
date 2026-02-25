//! commands.zig — Command registration and dispatch
//!
//! Zig port of src/commands.c + src/config_command.c + src/provider_command.c.
//!
//! Implements the slash-command system (e.g. /help, /clear, /exit, /provider)
//! used in interactive mode.  The TUI integration is Phase 9; this module
//! contains only the pure dispatch logic.
//!
//! ## Usage
//! ```zig
//! var registry = CommandRegistry.init(allocator);
//! defer registry.deinit();
//!
//! // Register built-in commands.
//! try registry.registerBuiltins();
//!
//! // Execute a command from user input.
//! const result = try registry.execute(state, "/help");
//! ```

const std = @import("std");

// ---------------------------------------------------------------------------
// Command result
// ---------------------------------------------------------------------------

pub const CommandResultKind = enum {
    ok,
    /// Application should quit.
    exit,
    /// Unknown command.
    not_found,
    /// Command failed.
    @"error",
};

pub const CommandResult = struct {
    kind: CommandResultKind,
    /// Optional message for the UI (not owned; points into static strings or
    /// caller-managed memory).
    message: ?[]const u8 = null,
};

// ---------------------------------------------------------------------------
// Command handler signature
// ---------------------------------------------------------------------------

/// Context passed to every command handler.
pub const CommandContext = struct {
    /// Opaque pointer to the application state (ConversationState or similar).
    /// Commands cast this to the concrete type they need.
    state: ?*anyopaque = null,
    /// When true, commands should not print to stdout/stderr.
    tui_mode: bool = false,
};

pub const CommandHandlerFn = *const fn (
    ctx: CommandContext,
    args: []const u8,
) CommandResult;

// ---------------------------------------------------------------------------
// Command definition
// ---------------------------------------------------------------------------

pub const Command = struct {
    name: []const u8,
    usage: []const u8,
    description: []const u8,
    handler: CommandHandlerFn,
    /// Whether the command needs a real terminal (e.g. /voice, /vim).
    needs_terminal: bool = false,
};

// ---------------------------------------------------------------------------
// Built-in command handlers
// ---------------------------------------------------------------------------

fn cmdExit(_: CommandContext, _: []const u8) CommandResult {
    return .{ .kind = .exit };
}

fn cmdQuit(ctx: CommandContext, args: []const u8) CommandResult {
    return cmdExit(ctx, args);
}

fn cmdHelp(ctx: CommandContext, _: []const u8) CommandResult {
    if (!ctx.tui_mode) {
        // In non-TUI mode, print to stdout.
        const stdout = std.io.getStdOut().writer();
        stdout.writeAll("Available commands:\n") catch {};
        stdout.writeAll("  /help          Show this help\n") catch {};
        stdout.writeAll("  /clear         Clear conversation history\n") catch {};
        stdout.writeAll("  /exit, /quit   Exit interactive mode\n") catch {};
        stdout.writeAll("  /provider      View or switch LLM providers\n") catch {};
        stdout.writeAll("  /config        Modify configuration settings\n") catch {};
        stdout.writeAll("  /compact       Trigger context compaction\n") catch {};
        stdout.writeAll("  /dump [file]   Dump conversation to Markdown file\n") catch {};
        stdout.writeAll("  /add-dir <dir> Add directory to working directories\n") catch {};
        stdout.writeAll("  /vim           Open vim editor\n") catch {};
        stdout.writeAll("  /voice         Record voice input\n") catch {};
        stdout.writeAll("  /themes        Browse color themes\n") catch {};
        stdout.writeAll("\nType /help to see this list again.\n") catch {};
    }
    return .{ .kind = .ok };
}

fn cmdClear(_: CommandContext, _: []const u8) CommandResult {
    // The actual conversation clearing happens at a higher level; here we
    // signal success and let the caller handle state mutation.
    return .{ .kind = .ok, .message = "Conversation cleared" };
}

fn cmdProvider(ctx: CommandContext, args: []const u8) CommandResult {
    if (!ctx.tui_mode) {
        const stdout = std.io.getStdOut().writer();
        if (args.len == 0 or std.mem.eql(u8, args, "list")) {
            stdout.writeAll("Available providers: openai, anthropic, bedrock, deepseek, moonshot, kimi\n") catch {};
            stdout.writeAll("Set active provider with: /provider <name>\n") catch {};
            stdout.writeAll("Or via environment: KLAWED_LLM_PROVIDER=<name>\n") catch {};
        } else {
            stdout.print("Switching provider to: {s}\n", .{args}) catch {};
        }
    }
    return .{ .kind = .ok };
}

fn cmdConfig(ctx: CommandContext, args: []const u8) CommandResult {
    if (!ctx.tui_mode) {
        const stdout = std.io.getStdOut().writer();
        if (args.len == 0) {
            stdout.writeAll("Usage: /config <setting> <value>\n") catch {};
            stdout.writeAll("Example: /config llm_provider anthropic\n") catch {};
        } else {
            stdout.print("Config: {s}\n", .{args}) catch {};
        }
    }
    return .{ .kind = .ok };
}

fn cmdCompact(_: CommandContext, _: []const u8) CommandResult {
    return .{ .kind = .ok, .message = "Compaction requested" };
}

fn cmdDump(_: CommandContext, _: []const u8) CommandResult {
    return .{ .kind = .ok, .message = "Dump requested" };
}

fn cmdAddDir(_: CommandContext, args: []const u8) CommandResult {
    if (args.len == 0) {
        return .{ .kind = .@"error", .message = "Usage: /add-dir <path>" };
    }
    return .{ .kind = .ok };
}

fn cmdVim(_: CommandContext, _: []const u8) CommandResult {
    return .{ .kind = .ok };
}

fn cmdVoice(_: CommandContext, _: []const u8) CommandResult {
    return .{ .kind = .ok };
}

fn cmdThemes(_: CommandContext, _: []const u8) CommandResult {
    return .{ .kind = .ok };
}

fn cmdAutocompact(_: CommandContext, _: []const u8) CommandResult {
    return .{ .kind = .ok, .message = "Auto-compaction toggled" };
}

// ---------------------------------------------------------------------------
// Built-in command definitions (static)
// ---------------------------------------------------------------------------

const BUILTIN_COMMANDS = [_]Command{
    .{ .name = "exit",       .usage = "/exit",              .description = "Exit interactive mode",                        .handler = cmdExit },
    .{ .name = "quit",       .usage = "/quit",              .description = "Exit interactive mode",                        .handler = cmdQuit },
    .{ .name = "q",          .usage = "/q",                 .description = "Exit interactive mode",                        .handler = cmdQuit },
    .{ .name = "help",       .usage = "/help",              .description = "Show this help",                               .handler = cmdHelp },
    .{ .name = "clear",      .usage = "/clear",             .description = "Clear conversation history",                   .handler = cmdClear },
    .{ .name = "provider",   .usage = "/provider [name]",   .description = "View or switch LLM providers",                 .handler = cmdProvider },
    .{ .name = "config",     .usage = "/config <k> <v>",    .description = "Modify configuration settings",               .handler = cmdConfig },
    .{ .name = "compact",    .usage = "/compact",           .description = "Trigger context compaction",                   .handler = cmdCompact },
    .{ .name = "autocompact",.usage = "/autocompact",       .description = "Toggle automatic context compaction",          .handler = cmdAutocompact },
    .{ .name = "dump",       .usage = "/dump [file]",       .description = "Dump conversation to file",                   .handler = cmdDump },
    .{ .name = "add-dir",    .usage = "/add-dir <path>",    .description = "Add directory to working directories",         .handler = cmdAddDir },
    .{ .name = "vim",        .usage = "/vim",               .description = "Open vim editor",                             .handler = cmdVim,    .needs_terminal = true },
    .{ .name = "voice",      .usage = "/voice",             .description = "Record voice input",                          .handler = cmdVoice,  .needs_terminal = true },
    .{ .name = "themes",     .usage = "/themes",            .description = "Browse color themes",                         .handler = cmdThemes, .needs_terminal = true },
};

// ---------------------------------------------------------------------------
// Command registry
// ---------------------------------------------------------------------------

pub const CommandRegistry = struct {
    allocator: std.mem.Allocator,
    commands: std.ArrayList(Command),

    pub fn init(allocator: std.mem.Allocator) CommandRegistry {
        return .{
            .allocator = allocator,
            .commands = std.ArrayList(Command).init(allocator),
        };
    }

    pub fn deinit(self: *CommandRegistry) void {
        self.commands.deinit();
    }

    /// Register all built-in slash commands.
    pub fn registerBuiltins(self: *CommandRegistry) !void {
        for (&BUILTIN_COMMANDS) |*cmd| {
            try self.commands.append(cmd.*);
        }
    }

    /// Register a custom command.
    pub fn register(self: *CommandRegistry, cmd: Command) !void {
        try self.commands.append(cmd);
    }

    /// Look up a command by name (without the leading '/').
    pub fn lookup(self: *const CommandRegistry, name: []const u8) ?*const Command {
        for (self.commands.items) |*cmd| {
            if (std.mem.eql(u8, cmd.name, name)) return cmd;
        }
        return null;
    }

    /// Dispatch `input` (which must start with '/').
    /// Parses the command name and args, then calls the handler.
    pub fn execute(
        self: *const CommandRegistry,
        ctx: CommandContext,
        input: []const u8,
    ) CommandResult {
        if (input.len == 0 or input[0] != '/') {
            return .{ .kind = .not_found };
        }

        const cmd_line = input[1..];
        const space_idx = std.mem.indexOfScalar(u8, cmd_line, ' ');
        const cmd_name = if (space_idx) |i| cmd_line[0..i] else cmd_line;
        const args = if (space_idx) |i| std.mem.trimLeft(u8, cmd_line[i + 1 ..], " \t") else "";

        const cmd = self.lookup(cmd_name) orelse return .{ .kind = .not_found };
        return cmd.handler(ctx, args);
    }

    /// Return the count of registered commands.
    pub fn count(self: *const CommandRegistry) usize {
        return self.commands.items.len;
    }

    /// List all commands (read-only slice).
    pub fn list(self: *const CommandRegistry) []const Command {
        return self.commands.items;
    }

    /// Generate tab-completion candidates for a partial input line.
    /// `partial` should start with '/' (e.g. "/hel").
    /// Returns null if no matches.  Caller must free each string.
    pub fn tabComplete(
        self: *const CommandRegistry,
        allocator: std.mem.Allocator,
        partial: []const u8,
    ) !?[][]u8 {
        if (partial.len == 0 or partial[0] != '/') return null;

        const prefix = partial[1..]; // strip leading '/'
        var matches = std.ArrayList([]u8).init(allocator);
        errdefer {
            for (matches.items) |m| allocator.free(m);
            matches.deinit();
        }

        for (self.commands.items) |*cmd| {
            if (std.mem.startsWith(u8, cmd.name, prefix)) {
                const with_slash = try std.fmt.allocPrint(allocator, "/{s}", .{cmd.name});
                try matches.append(with_slash);
            }
        }

        if (matches.items.len == 0) {
            matches.deinit();
            return null;
        }

        return try matches.toOwnedSlice();
    }
};

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

test "CommandRegistry: registerBuiltins and count" {
    const alloc = std.testing.allocator;
    var reg = CommandRegistry.init(alloc);
    defer reg.deinit();

    try reg.registerBuiltins();
    try std.testing.expect(reg.count() > 0);
}

test "CommandRegistry: lookup found and not found" {
    const alloc = std.testing.allocator;
    var reg = CommandRegistry.init(alloc);
    defer reg.deinit();
    try reg.registerBuiltins();

    try std.testing.expect(reg.lookup("help") != null);
    try std.testing.expect(reg.lookup("exit") != null);
    try std.testing.expectEqual(@as(?*const Command, null), reg.lookup("nonexistent_cmd"));
}

test "CommandRegistry: execute /exit returns exit" {
    const alloc = std.testing.allocator;
    var reg = CommandRegistry.init(alloc);
    defer reg.deinit();
    try reg.registerBuiltins();

    const ctx = CommandContext{};
    const result = reg.execute(ctx, "/exit");
    try std.testing.expectEqual(CommandResultKind.exit, result.kind);
}

test "CommandRegistry: execute /quit returns exit" {
    const alloc = std.testing.allocator;
    var reg = CommandRegistry.init(alloc);
    defer reg.deinit();
    try reg.registerBuiltins();

    const result = reg.execute(.{}, "/quit");
    try std.testing.expectEqual(CommandResultKind.exit, result.kind);
}

test "CommandRegistry: execute unknown command returns not_found" {
    const alloc = std.testing.allocator;
    var reg = CommandRegistry.init(alloc);
    defer reg.deinit();
    try reg.registerBuiltins();

    const result = reg.execute(.{}, "/zzz_no_such_cmd");
    try std.testing.expectEqual(CommandResultKind.not_found, result.kind);
}

test "CommandRegistry: execute /clear returns ok" {
    const alloc = std.testing.allocator;
    var reg = CommandRegistry.init(alloc);
    defer reg.deinit();
    try reg.registerBuiltins();

    const result = reg.execute(.{}, "/clear");
    try std.testing.expectEqual(CommandResultKind.ok, result.kind);
}

test "CommandRegistry: execute /add-dir without args returns error" {
    const alloc = std.testing.allocator;
    var reg = CommandRegistry.init(alloc);
    defer reg.deinit();
    try reg.registerBuiltins();

    const result = reg.execute(.{}, "/add-dir");
    try std.testing.expectEqual(CommandResultKind.@"error", result.kind);
}

test "CommandRegistry: execute /add-dir with args returns ok" {
    const alloc = std.testing.allocator;
    var reg = CommandRegistry.init(alloc);
    defer reg.deinit();
    try reg.registerBuiltins();

    const result = reg.execute(.{}, "/add-dir /tmp");
    try std.testing.expectEqual(CommandResultKind.ok, result.kind);
}

test "CommandRegistry: tabComplete /hel" {
    const alloc = std.testing.allocator;
    var reg = CommandRegistry.init(alloc);
    defer reg.deinit();
    try reg.registerBuiltins();

    const matches = try reg.tabComplete(alloc, "/hel");
    defer if (matches) |m| {
        for (m) |s| alloc.free(s);
        alloc.free(m);
    };

    try std.testing.expect(matches != null);
    // "/help" must be among the matches.
    var found = false;
    for (matches.?) |m| {
        if (std.mem.eql(u8, m, "/help")) found = true;
    }
    try std.testing.expect(found);
}

test "CommandRegistry: tabComplete returns null for no matches" {
    const alloc = std.testing.allocator;
    var reg = CommandRegistry.init(alloc);
    defer reg.deinit();
    try reg.registerBuiltins();

    const matches = try reg.tabComplete(alloc, "/zzz_no_match");
    try std.testing.expectEqual(@as(?[][]u8, null), matches);
}

test "CommandRegistry: custom command registration" {
    const alloc = std.testing.allocator;
    var reg = CommandRegistry.init(alloc);
    defer reg.deinit();

    const custom: Command = .{
        .name = "mytest",
        .usage = "/mytest",
        .description = "Custom test command",
        .handler = struct {
            fn h(_: CommandContext, _: []const u8) CommandResult {
                return .{ .kind = .ok, .message = "custom ran" };
            }
        }.h,
    };
    try reg.register(custom);

    const result = reg.execute(.{}, "/mytest");
    try std.testing.expectEqual(CommandResultKind.ok, result.kind);
    try std.testing.expectEqualStrings("custom ran", result.message.?);
}
