# Provider Configuration via Environment Variables

Klawed supports configuring LLM providers entirely through environment variables, without needing a `config.json` file. This is especially useful for OAuth-based providers that don't require API keys.

## Quick Reference

| Provider Type | `KLAWED_PROVIDER_TYPE` | API Key Env Var | Default Model |
|--------------|------------------------|-----------------|---------------|
| OpenAI Subscription (ChatGPT Plus/Pro) | `openai_sub` | None (OAuth) | `gpt-5.3-codex` |
| Kimi Coding Plan | `kimi_coding_plan` | None (OAuth) | `kimi-for-coding` |
| Z.AI GLM Coding Plan | `zai_coding` | `ZAI_API_KEY_CODING_PLAN` | `glm-4-flash` |
| Anthropic Subscription (Claude.ai) | `anthropic_sub` | None (OAuth) | `claude-opus-4` |

## Common Environment Variables

- `KLAWED_PROVIDER_TYPE` - Select the provider type (see table above)
- `OPENAI_MODEL` - Override the default model name for any provider
- `OPENAI_API_BASE` - Custom API base URL (for compatible endpoints)

**Note on `OPENAI_API_BASE`:** This variable works with `openai_sub`, `anthropic_sub`, and `zai_coding` providers, but **does not affect `kimi_coding_plan`** (which uses a fixed endpoint).

## OAuth Providers (No API Key Required)

### OpenAI Subscription (ChatGPT Plus/Pro)

Uses OAuth device flow for authentication. Tokens are stored in `~/.openai/auth.json` by default.

```bash
export KLAWED_PROVIDER_TYPE=openai_sub
# Optional: use a custom OAuth token location
export OPENAI_OAUTH_PATH=/path/to/custom/auth.json
# Optional: override default model
export OPENAI_MODEL=gpt-4o

klawed "your coding task here"
```

**OAuth Token Location:**
- Default: `~/.openai/auth.json`
- Custom: Set `OPENAI_OAUTH_PATH` to use a different location

This is useful for:
- Sharing OAuth credentials across multiple machines (point to a shared/synced directory)
- Using a more secure location for token storage
- Running in containerized environments with mounted volumes

### Kimi Coding Plan

Uses OAuth device flow for authentication. Tokens are stored in `~/.kimi/oauth_token.json` by default (controlled by `KIMI_CONFIG_DIR` environment variable).

```bash
export KLAWED_PROVIDER_TYPE=kimi_coding_plan
# Optional: use a custom config directory for OAuth tokens
export KIMI_CONFIG_DIR=/path/to/custom/kimi/config
# Optional: override default model
export OPENAI_MODEL=kimi-custom-model

klawed "your coding task here"
```

**OAuth Token Location:**
- Default: `~/.kimi/oauth_token.json`
- Custom: Set `KIMI_CONFIG_DIR` to change the directory (the filename remains `oauth_token.json`)

### Anthropic Subscription (Claude.ai)

Uses OAuth device flow for authentication.

```bash
export KLAWED_PROVIDER_TYPE=anthropic_sub
# Optional: override default model
export OPENAI_MODEL=claude-sonnet-4

klawed "your coding task here"
```

## API Key Providers

### Z.AI GLM Coding Plan

Requires an API key from Z.AI.

```bash
export KLAWED_PROVIDER_TYPE=zai_coding
export ZAI_API_KEY_CODING_PLAN="your-api-key-here"
# Optional: override default model
export OPENAI_MODEL=glm-4-plus

klawed "your coding task here"
```

**Getting an API Key:**
1. Visit [Z.AI](https://z.ai) and create an account
2. Navigate to the API/coding plan section
3. Generate an API key
4. Set it as `ZAI_API_KEY_CODING_PLAN`

## Advanced Usage

### Multiple Provider Switching

You can create shell aliases or functions to quickly switch between providers:

```bash
# ~/.bashrc or ~/.zshrc

alias klawed-openai='unset KLAWED_PROVIDER_TYPE; export OPENAI_API_KEY=your-key; klawed'
alias klawed-chatgpt='export KLAWED_PROVIDER_TYPE=openai_sub; unset OPENAI_API_KEY; klawed'
alias klawed-kimi='export KLAWED_PROVIDER_TYPE=kimi_coding_plan; unset OPENAI_API_KEY; klawed'
alias klawed-zai='export KLAWED_PROVIDER_TYPE=zai_coding; export ZAI_API_KEY_CODING_PLAN=your-key; klawed'
```

### Sharing OAuth Credentials Across Machines

For OAuth providers, you can sync the token file across machines:

```bash
# On machine 1 (authenticate)
export KLAWED_PROVIDER_TYPE=openai_sub
export OPENAI_OAUTH_PATH=~/Dropbox/openai/auth.json
klawed "test"

# On machine 2 (use the same credentials)
export KLAWED_PROVIDER_TYPE=openai_sub
export OPENAI_OAUTH_PATH=~/Dropbox/openai/auth.json
klawed "test"  # No re-authentication needed!
```

**Note:** The OAuth token file contains sensitive credentials. Ensure your sync method is secure and the file permissions are restrictive (0600).

### Docker/Container Usage

```bash
# For OAuth providers (mount the OAuth directory)
docker run -e KLAWED_PROVIDER_TYPE=openai_sub \
           -e OPENAI_OAUTH_PATH=/config/auth.json \
           -v ~/.openai:/config:ro \
           klawed "your prompt"

# For API key providers
docker run -e KLAWED_PROVIDER_TYPE=zai_coding \
           -e ZAI_API_KEY_CODING_PLAN="your-key" \
           klawed "your prompt"
```

## Troubleshooting

### OAuth Authentication Issues

If you see authentication errors with OAuth providers:

1. **Check token file exists:**
   ```bash
   ls -la ~/.openai/auth.json  # For openai_sub
   ls -la ~/.kimi/oauth_token.json  # For kimi_coding_plan
   ```

2. **Re-authenticate by deleting the token file:**
   ```bash
   rm ~/.openai/auth.json
   # Run klawed again - it will prompt for browser login
   ```

3. **Check file permissions:**
   ```bash
   chmod 600 ~/.openai/auth.json
   ```

### API Key Issues

If you see "API key is required" errors:

1. Verify the correct environment variable is set for your provider
2. Check that the key is not empty: `echo "$ZAI_API_KEY_CODING_PLAN"`
3. For Z.AI, ensure you're using `ZAI_API_KEY_CODING_PLAN` (not `OPENAI_API_KEY`)

### Model Override Not Working

If `OPENAI_MODEL` is not being used:

1. Ensure it's exported: `export OPENAI_MODEL=your-model` (not just `OPENAI_MODEL=...`)
2. Check for typos in the variable name
3. Some providers may validate model names - check the provider's documentation

## See Also

- [LLM Provider Configuration](llm-provider-configuration.md) - Full provider configuration guide
- [Configuration Files](configuration.md) - Using config.json files
