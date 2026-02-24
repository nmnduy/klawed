# Claude Code - Pure C Edition

A lightweight, modular implementation of a coding agent that interacts with an Open API compatible API. This is a pure C port of the core functionality from the TypeScript/Node.js Claude Code CLI.

![klawed preview](assets/images/klawed-preview.webp)

## Installation

### Installing Dependencies

**macOS (Homebrew):**
```bash
brew install curl cjson portaudio
```

**Ubuntu/Debian:**
```bash
sudo apt-get install libcurl4-openssl-dev libcjson-dev portaudio19-dev build-essential
```

## Building

**Recommended: Use stable release**
```bash
git clone --branch v0.29.26 https://github.com/nmnduy/klawed.git
cd klawed
make
```

**Building from source (latest development):**
```bash
cd klawed
make
```

This will produce a `klawed` executable in the current directory.

**Optional: Install globally**
```bash
make install
```

This installs to `$HOME/.local/bin/klawed`

### Voice Input (Optional)

Voice input using whisper.cpp is **disabled by default**.

**To enable voice input:**
```bash
# 1. Install ffmpeg (if not already installed)
# macOS: brew install ffmpeg
# Ubuntu: sudo apt install ffmpeg

# 2. Initialize whisper.cpp submodule
git submodule update --init --recursive

# 3. Build with voice input enabled
make VOICE=1 install
```

**Note:** Voice input adds significant build time and dependencies. Most users don't need it.

### Persistent Memory

Klawed uses SQLite with FTS5 for persistent memory storage, enabling the agent to remember facts, preferences, and context across sessions.

**Memory database location:** `.klawed/memory.db` (project-local)

See `docs/memory_db.md` for detailed documentation on the memory system.

## Usage

### Quick start

```sh
export OPENAI_API_KEY="your-api-key"
export OPENAI_API_BASE="https://api.openai.com/v1"
export OPENAI_MODEL="gpt-4o"
klawed
```

We dont have config switching yet. But you can also just do this in .bashrc

```bash
alias deepseek-chat="OPENAI_API_KEY=$DEEPSEEK_API_KEY OPENAI_API_BASE=https://api.deepseek.com OPENAI_MODEL=deepseek-chat klawed"
alias glm-4.7="OPENAI_API_KEY=$ZAI_API_KEY OPENAI_API_BASE=https://api.z.ai/api/paas/v4/chat/completions OPENAI_MODEL=glm-4.7 klawed"
alias kimi-k2-thinking="OPENAI_API_KEY=$MOONSHOT_AI_API_KEY OPENAI_API_BASE=https://api.moonshot.ai OPENAI_MODEL=kimi-k2-thinking klawed"
alias kimi-for-coding="KLAWED_LLM_PROVIDER=kimi-for-coding klawed"
alias gpt-5-1-codex-max="OPENAI_API_KEY=$OPENROUTER_API_KEY OPENAI_API_BASE=https://openrouter.ai/api/v1/chat/completions OPENAI_MODEL=openai/gpt-5.1-codex-max klawed"
alias minimax-2.1-coding-plan="ANTHROPIC_BASE_URL=https://api.minimax.io/anthropic/v1/messages OPENAI_API_KEY=$MINIMAX_CODING_PLAN_API_KEY OPENAI_MODEL=MiniMax-M2.1 ANTHROPIC_VERSION=2023-06-01 OPENAI_API_BASE= klawed"
```

**Note:** For `kimi-for-coding`, add this to your `~/.klawed/config.json`:
```json
{
  "providers": {
    "kimi-for-coding": {
      "provider_type": "kimi_coding_plan",
      "model": "kimi-for-coding"
    }
  }
}
```
No API key required - OAuth authentication will prompt you to authorize via browser on first use.

### Color Theme Support

**Available built-in themes:** `kitty-default`, `dracula`, `gruvbox-dark`, `solarized-dark`, `black-metal`

Override via env var `KLAWED_THEME`

## Memory footprint

![Memory usage](assets/images/klawed-memory-usage.webp)

## Security Notes

- No sandboxing: Bash tool has full shell access
- File tools can access any readable/writable file
- Intended for trusted environments only
