//! Indicators module
//!
//! Idiomatic Zig replacement for src/indicators.h
//!
//! Provides status indicators and visual feedback elements.

const std = @import("std");

// ---------------------------------------------------------------------------
// Spinner State
// ---------------------------------------------------------------------------

pub const SpinnerState = struct {
    frames: []const []const u8,
    current_frame: usize,
    last_update_ms: u64,
    frame_interval_ms: u64,

    /// Standard spinner frames
    pub const DEFAULT_FRAMES = &[_][]const u8{
        "⠋", "⠙", "⠹", "⠸", "⠼", "⠴", "⠦", "⠧", "⠇", "⠏",
    };

    /// Initialize spinner state
    pub fn init(frames: ?[]const []const u8, interval_ms: u64) SpinnerState {
        return SpinnerState{
            .frames = frames orelse DEFAULT_FRAMES,
            .current_frame = 0,
            .last_update_ms = 0,
            .frame_interval_ms = interval_ms,
        };
    }

    /// Update spinner (call periodically)
    pub fn update(self: *SpinnerState, current_time_ms: u64) void {
        if (current_time_ms - self.last_update_ms >= self.frame_interval_ms) {
            self.current_frame = (self.current_frame + 1) % self.frames.len;
            self.last_update_ms = current_time_ms;
        }
    }

    /// Get current frame
    pub fn getCurrentFrame(self: SpinnerState) []const u8 {
        return self.frames[self.current_frame];
    }

    /// Reset spinner
    pub fn reset(self: *SpinnerState) void {
        self.current_frame = 0;
        self.last_update_ms = 0;
    }
};

// ---------------------------------------------------------------------------
// Status Indicators
// ---------------------------------------------------------------------------

pub const StatusIndicator = struct {
    pub const OK = "✓";
    pub const ERROR = "✗";
    pub const WARNING = "⚠";
    pub const INFO = "ℹ";
    pub const PENDING = "○";
    pub const RUNNING = "◐";
};

// ---------------------------------------------------------------------------
// Progress Bar
// ---------------------------------------------------------------------------

pub const ProgressBar = struct {
    width: usize,
    filled_char: u8,
    empty_char: u8,

    /// Initialize progress bar
    pub fn init(width: usize) ProgressBar {
        return ProgressBar{
            .width = width,
            .filled_char = '█',
            .empty_char = '░',
        };
    }

    /// Render progress bar to buffer
    pub fn render(self: ProgressBar, buf: []u8, percent: f32) []const u8 {
        const filled = @min(
            @as(usize, @intFromFloat(@round(percent * @as(f32, @floatFromInt(self.width))))),
            self.width,
        );

        var i: usize = 0;
        while (i < filled) : (i += 1) {
            buf[i] = self.filled_char;
        }
        while (i < self.width) : (i += 1) {
            buf[i] = self.empty_char;
        }

        return buf[0..self.width];
    }
};

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

test "SpinnerState: frame rotation" {
    var spinner = SpinnerState.init(null, 100);

    try std.testing.expectEqualStrings("⠋", spinner.getCurrentFrame());

    spinner.update(100);
    try std.testing.expectEqualStrings("⠙", spinner.getCurrentFrame());

    spinner.update(200);
    try std.testing.expectEqualStrings("⠹", spinner.getCurrentFrame());
}

test "ProgressBar: rendering" {
    const pb = ProgressBar.init(10);
    var buf: [10]u8 = undefined;

    const result = pb.render(&buf, 0.5);
    try std.testing.expectEqualStrings("█████░░░░░", result);
}
