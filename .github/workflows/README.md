# GitHub Workflows Summary

## Overview
This repository uses two separate GitHub Actions workflows that serve distinct purposes:

### 1. CI Workflow (`.github/workflows/ci.yml`)
**Triggers:** Every push and pull request to `main`/`master` branches
**Purpose:** Continuous integration testing

**Features:**
- Tests on Ubuntu and macOS with gcc and clang (4 build configurations)
- Runs unit tests (`make test`)
- Verifies installation process
- Builds with sanitizers (address and undefined behavior)
- Fast feedback loop for development

### 2. Release Workflow (`.github/workflows/release.yml`)
**Triggers:** Git tags starting with `v*` (e.g., `v1.0.0`) or manual dispatch
**Purpose:** Build and distribute releases

**Features:**
- Builds release binaries for Linux, macOS, and Windows
- Creates GitHub releases with proper assets
- Generates Docker images with multi-platform support
- Automatic release notes from git history
- Prerelease support

## Recent Improvements

### Reusable Action (`.github/actions/setup-build/action.yml`)
Created a composite action to eliminate duplication:
- Centralized dependency installation logic
- Supports Ubuntu, macOS, and Windows
- Used by both CI and release workflows
- Easier to maintain and update

### Key Changes Made
1. **Extracted common setup steps** into reusable action
2. **Updated to checkout@v4** across all workflows
3. **Simplified workflow definitions** by removing redundancy
4. **Maintained separation of concerns** - CI for testing, Release for distribution

## Why Keep Both Workflows?

The workflows serve different purposes and should remain separate:

### CI Workflow - "Does this code work?"
- **Frequency:** Runs on every change
- **Scope:** Testing and validation only
- **Speed:** Optimized for quick feedback
- **Audience:** Developers getting PR feedback

### Release Workflow - "Can users install this?"
- **Frequency:** Runs only on version tags
- **Scope:** Full build and distribution
- **Comprehensiveness:** Cross-platform binaries, Docker images
- **Audience:** End users downloading releases

## Maintenance Benefits

1. **Single source of truth** for dependency installation
2. **Easier updates** - change dependency setup in one place
3. **Consistent environments** between CI and release builds
4. **Reduced duplication** means fewer places for bugs

## Future Considerations

- Could add security scanning to CI workflow
- Might add performance benchmarking
- Could expand matrix testing (more OS/compiler combinations)
- May add artifact caching for faster builds