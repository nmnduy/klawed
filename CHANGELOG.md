# Changelog

## v0.1.11

- Added custom headers support for OpenAI provider
- Added logging headers for better debugging
- Added libbsd integration for safer string operations
- Added session dump and conversation printing
- Added diff display for new files
- Added scroll controls (Ctrl+D, Ctrl+U)
- Added input height expansion up to 20% of screen
- Added format one-shot mode
- Added tool results validation
- Added cache control improvements
- Fixex SQLite database locked
- Fixed token usage parsing for different providers
- Fixed resize segfault issues
- Fixed TUI segfault problems

## v0.5.0 (2026-01-05)

### Major Features
- **Version bump**: Initial release of v0.5.0 series

## v0.5.1 (2026-01-05)

### Token Management
- **Token count statistics**: Token usage now shows statistics solely from last token usage record
- **Total token display**: Token count now shows total tokens (prompt + completion) instead of just prompt tokens

### Normal Mode
- **Command execution**: Users can run commands in normal mode with ':' prefix
  - Enhanced command parsing in `commands.c`
  - Added TUI support for command mode interface

## v0.5.2 (2026-01-05)

### Build System
- **Makefile updates**: Improved build configuration and dependencies

### Process Management
- **Bash tool refactor**: Completely refactored bash tool with robust process management
  - Moved process handling to dedicated `process_utils.c` module
  - Improved error handling and resource cleanup
  - Better signal handling and timeout management

### Code Quality
- **Build fixes**: Fixed various build issues and compilation warnings
- **Documentation**: Updated KLAWED.md with current project information

### libbsd Integration
- **Revert and restore**: Fixed libbsd compatibility issues
  - Temporarily reverted libbsd integration to fix build
  - Restored with proper compatibility layer in `compat.h`
  - Safer string operations with `strlcpy`, `strlcat`, `strnlen`

## v0.5.3 (2026-01-05)

### User Interface
- **Search highlighting**: Added text highlighting for search results in TUI
- **Git conventions**: Added git commit conventions documentation to KLAWED.md

## v0.5.4 (2026-01-05)

### User Interface
- **Bash output visibility**: Users can now see bash command output directly in the interface

## v0.5.5 (2026-01-05)

### Code Quality
- **Whitespace cleanup**: Applied consistent code formatting across multiple files

## v0.5.6 (2026-01-05)

### Bug Fixes
- **Segfault fixes**: Fixed segmentation faults in various components
- **Command processing**: Improved handling of vim-style commands starting with ':'

### Code Quality
- **Whitespace formatting**: Applied consistent whitespace formatting across codebase

## v0.6.0 (2026-01-05)

### User Interface
- **History search**: Added comprehensive history search functionality with fuzzy matching
- **Search shortcuts**: Added keyboard shortcuts for history search navigation
- **Mascot display**: Fixed issue where mascot would disappear after screen clear
- **Search ordering**: Improved ordering of history search results for better relevance

### Normal Mode
- **Vim-style commands**: Input starting with ':' is now treated as vim command in normal mode
- **Segfault fixes**: Fixed segmentation faults in TUI and command processing

## v0.6.1 (2026-01-05)

### Build System
- **Makefile improvements**: Updated build configuration for better compatibility