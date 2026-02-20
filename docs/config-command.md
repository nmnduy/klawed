# /config Command - Mid-Session Configuration Switching

The `/config` command allows you to modify configuration settings during an interactive session, including switching LLM providers without restarting.

## Usage

### Basic Syntax
```
/config <setting> <value>
```

### Available Settings

#### `llm_provider`
Switch the LLM provider for the current session and save it to configuration for future sessions.

```
/config llm_provider <provider_name>
```

**Example:**
```
/config llm_provider claude-sonnet
```

This command:
1. Saves `claude-sonnet` as the active provider in `.klawed/config.json`
2. Switches the current session to use the Claude Sonnet provider
3. Updates the model, API URL, and provider instance for the current session

### Viewing Available Providers

Use the `/provider` command to see available providers:
```
/provider list
```

Or just:
```
/provider
```

## How It Works

When you switch providers mid-session:

1. **Configuration Save**: The new provider is saved to `.klawed/config.json` as the `active_provider`
2. **Session Update**: The current session's provider is cleaned up and reinitialized with the new provider configuration
3. **Model Update**: The conversation state's model name is updated
4. **API URL Update**: The API base URL is updated based on the new provider configuration
5. **Provider Instance**: A new provider instance is created (OpenAI, Anthropic, or Bedrock)

## Comparison with `/provider` Command

Both commands can switch providers, but with different focus:

| Command | Primary Purpose | Session Update | Config Save |
|---------|----------------|----------------|-------------|
| `/provider <name>` | Provider management | Yes (with new implementation) | Yes |
| `/config llm_provider <name>` | Configuration management | Yes | Yes |

The `/provider` command is more focused on provider management (listing, switching), while `/config` is designed as a general configuration command that can be extended with more settings in the future.

## Error Handling

- If the provider doesn't exist in configuration, an error is shown
- If provider initialization fails, the configuration is still saved but the session continues with the old provider
- Provider keys are limited to 64 characters (defined by `CONFIG_PROVIDER_KEY_MAX`)
- Maximum of 15 providers is supported (combined from global and local config). If you exceed this limit, extra providers will be silently ignored.

## Environment Variable Override

Note: The `KLAWED_LLM_PROVIDER` environment variable takes precedence over the configuration file setting. If set, it will override any provider selected via `/config` or `/provider` commands.

## Example Session

```
$ ./build/klawed
> /provider
Current LLM Provider: gpt-4
  Type: OpenAI
  Model: gpt-4
  API Base: https://api.openai.com/v1

Available providers:
  gpt-4 - OpenAI GPT-4 (gpt-4)
  claude-sonnet - Anthropic Claude Sonnet (claude-3-5-sonnet-20241022)
  bedrock-claude - AWS Bedrock Claude (us.anthropic.claude-3-5-sonnet-20241022-v2:0)

> /config llm_provider claude-sonnet
Configuration updated:
  llm_provider = claude-sonnet
  Type: Anthropic
  Model: claude-3-5-sonnet-20241022
Configuration saved to .klawed/config.json
Provider switched for current session

> Ask a question...
[Uses Claude Sonnet for response]
```