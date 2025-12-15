# Architecture

## System Overview

```
┌─────────────────────────────────────────┐
│         Main Entry Point                │
│  - CLI argument parsing                 │
│  - Environment setup                    │
└─────────────┬───────────────────────────┘
              │
┌─────────────▼───────────────────────────┐
│      Conversation Loop                  │
│  - Message management                   │
│  - API request/response handling        │
└─────────────┬───────────────────────────┘
              │
    ┌─────────┼─────────┐
    │                   │
┌───▼──────────┐  ┌────▼──────────────┐
│  API Client  │  │  Tool Executor    │
│  - libcurl   │  │  - Bash           │
│  - cJSON     │  │  - Read/Write     │
│  - Logging   │  │  - Edit           │
└──────────────┘  │  - Glob/Grep      │
                  └───────────────────┘
              │
    ┌─────────┼─────────┐
    │                   │
┌───▼──────────┐  ┌────▼──────────────┐
│   TUI/Theme  │  │   Persistence     │
│  - Colors    │  │  - API history    │
│  - Config    │  │  - Session data    │
└──────────────┘  └───────────────────┘
```

## Module Structure

```
src/
├── claude.c         # Main entry point and conversation loop
├── commands.c       # Command-line argument parsing
├── completion.c     # Auto-completion system
├── lineedit.c       # Line editing and input handling
├── logger.c         # Logging infrastructure
├── migrations.c     # Data migration utilities
├── persistence.c    # Data persistence and history
└── tui.c           # Terminal UI and theme system
```

## Core Components

### Main Entry Point
- Handles CLI argument parsing
- Sets up environment and configuration
- Initializes logging and persistence systems
- Enters either one-shot or interactive mode

### Conversation Loop
- Manages message history and state
- Handles API request/response cycle
- Coordinates tool execution
- Processes streaming responses (when implemented)

### API Client
- HTTP communication via libcurl
- JSON parsing with cJSON
- Request/response logging
- Error handling and retry logic

### Tool Executor
- Implements 6 core tools: Bash, Read, Write, Edit, Glob, Grep
- Supports advanced features like regex patterns
- Handles parameter validation
- Manages tool execution context

### TUI/Theme System
- Terminal user interface with ncurses
- Kitty-compatible theme loading
- Color management and ANSI escape codes
- Interactive input handling

### Persistence Layer
- API call history storage
- Session data management
- Configuration persistence
- Data migration utilities

## Data Flow

1. **Initialization**: CLI args → Environment setup → System initialization
2. **User Input**: Prompt parsing → Message creation → Context building
3. **API Request**: Message formatting → HTTP request → Response parsing
4. **Tool Execution**: Tool discovery → Parameter validation → Execution → Result collection
5. **Response Processing**: Result formatting → Message addition → Display update
6. **Persistence**: History saving → Session storage → Cache management

## Memory Management

The architecture follows strict memory management patterns:
- RAII-style cleanup with goto patterns
- Clear ownership documentation
- Stack allocation for small buffers
- Comprehensive error handling paths
- Zero-initialization of structures

## Threading Model

Currently single-threaded with infrastructure for future parallel execution:
- pthread support included in dependencies
- Sequential tool execution (current)
- Framework ready for concurrent tool execution
- Non-blocking persistence operations

## Security Considerations

- API key from environment only
- No sandboxing (trusted environments)
- Full file system access via tools
- Input validation and sanitization
- Secure memory handling practices