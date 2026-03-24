# LLM Provider Configuration

Klawed supports configuring LLM providers through the `.klawed/config.json` file. You can configure a single provider or multiple named providers and switch between them easily.

## Configuration Schema

### Single Provider (Legacy Format)

The configuration file supports a legacy single-provider format:

```json
{
  "input_box_style": "horizontal",
  "theme": "tender",
  "llm_provider": {
    "provider_type": "openai",
    "provider_name": "OpenAI",
    "model": "gpt-4",
    "api_base": "https://api.openai.com/v1",
    "api_key": "",
    "use_bedrock": 0
  }
}
```

### Multiple Named Providers (Recommended)

You can define multiple named provider configurations and switch between them:

```json
{
  "input_box_style": "horizontal",
  "theme": "tender",
  "active_provider": "sonnet-4.5-bedrock",
  "providers": {
    "sonnet-4.5-bedrock": {
      "provider_type": "bedrock",
      "provider_name": "AWS Bedrock",
      "model": "us.anthropic.claude-3-5-sonnet-20241022-v2:0",
      "use_bedrock": 1
    },
    "opus-4.5": {
      "provider_type": "anthropic",
      "provider_name": "Anthropic",
      "model": "claude-3-5-sonnet-20241022",
      "api_base": "https://api.anthropic.com/v1"
    },
    "gpt-4-turbo": {
      "provider_type": "openai",
      "provider_name": "OpenAI",
      "model": "gpt-4-turbo",
      "api_base": "https://api.openai.com/v1"
    }
  }
}
```

### Fields

#### Provider Configuration Fields

- **provider_type**: The type of provider to use. Can be:
  - `"auto"` (default): Auto-detect based on URL patterns and environment variables
  - `"openai"`: OpenAI-compatible API (requires `OPENAI_API_KEY`)
  - `"openai_sub"` (or `"chatgpt"`): OpenAI Subscription (OAuth device flow — no API key needed, uses ChatGPT Plus/Pro account)
  - `"anthropic"`: Anthropic API
  - `"bedrock"`: AWS Bedrock
  - `"kimi_coding_plan"`: Kimi Coding Plan (OAuth device flow, requires browser auth)
  - `"moonshot"`: Moonshot/Kimi API (OpenAI-compatible, for kimi-k2.5, etc.)
  - `"custom"`: Custom provider (defaults to OpenAI-compatible)

- **provider_name**: Display name for the provider (optional)

- **model**: The model name to use (e.g., "gpt-4", "claude-3-5-sonnet")

- **api_base**: Base URL for the API (e.g., "https://api.openai.com/v1")

- **api_key**: API key (optional, prefer environment variable for security)

- **api_key_env**: Name of environment variable to read API key from (e.g., "DEEPSEEK_API_KEY"). This allows specifying which environment variable to use without hardcoding the key itself. Priority: `OPENAI_API_KEY` env var > `api_key_env` > `api_key`

- **use_bedrock**: Legacy flag for AWS Bedrock (0 or 1)

#### Multiple Provider Fields

- **active_provider**: The key of the provider to use by default (e.g., "sonnet-4.5-bedrock")

- **providers**: An object containing named provider configurations. Each key is a provider name, and the value is a provider configuration object with the fields above. Maximum of **15 providers** is supported (combined from global and local config files). If you exceed this limit, a warning will be logged and extra providers will be silently ignored.

## Configuration File Locations

Klawed supports two configuration file locations that are merged together:

1. **Global configuration**: `~/.klawed/config.json` - User-wide settings shared across all projects
2. **Local configuration**: `.klawed/config.json` - Project-specific settings that override global settings

### Merging Behavior

When both files exist, they are merged with the following rules:

- **Simple values** (theme, input_box_style, active_provider, llm_provider): Local values override global values
- **Named providers**: Providers from both files are combined. If a provider with the same key exists in both files, the local version completely overrides the global one

### Example Usage

**Global config** (`~/.klawed/config.json`) - Define commonly used providers:
```json
{
  "theme": "tender",
  "providers": {
    "sonnet-4.5-bedrock": {
      "provider_type": "bedrock",
      "model": "us.anthropic.claude-3-5-sonnet-20241022-v2:0"
    },
    "gpt-4-turbo": {
      "provider_type": "openai",
      "model": "gpt-4-turbo"
    }
  }
}
```

**Local config** (`.klawed/config.json`) - Set project-specific settings:
```json
{
  "active_provider": "sonnet-4.5-bedrock"
}
```

The result: All providers from global are available, with `sonnet-4.5-bedrock` selected as active.

## Priority Order

Configuration priority depends on whether a **named provider is explicitly selected** (via `active_provider` or `KLAWED_LLM_PROVIDER`).

### Named Provider Mode (Recommended)

When a named provider is selected, the provider's configuration takes precedence:

1. **Model**: provider config `model` > `OPENAI_MODEL` environment variable
2. **API Base**: provider config `api_base` > `OPENAI_API_BASE` environment variable
3. **API Key**: provider config `api_key_env` > provider config `api_key` > `OPENAI_API_KEY` environment variable

This allows you to switch providers with `/provider` command or `KLAWED_LLM_PROVIDER` without environment variables interfering.

### Legacy Mode (No Named Provider)

When no named provider is selected, environment variables take precedence for backward compatibility:

1. **Model**: `OPENAI_MODEL` environment variable > legacy config `model`
2. **API Base**: `OPENAI_API_BASE` environment variable > legacy config `api_base`
3. **API Key**: `OPENAI_API_KEY` environment variable > legacy config `api_key_env` > legacy config `api_key`

### Provider Selection Priority

Which provider configuration to use is determined by:

1. `KLAWED_LLM_PROVIDER` environment variable (highest priority)
2. `active_provider` in config file
3. Legacy `llm_provider` configuration (if no named providers)

### API Key Priority (Named Provider Mode)

When using named providers, API keys are resolved in this order:

1. `api_key_env` from provider config (reads from the specified environment variable)
2. `api_key` from provider config (not recommended for security)
3. `OPENAI_API_KEY` environment variable (fallback)

**Example:**
```json
{
  "providers": {
    "my-provider": {
      "api_key_env": "MY_API_KEY",
      "api_key": "hardcoded-fallback"
    }
  }
}
```

The system will:
1. First check if `MY_API_KEY` environment variable is set (uses it if found)
2. Then fall back to `hardcoded-fallback` (not recommended)
3. Finally check `OPENAI_API_KEY` as a last resort

### Environment Variables

- `KLAWED_LLM_PROVIDER` - Selects which named provider to use (e.g., "sonnet-4.5-bedrock")
- `KLAWED_USE_BEDROCK` - Overrides `use_bedrock` (always respected)
- `OPENAI_MODEL` - Model fallback (only used if provider config has no model)
- `OPENAI_API_KEY` - API key fallback (only used if provider config has no key)
- `OPENAI_API_BASE` - API base fallback (only used if provider config has no api_base)

## Examples

### Single Provider Examples

#### OpenAI Configuration
```json
{
  "llm_provider": {
    "provider_type": "openai",
    "model": "gpt-4",
    "api_base": "https://api.openai.com/v1"
  }
}
```

#### Anthropic Configuration
```json
{
  "llm_provider": {
    "provider_type": "anthropic",
    "model": "claude-3-5-sonnet-20241022",
    "api_base": "https://api.anthropic.com/v1"
  }
}
```

#### Local OpenAI-compatible API
```json
{
  "llm_provider": {
    "provider_type": "openai",
    "model": "local-model",
    "api_base": "http://localhost:8080/v1"
  }
}
```

#### AWS Bedrock Configuration
```json
{
  "llm_provider": {
    "provider_type": "bedrock",
    "model": "us.anthropic.claude-3-5-sonnet-20241022-v2:0"
  }
}
```

#### Kimi Coding Plan Configuration
```json
{
  "llm_provider": {
    "provider_type": "kimi_coding_plan",
    "model": "kimi-for-coding"
  }
}
```

**Note:** Kimi Coding Plan uses OAuth device flow for authentication. On first use, you'll be prompted to authorize via browser. The OAuth token will be stored in `~/.kimi/credentials/` for subsequent use.

**Headless/Server Usage:** To use Kimi on a machine without a browser (e.g., servers), you can copy the authentication from a machine that has completed the browser flow:

1. **Option A - Using `KIMI_CONFIG_DIR`:**
   ```bash
   # On machine with browser
   export KIMI_CONFIG_DIR="$HOME/.kimi-shared"
   klawed  # Complete browser auth
   
   # Copy to headless server
   scp -r ~/.kimi-shared user@server:~/
   
   # On headless server
   export KIMI_CONFIG_DIR="$HOME/.kimi-shared"
   klawed  # Works without browser
   ```

2. **Option B - Copy auth files directly:**
   ```bash
   # Copy these two files from the authenticated machine:
   # - ~/.kimi/device_id
   # - ~/.kimi/credentials/oauth_token.json
   
   # On headless server:
   mkdir -p ~/.kimi/credentials
   # Copy both files to the same locations
   ```

**Important:** You must copy **both** `device_id` AND `oauth_token.json`. The device_id is tied to the OAuth token and is required for API requests.

#### Moonshot/Kimi API Configuration (for kimi-k2.5)
```json
{
  "llm_provider": {
    "provider_type": "moonshot",
    "model": "kimi-k2.5",
    "api_base": "https://api.moonshot.ai",
    "api_key_env": "MOONSHOT_AI_API_KEY"
  }
}
```

#### Using api_key_env for Multiple API Keys
```json
{
  "providers": {
    "deepseek": {
      "provider_type": "openai",
      "model": "deepseek-chat",
      "api_base": "https://api.deepseek.com",
      "api_key_env": "DEEPSEEK_API_KEY"
    },
    "openrouter-gemini": {
      "provider_type": "openai",
      "model": "google/gemini-3-pro-preview",
      "api_base": "https://openrouter.ai/api",
      "api_key_env": "OPENROUTER_API_KEY"
    }
  }
}
```

With this configuration, you can set different API keys in your environment:
```bash
export DEEPSEEK_API_KEY="your-deepseek-key"
export OPENROUTER_API_KEY="your-openrouter-key"

# Switch between providers without changing environment variables
KLAWED_LLM_PROVIDER=deepseek klawed "Hello"
KLAWED_LLM_PROVIDER=openrouter-gemini klawed "Hello"
```

### Multiple Provider Examples

#### Switching Between Providers
```json
{
  "active_provider": "sonnet-4.5-bedrock",
  "providers": {
    "sonnet-4.5-bedrock": {
      "provider_type": "bedrock",
      "provider_name": "AWS Bedrock",
      "model": "us.anthropic.claude-3-5-sonnet-20241022-v2:0",
      "use_bedrock": 1
    },
    "opus-4.5": {
      "provider_type": "anthropic",
      "provider_name": "Anthropic",
      "model": "claude-3-5-sonnet-20241022",
      "api_base": "https://api.anthropic.com/v1"
    },
    "gpt-4-turbo": {
      "provider_type": "openai",
      "provider_name": "OpenAI",
      "model": "gpt-4-turbo",
      "api_base": "https://api.openai.com/v1"
    }
  }
}
```

#### Using Environment Variables to Select Provider
```bash
# Use the Bedrock provider
export KLAWED_LLM_PROVIDER="sonnet-4.5-bedrock"
./klawed "Hello, world!"

# Switch to OpenAI provider
export KLAWED_LLM_PROVIDER="gpt-4-turbo"
./klawed "Hello, world!"
```

## Security Considerations

1. **API Keys**: It's recommended to use environment variables (`OPENAI_API_KEY`) or `api_key_env` for API keys rather than storing them directly in the config file. The config file will warn if an API key is stored in it.

2. **api_key_env field**: Use this to specify which environment variable contains your API key, allowing you to keep keys out of config files while being explicit about which variable to use for each provider.

3. **File Permissions**: Ensure the `.klawed/config.json` file has appropriate permissions (e.g., `chmod 600 .klawed/config.json`).

4. **Git Ignore**: The `.klawed/` directory should be in your `.gitignore` file to prevent accidentally committing configuration files.

## Migration from Single to Multiple Providers

If you have an existing single-provider configuration, you can migrate to multiple providers:

1. Rename your existing `llm_provider` configuration to a named provider:
   ```json
   {
     "active_provider": "my-default",
     "providers": {
       "my-default": {
         // Copy your existing llm_provider configuration here
         "provider_type": "openai",
         "model": "gpt-4",
         "api_base": "https://api.openai.com/v1"
       }
     }
   }
   ```

2. You can keep the legacy `llm_provider` field for backward compatibility while transitioning.

3. Add additional providers as needed.

## Migration from Environment Variables

If you're currently using environment variables, you can gradually migrate to the config file:

1. Start by adding non-sensitive settings (provider_type, model, api_base) to the config file
2. Keep API keys in environment variables
3. Eventually move all settings to the config file if desired

## Backward Compatibility

The multiple provider configuration system is fully backward compatible:

1. If no configuration file exists, Klawed falls back to environment variables and defaults
2. If only the legacy `llm_provider` field exists, it will be used
3. If both `llm_provider` and named providers exist, named providers take precedence when selected
4. The legacy `KLAWED_USE_BEDROCK` environment variable is still supported and will override the config file's `use_bedrock` setting
5. **Important change**: When a named provider is selected, the provider's configuration takes precedence over environment variables (`OPENAI_MODEL`, `OPENAI_API_BASE`). This allows the `/provider` command to work correctly without environment variable interference. Environment variables are only used as fallbacks when the provider config doesn't specify a value.