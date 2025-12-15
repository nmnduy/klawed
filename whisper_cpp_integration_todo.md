# Voice Mode Implementation with whisper.cpp

__NOTE__: This file tracks the implementation of a new voice input system using whisper.cpp and ffmpeg to replace the current PortAudio + OpenAI API approach.

## Overview
Replace the current voice input system (PortAudio + OpenAI Whisper API) with a bundled solution using whisper.cpp for local transcription and ffmpeg for audio recording. This provides offline voice input with no API key requirements.

## Implementation Tasks

- [x] Update Makefile for whisper.cpp integration
    - [x] Remove PortAudio dependency and build flags
    - [x] Add `VOICE=1` option to enable voice support
    - [x] Add automatic download of whisper.cpp as submodule or tarball
    - [x] Add build rules for whisper.cpp static library
    - [x] Add model download (ggml-small.en.bin)
    - [x] Add linking against libwhisper.a and all ggml libraries
    - [x] voice_stub.c already exists for when voice is disabled
    - Notes: Backward compatibility maintained - default is no voice support

- [x] Create new voice_input.c implementation
    - [x] Implement ffmpeg-based audio recording using fork/exec
    - [x] Implement whisper.cpp API integration for transcription
    - [x] Add model loading from multiple locations (env var, local, system)
    - [x] Add platform detection for audio input (avfoundation on macOS, pulse on Linux)
    - [x] Implement error handling for missing ffmpeg or model
    - [x] Keep same API as current voice_input.h
    - Notes: Uses whisper.h API directly with singleton context pattern

- [x] Model distribution strategy
    - [x] External file approach - not embedded in binary
    - [x] Model search path: WHISPER_MODEL_PATH env var -> ./whisper_models/ -> system paths
    - [x] Added `make download-model` targets (default, tiny, base)
    - [x] Multiple model sizes supported (tiny.en, base.en, small.en)
    - Notes: Model file stays separate, user can choose size based on accuracy vs speed needs

- [ ] Update voice command integration
    - [ ] Verify /voice command works with new implementation
    - [ ] Update error messages for missing dependencies
    - [ ] Add voice status reporting in startup
    - [ ] Test TUI and non-TUI modes
    - Notes: The command API should remain unchanged

- [ ] Testing and validation
    - [ ] Test build with VOICE=1 and VOICE=0
    - [ ] Test audio recording with ffmpeg
    - [ ] Test transcription with whisper.cpp
    - [ ] Test error cases (no ffmpeg, no model, no microphone)
    - [ ] Test on macOS and Linux
    - [ ] Performance testing with different model sizes
    - Notes: Requires ffmpeg installed at runtime

- [ ] Documentation and installation
    - [ ] Update README with new voice system instructions
    - [ ] Add installation instructions for ffmpeg
    - [ ] Document environment variables (VOICE_DEVICE, WHISPER_MODEL_PATH)
    - [ ] Create quick start guide for voice mode
    - Notes: Emphasize offline capability and no API key requirement

## Dependencies
- ffmpeg (runtime dependency for audio recording)
- whisper.cpp (built and linked statically)
- ggml-small.en.bin model (~500MB, downloaded automatically)

## Build Commands
```bash
# Build with voice support
make VOICE=1

# Build without voice (default)
make

# Install ffmpeg (required at runtime)
brew install ffmpeg          # macOS
sudo apt install ffmpeg      # Ubuntu
```

## Environment Variables
- `VOICE_DEVICE`: Audio device number (default: 2 for macOS)
- `WHISPER_MODEL_PATH`: Path to whisper model file
- `VOICE_MODE`: Could be used for future OpenAI fallback (not implemented yet)

## Future Enhancements
- Support for multiple languages
- Model quantization for smaller size
- Real-time streaming transcription
- Voice command activation (like "Hey Claude")
- Integration with system TTS for responses