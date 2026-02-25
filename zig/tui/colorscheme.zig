//! Colorscheme module for TUI theming
//!
//! Provides RGB color handling, theme parsing from Kitty .conf format,
//! and ANSI 256-color conversion for terminal display.

const std = @import("std");

/// RGB color structure with 8-bit components
pub const Rgb = struct {
    r: u8,
    g: u8,
    b: u8,

    /// Initialize RGB from individual components
    pub fn init(r: u8, g: u8, b: u8) Rgb {
        return .{ .r = r, .g = g, .b = b };
    }

    /// Parse a hex color string like "#RRGGBB" or "RRGGBB"
    /// Returns null if parsing fails
    pub fn parse(hex: []const u8) ?Rgb {
        // Skip leading '#' if present
        const start: usize = if (hex.len > 0 and hex[0] == '#') 1 else 0;
        const hex_str = hex[start..];

        // Must have exactly 6 hex digits
        if (hex_str.len != 6) return null;

        const r = std.fmt.parseInt(u8, hex_str[0..2], 16) catch return null;
        const g = std.fmt.parseInt(u8, hex_str[2..4], 16) catch return null;
        const b = std.fmt.parseInt(u8, hex_str[4..6], 16) catch return null;

        return .{ .r = r, .g = g, .b = b };
    }

    /// Convert RGB to nearest 256-color palette index
    /// Supports both grayscale (232-255) and RGB cube (16-231)
    pub fn to256ColorIndex(self: Rgb) u8 {
        // Check if it's grayscale
        const avg = @divTrunc(@as(i16, self.r) + @as(i16, self.g) + @as(i16, self.b), @as(i16, 3));
        const r_diff = if (self.r > avg) self.r - avg else avg - self.r;
        const g_diff = if (self.g > avg) self.g - avg else avg - self.g;
        const b_diff = if (self.b > avg) self.b - avg else avg - self.b;

        if (r_diff < 10 and g_diff < 10 and b_diff < 10) {
            // Grayscale: use colors 232-255 (24 shades)
            const gray_index: u8 = @intCast(@divTrunc(avg * @as(i16, 23), @as(i16, 255)));
            return 232 + gray_index;
        }

        // RGB cube: 16-231
        const r_idx: u8 = @intCast((@as(u16, self.r) * 5) / 255);
        const g_idx: u8 = @intCast((@as(u16, self.g) * 5) / 255);
        const b_idx: u8 = @intCast((@as(u16, self.b) * 5) / 255);

        return 16 + (36 * r_idx) + (6 * g_idx) + b_idx;
    }

    /// Generate ANSI 256-color escape code for foreground color
    pub fn toAnsiCode(self: Rgb, buf: []u8) ![]u8 {
        const idx = self.to256ColorIndex();
        return try std.fmt.bufPrint(buf, "\x1b[38;5;{d}m", .{idx});
    }

    /// Generate ANSI 256-color escape code for background color
    pub fn toAnsiBgCode(self: Rgb, buf: []u8) ![]u8 {
        const idx = self.to256ColorIndex();
        return try std.fmt.bufPrint(buf, "\x1b[48;5;{d}m", .{idx});
    }
};

/// Colorscheme element types for ANSI escape code generation
pub const ColorschemeElement = enum {
    foreground, // Main text color for majority of content
    user,
    assistant,
    tool,
    err,
    status,
    diff_add, // Added lines in diffs (green)
    diff_remove, // Removed lines in diffs (red)
    diff_header, // Diff metadata/headers (cyan)
    diff_context, // Line numbers and context (dim)
    search, // Search highlight (magenta/color5)
    todo_accent, // TODO list accent color (magenta)
};

/// Theme structure holding parsed Kitty colors
pub const Theme = struct {
    foreground_rgb: Rgb = Rgb.init(0, 0, 0),
    background_rgb: Rgb = Rgb.init(0, 0, 0),
    assistant_rgb: Rgb = Rgb.init(0, 0, 0),
    user_rgb: Rgb = Rgb.init(0, 0, 0),
    status_rgb: Rgb = Rgb.init(0, 0, 0),
    error_rgb: Rgb = Rgb.init(0, 0, 0),
    header_rgb: Rgb = Rgb.init(0, 0, 0),
    tool_rgb: Rgb = Rgb.init(0, 0, 0),
    diff_add_rgb: Rgb = Rgb.init(0, 0, 0),
    diff_remove_rgb: Rgb = Rgb.init(0, 0, 0),
    diff_header_rgb: Rgb = Rgb.init(0, 0, 0),
    diff_context_rgb: Rgb = Rgb.init(0, 0, 0),
    search_rgb: Rgb = Rgb.init(0, 0, 0),
    todo_accent_rgb: Rgb = Rgb.init(0, 0, 0),

    /// Parse a hex color value for a specific key and set the corresponding field
    fn setColorFromKey(self: *Theme, key: []const u8, rgb: Rgb) bool {
        if (std.mem.eql(u8, key, "foreground")) {
            self.foreground_rgb = rgb;
            self.assistant_rgb = rgb;
            return true;
        } else if (std.mem.eql(u8, key, "background")) {
            self.background_rgb = rgb;
            return true;
        } else if (std.mem.eql(u8, key, "color2")) {
            self.user_rgb = rgb;
            return true;
        } else if (std.mem.eql(u8, key, "color3")) {
            self.status_rgb = rgb;
            return true;
        } else if (std.mem.eql(u8, key, "color12")) {
            self.tool_rgb = rgb;
            return true;
        } else if (std.mem.eql(u8, key, "color1")) {
            self.error_rgb = rgb;
            return true;
        } else if (std.mem.eql(u8, key, "color5")) {
            self.search_rgb = rgb;
            self.todo_accent_rgb = rgb;
            return true;
        } else if (std.mem.eql(u8, key, "color6")) {
            self.header_rgb = rgb;
            return true;
        } else if (std.mem.eql(u8, key, "color4")) {
            // Only use color4 for header if not already set by color6
            if (self.header_rgb.r == 0 and self.header_rgb.g == 0 and self.header_rgb.b == 0) {
                self.header_rgb = rgb;
            }
            return true;
        }
        return false;
    }

    /// Apply diff color mappings after main colors are set
    pub fn applyDiffMappings(self: *Theme) void {
        self.diff_add_rgb = self.user_rgb;
        self.diff_remove_rgb = self.error_rgb;
        self.diff_header_rgb = self.header_rgb;
        self.diff_context_rgb = self.foreground_rgb;

        // Make context slightly dimmer by reducing brightness (~40%)
        const avg = @divTrunc(@as(i16, self.diff_context_rgb.r) + @as(i16, self.diff_context_rgb.g) + @as(i16, self.diff_context_rgb.b), 3);
        if (avg > 100) {
            self.diff_context_rgb.r = @intCast((@as(u16, self.diff_context_rgb.r) * 6) / 10);
            self.diff_context_rgb.g = @intCast((@as(u16, self.diff_context_rgb.g) * 6) / 10);
            self.diff_context_rgb.b = @intCast((@as(u16, self.diff_context_rgb.b) * 6) / 10);
        }
    }

    /// Load theme from Kitty .conf format buffer
    pub fn loadFromBuffer(self: *Theme, content: []const u8) usize {
        var parsed_count: usize = 0;
        var found_foreground = false;

        var lines = std.mem.splitScalar(u8, content, '\n');
        while (lines.next()) |line| {
            // Trim whitespace
            const trimmed = std.mem.trim(u8, line, &std.ascii.whitespace);

            // Skip empty lines and comments
            if (trimmed.len == 0 or trimmed[0] == '#') continue;

            // Parse key-value pair (format: "key value")
            var parts = std.mem.splitScalar(u8, trimmed, ' ');
            const key = parts.next() orelse continue;

            // Skip to next non-empty part for value
            var value: ?[]const u8 = null;
            while (parts.next()) |part| {
                const trimmed_part = std.mem.trim(u8, part, &std.ascii.whitespace);
                if (trimmed_part.len > 0) {
                    value = trimmed_part;
                    break;
                }
            }

            const val = value orelse continue;

            if (Rgb.parse(val)) |rgb| {
                if (self.setColorFromKey(key, rgb)) {
                    parsed_count += 1;
                    if (std.mem.eql(u8, key, "foreground")) {
                        found_foreground = true;
                    }
                }
            }
        }

        // If color6 (cyan) was found and we have foreground, set assistant to cyan
        if (found_foreground) {
            // Re-scan for color6 to set assistant_rgb
            lines = std.mem.splitScalar(u8, content, '\n');
            while (lines.next()) |line| {
                const trimmed = std.mem.trim(u8, line, &std.ascii.whitespace);
                if (trimmed.len == 0 or trimmed[0] == '#') continue;

                var parts = std.mem.splitScalar(u8, trimmed, ' ');
                const key = parts.next() orelse continue;

                if (std.mem.eql(u8, key, "color6")) {
                    while (parts.next()) |part| {
                        const trimmed_part = std.mem.trim(u8, part, &std.ascii.whitespace);
                        if (trimmed_part.len > 0) {
                            if (Rgb.parse(trimmed_part)) |rgb| {
                                self.assistant_rgb = rgb;
                            }
                            break;
                        }
                    }
                    break;
                }
            }
        }

        // Apply diff color mappings
        self.applyDiffMappings();

        return parsed_count;
    }

    /// Get the RGB color for a specific element
    pub fn getColor(self: *const Theme, element: ColorschemeElement) Rgb {
        return switch (element) {
            .foreground => self.foreground_rgb,
            .user => self.user_rgb,
            .assistant => self.assistant_rgb,
            .tool => self.status_rgb, // Unified with status
            .err => self.error_rgb,
            .status => self.status_rgb,
            .diff_add => self.diff_add_rgb,
            .diff_remove => self.diff_remove_rgb,
            .diff_header => self.diff_header_rgb,
            .diff_context => self.diff_context_rgb,
            .search => self.search_rgb,
            .todo_accent => self.todo_accent_rgb,
        };
    }

    /// Generate ANSI escape code for a colorscheme element
    pub fn getElementAnsiCode(self: *const Theme, element: ColorschemeElement, buf: []u8) ![]u8 {
        const rgb = self.getColor(element);
        return rgb.toAnsiCode(buf);
    }
};

/// Colorscheme manager to handle the current theme
pub const ColorschemeManager = struct {
    allocator: std.mem.Allocator,
    current_theme: Theme,
    loaded: bool,

    pub fn init(allocator: std.mem.Allocator) ColorschemeManager {
        return .{
            .allocator = allocator,
            .current_theme = Theme{},
            .loaded = false,
        };
    }

    pub fn deinit(self: *ColorschemeManager) void {
        _ = self;
    }

    /// Load theme from a file path
    pub fn loadFromFile(self: *ColorschemeManager, filepath: []const u8) !bool {
        const content = std.fs.cwd().readFileAlloc(self.allocator, filepath, 1024 * 1024) catch |err| {
            std.log.warn("Failed to read theme file {s}: {}", .{ filepath, err });
            return false;
        };
        defer self.allocator.free(content);

        const count = self.current_theme.loadFromBuffer(content);
        self.loaded = count > 0;
        return self.loaded;
    }

    /// Load theme from a buffer
    pub fn loadFromBuffer(self: *ColorschemeManager, content: []const u8) bool {
        const count = self.current_theme.loadFromBuffer(content);
        self.loaded = count > 0;
        return self.loaded;
    }

    /// Check if a theme is loaded
    pub fn isLoaded(self: *const ColorschemeManager) bool {
        return self.loaded;
    }

    /// Get the current theme
    pub fn getTheme(self: *const ColorschemeManager) *const Theme {
        return &self.current_theme;
    }

    /// Get ANSI color code for an element
    pub fn getColorCode(self: *const ColorschemeManager, element: ColorschemeElement, buf: []u8) ![]u8 {
        if (!self.loaded) {
            return error.NoThemeLoaded;
        }
        return self.current_theme.getElementAnsiCode(element, buf);
    }
};

// Default fallback colors (ANSI standard)
pub const FALLBACK = struct {
    pub const USER = "\x1b[32m"; // Green
    pub const ASSISTANT = "\x1b[36m"; // Cyan
    pub const STATUS = "\x1b[33m"; // Yellow
    pub const ERROR = "\x1b[31m"; // Red
    pub const RESET = "\x1b[0m";
};

// Tests
const testing = std.testing;

test "Rgb.parse valid hex colors" {
    const rgb1 = Rgb.parse("#FF5500").?;
    try testing.expectEqual(@as(u8, 255), rgb1.r);
    try testing.expectEqual(@as(u8, 85), rgb1.g);
    try testing.expectEqual(@as(u8, 0), rgb1.b);

    const rgb2 = Rgb.parse("00FF00").?;
    try testing.expectEqual(@as(u8, 0), rgb2.r);
    try testing.expectEqual(@as(u8, 255), rgb2.g);
    try testing.expectEqual(@as(u8, 0), rgb2.b);
}

test "Rgb.parse invalid hex colors" {
    try testing.expect(Rgb.parse("FF55") == null); // Too short
    try testing.expect(Rgb.parse("#GGGGGG") == null); // Invalid hex
    try testing.expect(Rgb.parse("") == null); // Empty
}

test "Rgb.to256ColorIndex" {
    // Pure red should be in RGB cube
    const red = Rgb.init(255, 0, 0);
    const red_idx = red.to256ColorIndex();
    try testing.expect(red_idx >= 16 and red_idx <= 231);

    // Gray should use grayscale range
    const gray = Rgb.init(128, 128, 128);
    const gray_idx = gray.to256ColorIndex();
    try testing.expect(gray_idx >= 232 and gray_idx <= 255);
}

test "Theme.loadFromBuffer" {
    const content =
        "# Test theme\n" ++
        "foreground #FFFFFF\n" ++
        "background #000000\n" ++
        "color1 #FF0000\n" ++
        "color2 #00FF00\n" ++
        "color3 #FFFF00\n" ++
        "color6 #00FFFF\n";

    var theme = Theme{};
    const count = theme.loadFromBuffer(content);

    try testing.expect(count > 0);
    try testing.expectEqual(@as(u8, 255), theme.foreground_rgb.r);
    try testing.expectEqual(@as(u8, 255), theme.foreground_rgb.g);
    try testing.expectEqual(@as(u8, 255), theme.foreground_rgb.b);
}
