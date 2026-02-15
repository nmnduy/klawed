# Provider Configuration via Environment Variables

The fastest way to get started with klawed is using environment variables. This guide covers all provider-specific environment variables for quick setup without configuration files.

## Quick Start

```bash
export OPENAI_API_KEY="sk-..."
klawed "Hello, world!"
```

## Core Environment Variables

### API Key (Required)

| Variable | Description |
|----------|-------------|
| `OPENAI_API_KEY` | API key for OpenAI, Anthropic, or any OpenAI-compatible API |

Most providers use `OPENAI_API_KEY` regardless of the actual provider (OpenAI, Anthropic, local APIs, etc.).

### Model Selection

| Variable | Description | Example |
|----------|-------------|---------|
| `OPENAI_MODEL` | Model name to use | `gpt-4`, `claude-3-5-sonnet-20241022` |

### API Endpoint

| Variable | Description | Example |
|----------|-------------|---------|
| `OPENAI_API_BASE` | Base URL for the API | `https://api.openai.com/v1` |

## Provider-Specific Setups

### OpenAI

```bash
export OPENAI_API_KEY="sk-..."
export OPENAI_MODEL="gpt-4"
export OPENAI_API_BASE="https://api.openai.com/v1"
klawed "Your prompt"
```

**Default model if not specified:** `gpt-4`

### Anthropic

```bash
export OPENAI_API_KEY="sk-ant-..."  # Your Anthropic API key
export OPENAI_MODEL="claude-3-5-sonnet-20241022"
export OPENAI_API_BASE="https://api.anthropic.com/v1"
klawed "Your prompt"
```

**Supported models:**
- `claude-3-5-sonnet-20241022`
- `claude-3-opus-20240229`
- `claude-3-5-haiku-20241022`

### AWS Bedrock

AWS Bedrock uses AWS credentials instead of API keys:

```bash
export AWS_ACCESS_KEY_ID="..."
export AWS_SECRET_ACCESS_KEY="..."
export AWS_REGION="us-east-1"
export OPENAI_MODEL="us.anthropic.claude-3-5-sonnet-20241022-v2:0"
export KLAWED_USE_BEDROCK=1
klawed "Your prompt"
```

**Or use the provider selector:**
```bash
export KLAWED_LLM_PROVIDER="sonnet-4.5-bedrock"
klawed "Your prompt"
```

### Local/OpenAI-Compatible APIs

For Ollama, llama.cpp, vLLM, or other local servers:

```bash
export OPENAI_API_KEY="not-needed"  # Many local servers don't require a key
export OPENAI_MODEL="llama3.1"
export OPENAI_API_BASE="http://localhost:8080/v1"
klawed "Your prompt"
```

### OpenRouter

```bash
export OPENAI_API_KEY="sk-or-..."
export OPENAI_MODEL="anthropic/claude-3.5-sonnet"
export OPENAI_API_BASE="https://openrouter.ai/api/v1"
klawed "Your prompt"
```

### DeepSeek

```bash
export OPENAI_API_KEY="sk-..."
export OPENAI_MODEL="deepseek-chat"
export OPENAI_API_BASE="https://api.deepseek.com"
klawed "Your prompt"
```

### Moonshot/Kimi

```bash
export OPENAI_API_KEY="sk-..."
export OPENAI_MODEL="kimi-k2.5"
export OPENAI_API_BASE="https://api.moonshot.ai"
klawed "Your prompt"
```

## Advanced Options

### Authentication Header

For APIs requiring custom authentication headers:

```bash
export OPENAI_AUTH_HEADER="x-api-key: %s"
# or
export OPENAI_AUTH_HEADER="Authorization: Bearer %s"
```

The `%s` is replaced with your `OPENAI_API_KEY`.

### Extra Headers

Add additional headers to API requests:

```bash
export OPENAI_EXTRA_HEADERS="anthropic-version: 2023-06-01, X-Custom-Header: value"
```

Headers are comma-separated.

### Provider Selection

When you have multiple providers configured in `config.json`, select one via environment variable:

```bash
export KLAWED_LLM_PROVIDER="sonnet-4.5-bedrock"
klawed "Your prompt"
```

This overrides the `active_provider` setting in your config file.

## Environment Variable Priority

When no named provider is selected, environment variables take precedence over config file settings:

1. `OPENAI_MODEL` - overrides config file `model`
2. `OPENAI_API_BASE` - overrides config file `api_base`
3. `OPENAI_API_KEY` - used if config file has no API key

When a named provider is selected (via `KLAWED_LLM_PROVIDER`), the provider's configuration in `config.json` takes precedence, and environment variables are only used as fallbacks.

## Shell Configuration

Add to your shell profile for persistent configuration:

### Bash (~/.bashrc)

```bash
# OpenAI
export OPENAI_API_KEY="sk-..."
export OPENAI_MODEL="gpt-4"

# Optional: default to a named provider from config.json
export KLAWED_LLM_PROVIDER="gpt-4"
```

### Zsh (~/.zshrc)

```bash
# OpenAI
export OPENAI_API_KEY="sk-..."
export OPENAI_MODEL="gpt-4"
```

### Fish (~/.config/fish/config.fish)

```fish
# OpenAI
set -x OPENAI_API_KEY "sk-..."
set -x OPENAI_MODEL "gpt-4"
```

## Switching Providers Quickly

Use shell aliases or functions for quick switching:

```bash
# ~/.bashrc or ~/.zshrc

# OpenAI GPT-4
alias klawed-gpt4='KLAWED_LLM_PROVIDER=gpt-4-turbo klawed'

# Claude via Bedrock
alias klawed-claude='KLAWED_LLM_PROVIDER=sonnet-4.5-bedrock klawed'

# Local Ollama
alias klawed-local='OPENAI_API_BASE=http://localhost:11434/v1 OPENAI_MODEL=llama3.1 klawed'
```

Usage:
```bash
klawed-gpt4 "Explain this code"
klawed-claude "Refactor this function"
klawed-local "What is recursion?"
```

## Security Best Practices

1. **Never commit API keys** - Add to `.gitignore` if storing in project files
2. **Use shell profiles** - Store keys in `~/.bashrc`, `~/.zshrc`, or `~/.profile`
3. **Use a secrets manager** - For team environments, consider tools like:
   - `direnv` for per-directory environment variables
   - 1Password CLI (`op run`)
   - HashiCorp Vault
   - AWS/GCP/Azure secrets managers

### Using direnv (Recommended for Projects)

Create `.envrc` in your project root:

```bash
export OPENAI_API_KEY="sk-..."
export OPENAI_MODEL="gpt-4"
```

Run `direnv allow` to load automatically when entering the directory.

Add `.envrc` to `.gitignore` to prevent accidental commits.

## Troubleshooting

### "No API key configured"

- Verify `OPENAI_API_KEY` is set: `echo $OPENAI_API_KEY`
- Check for typos in the variable name
- Ensure the export is in the correct shell profile

### "Model not found" or 404 errors

- Verify `OPENAI_MODEL` matches a valid model name for your provider
- Check `OPENAI_API_BASE` has the correct URL (including `/v1` if needed)

### "Connection refused" for local APIs

- Verify the local server is running: `curl $OPENAI_API_BASE/models`
- Check the port and protocol (http vs https)

### Authentication errors

- For OpenAI: ensure key starts with `sk-`
- For Anthropic: ensure key starts with `sk-ant-`
- For Bedrock: verify AWS credentials are configured

## Next Steps

- For multiple provider management, see [LLM Provider Configuration](./llm-provider-configuration.md)
- For per-project configuration, consider using `config.json` files
- For subagent provider selection, see [Subagent Tool](./subagent.md)
