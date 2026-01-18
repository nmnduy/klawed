# LLM Provider Configuration

Klawed now supports configuring LLM providers through the `.klawed/config.json` file. This allows you to specify your preferred provider, model, API base URL, and other settings without relying solely on environment variables.

## Configuration Schema

The configuration file supports the following structure for LLM providers:

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

### Fields

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

## Priority Order

Configuration values are resolved in this priority order (highest to lowest):

1. Command-line arguments (if supported)
2. Environment variables
3. Configuration file (`.klawed/config.json`)
4. Default values

### Environment Variables That Override Config

- `OPENAI_MODEL` - Overrides `model`
- `OPENAI_API_KEY` - Overrides `api_key` (recommended for security)
- `OPENAI_API_BASE` - Overrides `api_base`
- `KLAWED_USE_BEDROCK` - Overrides `use_bedrock`

## Examples

### OpenAI Configuration
```json
{
  "llm_provider": {
    "provider_type": "openai",
    "model": "gpt-4",
    "api_base": "https://api.openai.com/v1"
  }
}
```

### Anthropic Configuration
```json
{
  "llm_provider": {
    "provider_type": "anthropic",
    "model": "claude-3-5-sonnet-20241022",
    "api_base": "https://api.anthropic.com/v1"
  }
}
```

### Local OpenAI-compatible API
```json
{
  "llm_provider": {
    "provider_type": "openai",
    "model": "local-model",
    "api_base": "http://localhost:8080/v1"
  }
}
```

### AWS Bedrock Configuration
```json
{
  "llm_provider": {
    "provider_type": "bedrock",
    "model": "us.anthropic.claude-3-5-sonnet-20241022-v2:0"
  }
}
```

## Security Considerations

1. **API Keys**: It's recommended to use environment variables (`OPENAI_API_KEY`) for API keys rather than storing them in the config file. The config file will warn if an API key is stored in it.

2. **File Permissions**: Ensure the `.klawed/config.json` file has appropriate permissions (e.g., `chmod 600 .klawed/config.json`).

3. **Git Ignore**: The `.klawed/` directory should be in your `.gitignore` file to prevent accidentally committing configuration files.

## Migration from Environment Variables

If you're currently using environment variables, you can gradually migrate to the config file:

1. Start by adding non-sensitive settings (provider_type, model, api_base) to the config file
2. Keep API keys in environment variables
3. Eventually move all settings to the config file if desired

## Backward Compatibility

The new configuration system is fully backward compatible. If no configuration file exists or if fields are missing, Klawed will fall back to:
1. Environment variables
2. Default values

The legacy `KLAWED_USE_BEDROCK` environment variable is still supported and will override the config file's `use_bedrock` setting.