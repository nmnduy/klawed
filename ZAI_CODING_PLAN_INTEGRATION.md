# Z.AI GLM Coding Plan Integration

This document describes the Z.AI GLM Coding Plan integration for klawed.

## Overview

Z.AI (Zhipu AI) provides a GLM Coding Plan subscription service that gives access to coding-optimized models through a dedicated API endpoint. The integration uses the OpenAI-compatible API format.

## API Details

- **Coding Endpoint**: `https://api.z.ai/api/coding/paas/v4/chat/completions`
- **General Endpoint**: `https://api.z.ai/api/paas/v4/chat/completions`
- **Authentication**: Bearer token via `Authorization: Bearer <api_key>`
- **Format**: OpenAI-compatible chat completions API

## Available Models

With the Coding Plan subscription ($10/month), the following models are available:

- `glm-4.5` - Fast and capable coding model
- `glm-4.6` - Enhanced coding capabilities (recommended)
- `glm-4.7` - Latest improvements

Note: `glm-5` requires a higher-tier subscription.

## Usage

### Method 1: Environment Variables (Quick Start)

```bash
export ZAI_API_KEY_CODING_PLAN="your-api-key-here"
export OPENAI_API_KEY="$ZAI_API_KEY_CODING_PLAN"
export OPENAI_API_BASE="https://api.z.ai/api/coding/paas/v4/chat/completions"
export OPENAI_MODEL="glm-4.6"

klawed "Your coding prompt here"
```

### Method 2: Configuration File

Add to your `~/.klawed/config.json` or `.klawed/config.json`:

```json
{
  "active_provider": "zai-glm-coding",
  "providers": {
    "zai-glm-coding": {
      "provider_type": "zai_coding",
      "provider_name": "Z.AI GLM Coding Plan",
      "model": "glm-4.6",
      "api_base": "https://api.z.ai/api/coding/paas/v4/chat/completions",
      "api_key_env": "ZAI_API_KEY_CODING_PLAN"
    }
  }
}
```

Then run:
```bash
export ZAI_API_KEY_CODING_PLAN="your-api-key-here"
klawed "Your coding prompt here"
```

### Method 3: Using KLAWED_LLM_PROVIDER

With the config above, you can also select the provider explicitly:

```bash
export ZAI_API_KEY_CODING_PLAN="your-api-key-here"
export KLAWED_LLM_PROVIDER=zai-glm-coding
klawed "Your coding prompt here"
```

## Provider Type

The implementation adds a new provider type `zai_coding` that:
- Uses the dedicated coding endpoint by default
- Preserves reasoning content (for thinking models)
- Supports extra headers configuration
- Supports streaming responses

## Files Added/Modified

- `src/zai_coding_provider.h` - New header file
- `src/zai_coding_provider.c` - New implementation
- `src/config.h` - Added `PROVIDER_ZAI_CODING` enum
- `src/config.c` - Added string conversion for zai_coding type
- `src/provider.c` - Added provider initialization logic
- `Makefile` - Added build rules for new provider

## API Key Storage

The API key can be stored in `~/.api_keys`:
```bash
export ZAI_API_KEY_CODING_PLAN=your-api-key-here
```

Then load it with:
```bash
export $(grep ZAI_API_KEY_CODING_PLAN ~/.api_keys)
```

## Testing

Quick test command:
```bash
export $(grep ZAI_API_KEY_CODING_PLAN ~/.api_keys)
export OPENAI_API_KEY="$ZAI_API_KEY_CODING_PLAN"
export OPENAI_API_BASE="https://api.z.ai/api/coding/paas/v4/chat/completions"
export OPENAI_MODEL="glm-4.6"

echo "test" | timeout 15 ./build/klawed "Say 'ZAI works' in exactly 2 words"
```

## Documentation

For more information about the Z.AI API:
- Quick Start: https://docs.z.ai/guides/overview/quick-start
- API Reference: https://docs.z.ai/api-reference

## Notes

- The Coding Plan endpoint is optimized for coding tasks
- The API includes `reasoning_content` in responses for thinking models
- Streaming is supported via SSE (Server-Sent Events)
- Token usage includes cached tokens (shown separately in billing)
