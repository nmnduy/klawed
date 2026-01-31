# Manual Provider Tests

This directory contains tests for real LLM provider configurations that require actual API keys and network access. These tests are **NOT** part of the normal unit test suite and must be run manually.

## Quick Start

```bash
cd tests_manual
make

# List configured providers from ~/.klawed/config.json
./test_provider_api_calls --list-providers

# Test with the active provider from config
./test_provider_api_calls --test-read --verbose

# Test a specific provider
./test_provider_api_calls --provider sonnet-4.5 --test-all
```

## Two Types of Tests

### 1. Configuration Tests (`test_provider_configs`)

Validates provider configuration loading and initialization **without making API calls**.

### 2. API Call Tests (`test_provider_api_calls`)

Makes **real API calls** to providers from your `~/.klawed/config.json` and validates tool handling.

---

## Configuration Tests

These tests validate the provider configurations in `~/.klawed/config.json` without making API calls.

```bash
# List all providers from config
./test_provider_configs --list

# Test all configured providers
./test_provider_configs --test-all

# Test specific providers
./test_provider_configs --sonnet
./test_provider_configs --minimax
./test_provider_configs --kimi
./test_provider_configs --bedrock
```

---

## API Call Tests

These tests make **real API calls** using providers from `~/.klawed/config.json`.

### ⚠️ Important Warnings

1. **These tests cost money!** Each API call incurs charges from the provider.
2. **Tests use read-only tools only** - no files will be modified.
3. **Tests may be slow** - API latency varies.

### Safe Test Queries

| Test | Query | Expected Tool |
|------|-------|---------------|
| `--test-read` | "Read README.md and summarize" | `Read` |
| `--test-glob` | "List C files in src/" | `Glob` |
| `--test-grep` | "Search for TODO comments" | `Grep` |
| `--test-bash` | "Check git status" | `Bash` |
| `--test-multi` | "Find test files and read one" | `Glob`, `Read` |

### Using Your Config

The tests automatically read `~/.klawed/config.json`:

```bash
# Use the active_provider from config
./test_provider_api_calls --test-all

# Override with a specific provider
./test_provider_api_calls --provider sonnet-4.5 --test-all

# Override with environment variable
KLAWED_LLM_PROVIDER=minimax-2.1 ./test_provider_api_calls --test-read
```

### Example Output

```bash
$ ./test_provider_api_calls --list-providers

Configured Providers:
====================

opus-4.5
  Type: openai
  Name: Local LM Studio
  Model: opus-4-5
  API Base: http://192.168.1.45:8085/v1/chat/completions

sonnet-4.5
  Type: openai
  Name: Local LM Studio
  Model: sonnet-4-5
  API Base: http://192.168.1.45:8085/v1/chat/completions

minimax-2.1
  Type: anthropic
  Name: MiniMax
  Model: MiniMax-M2.1
  API Base: https://api.minimax.io/anthropic/v1/messages
  API Key Env: MINIMAX_API_KEY (set)

kimi-k2.5
  Type: moonshot
  Name: Moonshot AI
  Model: kimi-k2.5
  API Base: https://api.moonshot.ai
  API Key Env: MOONSHOT_AI_API_KEY (set)

gpt-5.2-codex [ACTIVE]
  Type: openai
  Name: Local LM Studio
  Model: gpt-5.2-codex
  API Base: http://192.168.1.45:8085/v1/chat/completions

$ ./test_provider_api_calls --provider sonnet-4.5 --test-read
=== Provider API Call Tests ===
Testing real API calls with tool validation
WARNING: These tests make ACTUAL API calls and may incur costs!

Using provider: sonnet-4.5
  Type: openai
  Model: sonnet-4-5
  API Base: http://192.168.1.45:8085/v1/chat/completions

Running 1 test(s)...

  Running test: read
  Query: Read the README.md file and tell me what this project is about in one sentence.
  Expected tools: Read
  Tools used: Read
  Response time: 1245 ms
  Response: This project is Klawed, a C-based coding assistant...

╔══════════════════════════════════════════════════════════════════╗
║                      TEST SUMMARY                                ║
╠══════════════════════════════════════════════════════════════════╣
║ Total tests:    1                                                ║
║ Passed:         1                                                ║
║ Failed:         0                                                ║
╠══════════════════════════════════════════════════════════════════╣
║ PASS   | read                 | 1245 ms | Read                   ║
╚══════════════════════════════════════════════════════════════════╝
```

### Security

The Bash tool is restricted to read-only commands:
- ✅ Allowed: `ls`, `cat`, `pwd`, `echo`, `git status`, `git log`, `git branch`, `wc`, `head`, `tail`, `find`, `grep`
- ❌ Blocked: Any write/modify commands

---

## Config File Format

The tests read from `~/.klawed/config.json`:

```json
{
    "active_provider": "gpt-5.2-codex",
    "providers": {
        "sonnet-4.5": {
            "provider_type": "openai",
            "provider_name": "Local LM Studio",
            "model": "sonnet-4-5",
            "api_base": "http://192.168.1.45:8085/v1/chat/completions"
        },
        "minimax-2.1": {
            "provider_type": "anthropic",
            "provider_name": "MiniMax",
            "model": "MiniMax-M2.1",
            "api_base": "https://api.minimax.io/anthropic/v1/messages",
            "api_key_env": "MINIMAX_API_KEY"
        },
        "kimi-k2.5": {
            "provider_type": "moonshot",
            "provider_name": "Moonshot AI",
            "model": "kimi-k2.5",
            "api_base": "https://api.moonshot.ai",
            "api_key_env": "MOONSHOT_AI_API_KEY"
        }
    }
}
```

---

## Prerequisites

**Debian/Ubuntu:**
```bash
sudo apt install libcjson-dev libcurl4-openssl-dev libbsd-dev
```

**macOS:**
```bash
brew install cjson curl libbsd
```

---

## File Structure

```
tests_manual/
├── Makefile                       # Build configuration
├── README.md                      # This file
├── .env.example                   # Example environment variables
├── test_provider_configs.h        # Provider configuration definitions
├── test_provider_configs.c        # Configuration test implementation
└── test_provider_api_calls.c      # API call test implementation (uses ~/.klawed/config.json)
```

---

## Provider Selection Priority

The tests use this priority order to select a provider:

1. `--provider <name>` command line argument
2. `KLAWED_LLM_PROVIDER` environment variable
3. `active_provider` from `~/.klawed/config.json`
4. Default (first provider in config)

---

## Notes

- **API keys** are read from environment variables (never from config file)
- The config file `~/.klawed/config.json` is shared with the main klawed application
- Tests will show `[ACTIVE]` next to the active provider from config
- API call tests require a valid API key for the selected provider
