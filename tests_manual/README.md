# Manual Provider Configuration Tests

This directory contains tests for real LLM provider configurations that require actual API keys and network access. These tests are **NOT** part of the normal unit test suite and must be run manually.

## Provider Configurations

The following provider configurations are defined in `test_provider_configs.h` and can be tested:

### 1. sonnet-4.5 (Local LM Studio)
- **Provider Type**: `openai`
- **Provider Name**: Local LM Studio
- **Model**: `sonnet-4-5`
- **API Base**: `http://192.168.1.45:8085/v1/chat/completions`
- **Environment Variable**: `SONNET_4_5_API_KEY` (optional, LM Studio may not require auth)
- **Description**: Local LM Studio instance running on your network

### 2. minimax-2.1 (MiniMax)
- **Provider Type**: `anthropic`
- **Provider Name**: MiniMax
- **Model**: `MiniMax-M2.1`
- **API Base**: `https://api.minimax.io/anthropic/v1/messages`
- **Environment Variable**: `MINIMAX_API_KEY` (required)
- **Description**: MiniMax API via Anthropic-compatible endpoint

### 3. kimi-k2.5 (Moonshot AI)
- **Provider Type**: `moonshot`
- **Provider Name**: Moonshot AI
- **Model**: `kimi-k2.5`
- **API Base**: `https://api.moonshot.ai`
- **Environment Variable**: `MOONSHOT_AI_API_KEY` (required)
- **Description**: Moonshot AI's Kimi K2.5 model

### 4. bedrock (AWS Bedrock)
- **Provider Type**: `bedrock`
- **Provider Name**: AWS Bedrock
- **Model**: `us.anthropic.claude-sonnet-4-5-20250929-v1:0` (or override via `ANTHROPIC_MODEL`)
- **Region**: `us-west-2` (or override via `AWS_REGION`)
- **Environment Variables**:
  - `AWS_ACCESS_KEY_ID` (required)
  - `AWS_SECRET_ACCESS_KEY` (required)
  - `AWS_REGION` (optional, defaults to `us-west-2`)
  - `ANTHROPIC_MODEL` (optional)
- **Description**: AWS Bedrock with Claude Sonnet 4.5

## Building

```bash
cd tests_manual
make
```

### Prerequisites

Install required dependencies:

**Debian/Ubuntu:**
```bash
sudo apt install libcjson-dev libcurl4-openssl-dev libsqlite3-dev libbsd-dev
```

**macOS:**
```bash
brew install cjson curl sqlite3 libbsd
```

## Environment Setup

Create a `.env` file with your API keys:

```bash
cp .env.example .env
# Edit .env with your actual API keys
```

Load the environment variables before running tests:

```bash
export $(cat .env | xargs)
```

Or source them directly:

```bash
set -a  # automatically export all variables
source .env
set +a
```

## Running Tests

### List all providers and their configuration status
```bash
./test_provider_configs --list
```

### Test all configured providers
```bash
export MINIMAX_API_KEY="your-key"
export MOONSHOT_AI_API_KEY="your-key"
export AWS_ACCESS_KEY_ID="your-key"
export AWS_SECRET_ACCESS_KEY="your-secret"
./test_provider_configs --test-all
```

### Test individual providers
```bash
./test_provider_configs --sonnet
./test_provider_configs --minimax
./test_provider_configs --kimi
./test_provider_configs --bedrock
```

### Show help
```bash
./test_provider_configs --help
```

## What the Tests Do

For each provider, the tests:

1. **Configuration Check**: Verify that required environment variables are set
2. **Config Loading**: Populate the `LLMProviderConfig` struct with provider settings
3. **Provider Initialization**: Call `provider_init_from_config()` to create the provider
4. **Validation**: Verify that the provider initializes successfully and has valid settings

The tests do **NOT** make actual API calls by default (that would cost money). They only test that the provider configuration is valid and can be initialized.

## Adding New Provider Configurations

To add a new provider configuration:

1. Add a new config function in `test_provider_configs.h`:
```c
static inline void get_my_new_provider_config(LLMProviderConfig *config) {
    memset(config, 0, sizeof(*config));
    config->provider_type = PROVIDER_OPENAI;  // or appropriate type
    strlcpy(config->provider_name, "My Provider", sizeof(config->provider_name));
    strlcpy(config->model, "my-model", sizeof(config->model));
    strlcpy(config->api_base, "https://api.example.com/v1", sizeof(config->api_base));
    strlcpy(config->api_key_env, "MY_API_KEY", sizeof(config->api_key_env));
    
    const char *api_key = getenv("MY_API_KEY");
    if (api_key) {
        strlcpy(config->api_key, api_key, sizeof(config->api_key));
    }
}

static inline int my_new_provider_is_configured(void) {
    return getenv("MY_API_KEY") != NULL;
}

static inline const char* my_new_provider_description(void) {
    return "my-new-provider (My Provider)";
}
```

2. Add a test function in `test_provider_configs.c` following the pattern of existing tests.

3. Add command-line option handling in `main()`.

## Provider Types

The test configurations use these provider types from `src/config.h`:

| Type | Description | Used By |
|------|-------------|---------|
| `openai` | OpenAI-compatible API | sonnet-4.5 (LM Studio) |
| `anthropic` | Anthropic API format | minimax-2.1 |
| `moonshot` | Moonshot/Kimi API (OpenAI-compatible, preserves reasoning_content) | kimi-k2.5 |
| `bedrock` | AWS Bedrock | bedrock |

## File Structure

```
tests_manual/
├── Makefile                    # Build configuration
├── README.md                   # This file
├── .env.example                # Example environment variables
├── .gitignore                  # Ignore secrets and build artifacts
├── test_provider_configs.h     # Provider configuration definitions
└── test_provider_configs.c     # Test implementation
```

## Notes

- These tests are intentionally separate from the normal unit tests because they require external dependencies (API keys, network access, running services).
- The tests will skip providers that aren't configured rather than fail.
- Bedrock tests require valid AWS credentials with permission to call Bedrock.
- The LM Studio provider assumes a local instance is running at `192.168.1.45:8085`.
- API keys are **never** hardcoded in the source files - they are always read from environment variables.
