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
git clone --branch v0.1.28 https://github.com/nmnduy/klawed.git
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

## Usage

### Quick start

```sh
export OPENAI_API_KEY=$DEEPSEEK_API_KEY
export OPENAI_API_BASE=https://api.deepseek.com
export OPENAI_MODEL=deepseek-chat
klawed
```

We dont have config switching yet. But you can also just do this in .bashrc

```bash
alias deepseek-chat="OPENAI_API_KEY=$DEEPSEEK_API_KEY OPENAI_API_BASE=https://api.deepseek.com OPENAI_MODEL=deepseek-chat klawed"
alias glm-4.6="OPENAI_API_KEY=$ZAI_API_KEY OPENAI_API_BASE=https://api.z.ai/api/paas/v4/chat/completions OPENAI_MODEL=glm-4.6 klawed"
alias kimi-k2-thinking="OPENAI_API_KEY=$MOONSHOT_AI_API_KEY OPENAI_API_BASE=https://api.moonshot.ai OPENAI_MODEL=kimi-k2-thinking klawed"
alias gpt-5="OPENAI_API_BASE=https://api.openai.com OPENAI_MODEL=gpt-5 klawed"
```

### Color Theme Support

**Available built-in themes:** `kitty-default`, `dracula`, `gruvbox-dark`, `solarized-dark`, `black-metal`

Override via env var `KLAWED_THEME`

## Memory footprint

![Memory usage](assets/images/klawed-memory-usage.webp)

## Security Notes

- No sandboxing: Bash tool has full shell access
- File tools can access any readable/writable file
- Intended for trusted environments only
