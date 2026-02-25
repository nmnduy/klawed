# Voice Mode with Whisper.cpp

Local voice-to-text transcription using whisper.cpp and ffmpeg. No API key required, completely offline.

## Overview

Voice mode allows you to use the `/voice` command to record audio and transcribe it locally using:
- **whisper.cpp v1.8.2** for local transcription (no API calls)
- **ffmpeg** for audio recording (microphone input)
- **ggml-small.en** model (~465MB) for English transcription

> **Version Pinning:** This project uses whisper.cpp v1.8.2 (released 2024-10-15) for stable, reproducible builds. The version is controlled in the Makefile via `WHISPER_VERSION`.

## Quick Start

### 1. Install Dependencies

**macOS:**
```bash
brew install ffmpeg cmake
```

**Ubuntu/Debian:**
```bash
sudo apt install ffmpeg cmake build-essential
```

### 2. Setup Whisper.cpp

```bash
# Initialize whisper.cpp submodule
git submodule update --init --recursive

# Build whisper.cpp and download model (one-time setup)
make setup-voice
```

This will:
- Build whisper.cpp static libraries (~2-3 minutes)
- Download ggml-small.en.bin model (~465MB)

### 3. Build with Voice Support

```bash
# Clean previous build
make clean

# Build with voice enabled
make VOICE=1

# Run
./build/klawed
```

### 4. Use Voice Mode

Inside the TUI:
```
/voice
```

Press `Ctrl+C` to stop recording. The transcription will be added to your input.

## Build Options

### VOICE Build Flag

```bash
# Explicitly enable (requires ffmpeg + whisper.cpp)
make VOICE=1

# Explicitly disable (uses stub, smaller binary)
make VOICE=0

# Auto-detect (default: enables if ffmpeg found)
make VOICE=auto  # or just: make
```

### Without Voice Support

If you don't need voice mode:
```bash
make VOICE=0
```

This builds a lighter binary without whisper.cpp dependencies.

## Model Options

### Default Model (Recommended)

**ggml-small.en.bin** (~465MB)
- Good balance of quality and speed
- ~2-3x faster than real-time on M1/M2
- English only
- Downloaded automatically with `make setup-voice`

### Alternative Models

**Tiny (Fast, Lower Quality):**
```bash
make download-model-tiny
export WHISPER_MODEL_PATH=./whisper_models/ggml-tiny.en.bin
```

**Base (Faster, Good Quality):**
```bash
make download-model-base
export WHISPER_MODEL_PATH=./whisper_models/ggml-base.en.bin
```

### Model Sizes

| Model | Size | Speed | Quality | Use Case |
|-------|------|-------|---------|----------|
| tiny.en | ~75MB | Fastest | Basic | Testing, simple queries |
| base.en | ~150MB | Fast | Good | General use, lower-end hardware |
| small.en | ~465MB | Moderate | Better | Recommended (default) |

## Environment Variables

### WHISPER_MODEL_PATH
Path to custom whisper model file.

```bash
export WHISPER_MODEL_PATH=/path/to/ggml-small.en.bin
```

**Default search paths:**
1. `$WHISPER_MODEL_PATH`
2. `./whisper_models/ggml-small.en.bin`
3. `./whisper_models/ggml-base.en.bin`
4. `./whisper_models/ggml-tiny.en.bin`
5. `~/.local/share/klawed/models/ggml-small.en.bin`
6. `/usr/local/share/whisper/ggml-small.en.bin`

### VOICE_DEVICE
Audio input device to use.

**macOS (AVFoundation):**
```bash
# Default: built-in microphone
export VOICE_DEVICE=0

# List available devices:
ffmpeg -f avfoundation -list_devices true -i ""
```

**Linux (ALSA):**
```bash
# Default: system default
export VOICE_DEVICE=default

# Or specify by name:
export VOICE_DEVICE=hw:0

# List available devices:
arecord -l
```

## Architecture

### Components

```
┌─────────────┐
│   /voice    │  User command
└──────┬──────┘
       │
       v
┌─────────────────┐
│  voice_input.c  │  Recording + Transcription
└─────┬───────────┘
      │
      ├─→ ffmpeg (record audio to .wav)
      │
      └─→ whisper.cpp (local transcription)
           │
           ├─→ libwhisper.a
           ├─→ libggml.a
           └─→ libggml-metal.a (macOS GPU acceleration)
```

### Files

- `src/voice_input.h` - Public API (unchanged)
- `src/voice_input.c` - Whisper.cpp implementation
- `src/voice_stub.c` - Stub when `VOICE=0`
- `external/whisper.cpp/` - Git submodule
- `whisper_models/` - Downloaded models (gitignored)

## Version Management

### Pinned Version

This project pins whisper.cpp to **v1.8.2** (released October 15, 2024) for:
- **Reproducible builds** - Same version = same behavior
- **Stability** - Tested and known to work
- **API compatibility** - No surprise breaking changes

### Checking Version

```bash
# Check current whisper.cpp version
make check-whisper-version
```

Output:
```
Whisper.cpp version information:
  Pinned version: v1.8.2
  Current checkout: v1.8.2
  ✓ On correct version
```

### Upgrading Whisper.cpp

When a new version is released:

1. **Test the new version:**
   ```bash
   cd external/whisper.cpp
   git fetch --tags
   git checkout v1.9.0  # example new version
   cd ../..
   make clean-whisper
   make setup-voice
   make VOICE=1
   # Test thoroughly
   ```

2. **If tests pass, update the Makefile:**
   ```makefile
   WHISPER_VERSION = v1.9.0  # Updated from v1.8.2
   ```

3. **Document changes in commit message:**
   - What changed in whisper.cpp
   - Any API changes needed
   - Performance differences
   - Why the upgrade is needed

### Release History

| Version | Release Date | Status | Notes |
|---------|-------------|--------|-------|
| v1.8.2 | 2024-10-15 | Current | Stable, tested |
| v1.8.1 | 2024-09-xx | - | Previous |
| v1.8.0 | 2024-08-xx | - | Previous |

## Technical Details

### Recording

**macOS:** Uses `ffmpeg -f avfoundation` to capture from microphone
**Linux:** Uses `ffmpeg -f alsa` to capture from ALSA device

Audio format:
- Sample rate: 16kHz (required by whisper)
- Channels: Mono
- Format: 16-bit PCM WAV
- Max duration: 10 seconds per recording

### Transcription

Whisper.cpp parameters:
- Sampling: Greedy (fastest, deterministic)
- Language: English
- Threads: 4 (configurable)
- Beam size: 1
- Temperature: 0.0

On Apple Silicon (M1/M2/M3):
- Uses Metal GPU acceleration automatically
- ~2-3x faster than real-time
- Low memory usage

### Build System

**Static linking:** All whisper libraries linked statically
- `libwhisper.a` - Core whisper implementation
- `libggml.a` - ML backend
- `libggml-metal.a` - Metal GPU support (macOS only)

**Frameworks (macOS):**
- `-framework Accelerate` - BLAS/LAPACK
- `-framework Metal` - GPU compute
- `-framework MetalKit` - GPU utilities
- `-framework Foundation` - System libraries

## Troubleshooting

### Submodule Not Found

```bash
git submodule update --init --recursive
```

### Build Fails

```bash
# Clean whisper build
make clean-whisper

# Rebuild
make setup-voice
```

### Model Not Found

```bash
# Re-download model
rm -f whisper_models/ggml-small.en.bin
make download-model
```

### ffmpeg Not Found

**macOS:**
```bash
brew install ffmpeg
```

**Ubuntu:**
```bash
sudo apt install ffmpeg
```

### No Audio Recorded

Check microphone permissions:
- **macOS:** System Settings → Privacy & Security → Microphone
- **Linux:** Check ALSA mixer levels: `alsamixer`

### Transcription Failed

- Check model file exists: `ls -lh whisper_models/`
- Try a different model: `export WHISPER_MODEL_PATH=...`
- Check ffmpeg can access microphone: `ffmpeg -f avfoundation -i :0 -t 5 test.wav`

## Performance

### Benchmarks (M1 MacBook Pro)

| Model | Load Time | Transcription Time (10s audio) | Quality |
|-------|-----------|-------------------------------|---------|
| tiny.en | ~0.1s | ~2s | Basic |
| base.en | ~0.2s | ~3s | Good |
| small.en | ~0.5s | ~5s | Better |

### Memory Usage

- Base overhead: ~100MB
- tiny.en: +75MB
- base.en: +150MB
- small.en: +465MB

## Comparison with Previous PortAudio Version

| Feature | PortAudio + OpenAI API | Whisper.cpp + ffmpeg |
|---------|----------------------|---------------------|
| Requires API key | ✅ Yes | ❌ No |
| Internet required | ✅ Yes | ❌ No |
| Cost | $0.006/min | Free |
| Privacy | Uploads audio | Local only |
| Quality | Excellent | Very good |
| Speed | ~real-time | 2-3x real-time |
| Dependency install | Easy | Moderate |
| Binary size | +50KB | +2MB |

## Future Enhancements

- [ ] Streaming transcription (real-time)
- [ ] Multi-language support
- [ ] Model quantization (smaller files)
- [ ] Voice activity detection (auto-start/stop)
- [ ] Custom vocabulary/prompts
- [ ] GPU acceleration on Linux (CUDA, ROCm)

## See Also

- [Whisper.cpp GitHub](https://github.com/ggerganov/whisper.cpp)
- [OpenAI Whisper](https://github.com/openai/whisper)
- [GGML](https://github.com/ggerganov/ggml)
