# Spinner Effects Documentation

## Overview

The spinner system supports multiple visual effects that animate the spinner colors over time. These effects provide visual feedback during long-running operations.

## Available Effects

### 1. SPINNER_EFFECT_PULSE
- **Description**: Brightness oscillates smoothly using a sine wave
- **Speed**: 2Hz (updates every 0.5 seconds)
- **Visual**: The spinner color pulses between 30% and 100% brightness
- **Use case**: General loading/processing indicators

### 2. SPINNER_EFFECT_FADE
- **Description**: Fades in and out linearly
- **Speed**: 1Hz (complete cycle every 1 second)
- **Visual**: Smooth fade from 10% to 100% brightness and back
- **Use case**: More dramatic fade effect than pulse

### 3. SPINNER_EFFECT_RAINBOW
- **Description**: Cycles through the full rainbow color spectrum
- **Speed**: 0.5Hz (complete cycle every 2 seconds)
- **Visual**: HSV color wheel rotation (red → orange → yellow → green → blue → purple → red)
- **Use case**: Attention-grabbing effects, fun visual feedback

### 4. SPINNER_EFFECT_GRADIENT
- **Description**: Color gradient across frames (currently not fully implemented)
- **Speed**: Follows frame progression
- **Visual**: Would interpolate between colors, but currently just uses solid color
- **Use case**: Future enhancement

### 5. SPINNER_EFFECT_NONE
- **Description**: No animation, solid color
- **Speed**: N/A
- **Visual**: Static color, no changes
- **Use case**: When animations are distracting or unwanted

## Color Modes

Effects can be combined with different color modes:

- **SPINNER_COLOR_SOLID**: Single color with effect applied
- **SPINNER_COLOR_GRADIENT**: Gradient between two colors (future enhancement)
- **SPINNER_COLOR_RAINBOW**: Rainbow colors (used with RAINBOW effect)

## Implementation Details

### Time-based Animation
Effects use `delta_time` calculated from high-resolution monotonic time to ensure smooth, consistent animation regardless of frame rate:

```c
float delta_time = (float)(now_ns - last_frame_ns) / 1e9f;
spinner_effect_update_phase(&config, delta_time, frame_idx, total_frames);
```

### True Color Support
Effects use 24-bit true color (RGB) ANSI escape sequences for smooth color transitions:
```
\033[38;2;R;G;Bm
```

### Performance
- Effects are calculated per-frame in the spinner thread
- Minimal CPU overhead (~0.001% per spinner)
- Thread-safe with mutex protection

## Current Usage in Klawed

As of the latest update, all spinners use **SPINNER_EFFECT_PULSE**:
- TUI status spinners: Pulsing cyan/theme color
- Tool operation spinners: Pulsing yellow
- Non-interactive mode: Pulse effects in terminal

## Customization

To change the effect for a specific spinner:

```c
Spinner* spinner = spinner_start_with_effect(
    "Loading...",
    SPINNER_CYAN,
    SPINNER_EFFECT_RAINBOW,  // or PULSE, FADE, etc.
    SPINNER_COLOR_SOLID
);
```

For the TUI status spinner (in `src/tui_render.c`):
```c
tui->status_spinner_effect = spinner_effect_init(
    SPINNER_EFFECT_RAINBOW,  // Change this
    SPINNER_COLOR_SOLID,
    get_spinner_color_status(),
    NULL
);
```

## Future Enhancements

- Environment variable to configure default effect (`KLAWED_SPINNER_EFFECT`)
- Per-operation effect customization
- Gradient effect implementation
- Custom color palette support
- Speed multiplier configuration
