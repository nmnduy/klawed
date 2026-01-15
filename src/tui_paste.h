/*
 * TUI Paste Detection & Handling
 *
 * Manages paste mode detection and content handling:
 * - Bracketed paste mode
 * - Heuristic paste detection (rapid input)
 * - Paste content buffering
 * - Placeholder insertion for large pastes
 * - Paste timeout detection
 */

#ifndef TUI_PASTE_H
#define TUI_PASTE_H

#include "tui_input.h"

// Forward declarations
typedef struct TUIStateStruct TUIState;

// Paste placeholder threshold (characters)
// Small pastes inserted directly, large pastes use placeholder
#define TUI_PASTE_PLACEHOLDER_THRESHOLD 200

// Insert paste content or placeholder into visible buffer
// Called when paste mode ends to finalize the paste
void tui_paste_finalize(TUIInputBuffer *input);

// Check if paste timeout has elapsed and finalize if needed
// prompt: prompt string for redraw after paste finalization
// Returns 1 if paste was finalized, 0 otherwise
int tui_paste_check_timeout(TUIState *tui, const char *prompt);

// Start paste mode (called when bracketed paste sequence detected)
void tui_paste_start_mode(TUIInputBuffer *input);

// End paste mode and finalize content
void tui_paste_end_mode(TUIInputBuffer *input);

// Check if input buffer is in paste mode
int tui_paste_is_active(const TUIInputBuffer *input);

// Get paste detection configuration from environment
// Sets global paste detection parameters
void tui_paste_init_config(void);

// Check if paste detection heuristic is enabled
int tui_paste_heuristic_enabled(void);

// Get paste detection parameters (gap_ms, burst_min, timeout_ms)
void tui_paste_get_params(int *gap_ms, int *burst_min, int *timeout_ms);

// Update paste detection timing (called for each input character)
// Returns: 1 if paste mode was just entered, 0 otherwise
int tui_paste_update_timing(TUIInputBuffer *input);

// Accumulate character in paste buffer (called during paste mode)
// Returns: 0 on success, -1 on error
int tui_paste_accumulate_char(TUIInputBuffer *input, const unsigned char *utf8_char, int char_bytes);

// Reset paste state (called on input buffer clear)
void tui_paste_reset(TUIInputBuffer *input);

#endif // TUI_PASTE_H
