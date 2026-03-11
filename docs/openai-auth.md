# Authenticating to OpenAI with a Subscribed Plan

This guide explains how to configure klawed to use OpenAI's API with a paid subscription (Plus, Pro, or API account).

---

## Prerequisites

- An OpenAI account at [platform.openai.com](https://platform.openai.com)
- An active subscription: **ChatGPT Plus/Pro** (for ChatGPT-tier access) or an **API billing plan** (pay-as-you-go or pre-paid credits)
- An API key generated from [platform.openai.com/api-keys](https://platform.openai.com/api-keys)

> **Note:** Free OpenAI accounts have no API access. You need a paid plan to generate an API key and make API calls.

---

## Quick Setup (Environment Variable)

The fastest way to authenticate is to export your OpenAI API key:

```bash
export OPENAI_API_KEY="sk-..."
klawed "Your prompt here"
```

klawed will automatically use `https://api.openai.com/v1/chat/completions` as the endpoint and default to `gpt-4` as the model.

To set a specific model (e.g., `gpt-4o`, `gpt-4o-mini`, `o1`, `o3`):

```bash
export OPENAI_API_KEY="sk-..."
export OPENAI_MODEL="gpt-4o"
klawed "Your prompt here"
```

To persist these across shell sessions, add them to your shell profile (`~/.bashrc`, `~/.zshrc`, etc.):

```bash
# OpenAI credentials
export OPENAI_API_KEY="sk-..."
export OPENAI_MODEL="gpt-4o"
```

---

## Config File Setup (Recommended)

For a durable, project-aware setup, define a named provider in your klawed config.

### 1. Create or edit `~/.klawed/config.json`

```json
{
  "active_provider": "openai-gpt4o",
  "providers": {
    "openai-gpt4o": {
      "provider_type": "openai",
      "provider_name": "OpenAI GPT-4o",
      "model": "gpt-4o",
      "api_base": "https://api.openai.com/v1"
    }
  }
}
```

The `api_base` must point to the OpenAI chat completions endpoint root — klawed appends `/chat/completions` automatically.

### 2. Set your API key

API keys should **not** be stored in the config file. Use an environment variable instead:

```bash
export OPENAI_API_KEY="sk-..."
```

Or use `api_key_env` in the config to name a custom variable:

```json
{
  "providers": {
    "openai-gpt4o": {
      "provider_type": "openai",
      "model": "gpt-4o",
      "api_base": "https://api.openai.com/v1",
      "api_key_env": "OPENAI_API_KEY"
    }
  }
}
```

Key resolution order (highest to lowest priority):
1. The env var named in `api_key_env`
2. `api_key` field in the provider config (not recommended)
3. `OPENAI_API_KEY` environment variable (fallback)

### 3. Verify

```bash
klawed "Say hello"
```

---

## Available OpenAI Models

The `model` field accepts any model ID from OpenAI's platform. Common choices:

| Model ID | Description |
|---|---|
| `gpt-4o` | Fast, capable flagship model |
| `gpt-4o-mini` | Cheaper, lower-latency option |
| `gpt-4-turbo` | Previous generation GPT-4 |
| `o1` | Reasoning model (slower, more thorough) |
| `o3` | Latest high-capability reasoning model |
| `o4-mini` | Fast reasoning model |

> The model you can access depends on your subscription tier and whether you have sufficient API credits.

---

## Multiple Providers (Switching Between Models)

You can define several OpenAI configurations as separate named providers and switch between them:

```json
{
  "active_provider": "gpt-4o",
  "providers": {
    "gpt-4o": {
      "provider_type": "openai",
      "model": "gpt-4o",
      "api_base": "https://api.openai.com/v1"
    },
    "gpt-4o-mini": {
      "provider_type": "openai",
      "model": "gpt-4o-mini",
      "api_base": "https://api.openai.com/v1"
    },
    "o3": {
      "provider_type": "openai",
      "model": "o3",
      "api_base": "https://api.openai.com/v1"
    }
  }
}
```

Switch on the fly using the `KLAWED_LLM_PROVIDER` env var (overrides `active_provider`):

```bash
KLAWED_LLM_PROVIDER=o3 klawed "Solve this problem..."
KLAWED_LLM_PROVIDER=gpt-4o-mini klawed "Quick summary..."
```

Or switch inside a session with the `/provider` command:

```
/provider gpt-4o-mini
```

---

## Project-Level Config

To use a different OpenAI model for a specific project without touching your global config, create `.klawed/config.json` in the project root:

```json
{
  "active_provider": "o3"
}
```

This overrides the `active_provider` from `~/.klawed/config.json` while inheriting all defined providers from the global file.

---

## Security

- **Never commit your API key.** Add `.klawed/` to your `.gitignore`.
- **Use environment variables** rather than the `api_key` config field.
- **Set file permissions** on your config: `chmod 600 ~/.klawed/config.json`
- For team or CI environments, use a secrets manager (e.g., `direnv`, 1Password CLI, AWS Secrets Manager) to inject `OPENAI_API_KEY` at runtime.

---

## How Plan Access Works

OpenAI authenticates all API calls via the `Authorization: Bearer <api_key>` header. The API key is tied to your account, and your **API billing plan** (separate from ChatGPT Plus/Pro) governs rate limits and available models.

- **ChatGPT Plus/Pro subscriptions** give you access to ChatGPT at chat.openai.com — they do **not** automatically grant API access or credits.
- **API billing** is managed separately at [platform.openai.com/settings/billing](https://platform.openai.com/settings/billing). You need to add a payment method or buy pre-paid credits.
- Once billing is active, generate an API key at [platform.openai.com/api-keys](https://platform.openai.com/api-keys) and set it as `OPENAI_API_KEY`.

If you hit a `429 Too Many Requests` or `insufficient_quota` error, your API account either has no credits remaining or your tier's rate limits have been reached.

---

## Troubleshooting

| Error | Cause | Fix |
|---|---|---|
| `No API key configured` | `OPENAI_API_KEY` not set | `export OPENAI_API_KEY="sk-..."` |
| `401 Unauthorized` | Invalid or expired key | Regenerate key at platform.openai.com/api-keys |
| `429 insufficient_quota` | No API credits | Add billing at platform.openai.com/settings/billing |
| `429 rate_limit_exceeded` | Too many requests | Wait or upgrade to a higher-tier plan |
| `404 model not found` | Model unavailable on your plan | Use a different model (e.g., `gpt-4o-mini`) |
| `Connection refused` | Wrong `api_base` | Verify URL is `https://api.openai.com/v1` |

To check your current key is active:

```bash
curl https://api.openai.com/v1/models \
  -H "Authorization: Bearer $OPENAI_API_KEY" | head -5
```

A successful response returns a JSON list of models. A `401` means the key is invalid.

---

## See Also

- [LLM Provider Configuration](./llm-provider-configuration.md) — full config reference
- [Provider Environment Variables](./provider-env-configuration.md) — all supported env vars
