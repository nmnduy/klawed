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
  - `"openai"`: OpenAI-compatible API
  - `"anthropic"`: Anthropic API
  - `"bedrock"`: AWS Bedrock
  - `"custom"`: Custom provider (defaults to OpenAI-compatible)

- **provider_name**: Display name for the provider (optional)

- **model**: The model name to use (e.g., "gpt-4", "claude-3-5-sonnet")

- **api_base**: Base URL for the API (e.g., "https://api.openai.com/v1")

- **api_key**: API key (optional, prefer environment variable for security)

- **use_bedrock**: Legacy flag for AWS Bedrock (0 or 1)

#### Multiple Provider Fields

- **active_provider**: The key of the provider to use by default (e.g., "sonnet-4.5-bedrock")

- **providers**: An object containing named provider configurations. Each key is a provider name, and the value is a provider configuration object with the fields above.

## Priority Order

Configuration values are resolved in this priority order (highest to lowest):

1. Command-line arguments (if supported)
2. Environment variables
3. Configuration file (`.klawed/config.json`)
   - If `KLAWED_LLM_PROVIDER` environment variable is set, use that named provider
   - Otherwise, if `active_provider` is set in config, use that named provider
   - Otherwise, use the legacy `llm_provider` configuration
4. Default values

### Environment Variables That Override Config

- `OPENAI_MODEL` - Overrides `model`
- `OPENAI_API_KEY` - Overrides `api_key` (recommended for security)
- `OPENAI_API_BASE` - Overrides `api_base`
- `KLAWED_USE_BEDROCK` - Overrides `use_bedrock`
- `KLAWED_LLM_PROVIDER` - Selects which named provider to use (e.g., "sonnet-4.5-bedrock")

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

1. **API Keys**: It's recommended to use environment variables (`OPENAI_API_KEY`) for API keys rather than storing them in the config file. The config file will warn if an API key is stored in it.

2. **File Permissions**: Ensure the `.klawed/config.json` file has appropriate permissions (e.g., `chmod 600 .klawed/config.json`).

3. **Git Ignore**: The `.klawed/` directory should be in your `.gitignore` file to prevent accidentally committing configuration files.

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
5. Environment variables (`OPENAI_MODEL`, `OPENAI_API_KEY`, etc.) still override configuration file values