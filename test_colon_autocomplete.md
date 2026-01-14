# Test Plan: Colon Command Autocomplete

## Feature Description
Tab completion for vim-style `:` commands in TUI mode.

## Available Commands
- `q` / `quit` - Exit interactive mode
- `w` / `write` - Write command (not yet implemented)
- `wq` - Write and quit
- `noh` / `nohlsearch` - Clear search highlight
- `!<cmd>` - Execute shell command
- `re !<cmd>` - Replace input buffer with command output

## Test Cases

### 1. Single Match Completion
**Steps:**
1. Start klawed in TUI mode: `./build/klawed`
2. Press `:` to enter command mode
3. Type `q` and press Tab
4. Expected: Command completes to `:q` (if only 'q' matches) or `:quit` (depending on which is first in list)

### 2. Multiple Match Completion with Common Prefix
**Steps:**
1. Press `:` to enter command mode
2. Type `n` and press Tab
3. Expected: Command completes to `:no` (common prefix of 'noh' and 'nohlsearch')
4. Press Tab again
5. Expected: Status line shows "Available: noh, nohlsearch"

### 3. Partial Match
**Steps:**
1. Press `:` to enter command mode
2. Type `qu` and press Tab
3. Expected: Command completes to `:quit`

### 4. No Match
**Steps:**
1. Press `:` to enter command mode
2. Type `xyz` and press Tab
3. Expected: Beep sound, no completion

### 5. Empty Prefix
**Steps:**
1. Press `:` to enter command mode (showing just `:`)
2. Press Tab
3. Expected: Beep sound, no completion (too many options)

### 6. Complete Match Extension
**Steps:**
1. Press `:` to enter command mode
2. Type `w` and press Tab
3. Expected: Status line shows "Available: w, write, wq"

### 7. Execute Completed Command
**Steps:**
1. Press `:` to enter command mode
2. Type `q` and press Tab to complete
3. Press Enter
4. Expected: Klawed should exit

## Manual Testing Instructions

```bash
# Build the project
make clean && make

# Start klawed in TUI mode
./build/klawed

# Test the autocomplete feature with the test cases above
```

## Notes
- Tab completion only works in command mode (after pressing `:`)
- Multiple presses of Tab on ambiguous prefixes will show available options
- ESC cancels command mode
- Backspace on just `:` exits command mode
