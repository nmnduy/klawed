# Release Process Guide

This document explains how automated releases work for Claude C - Pure C Edition, including the centralized version management system.

## Version Management System

Claude C uses a centralized version management system to ensure consistency across source code, build system, release process, and documentation.

### Core Version Files

```
├── VERSION              # Plain text version string (single source of truth)
├── version.h            # Generated header with version constants
├── src/version.h        # Auto-generated during build
├── .versionrc           # Conventional commits configuration
└── docs/RELEASE.md      # This documentation
```

### VERSION File

The `VERSION` file contains the current version as a plain string:

```
0.0.2
```

This is the **single source of truth** for all version information.

### Version Management Commands

```bash
# Simple version string
make version

# Detailed version information
make show-version

# Automatically bump patch version, commit, tag, and push (RECOMMENDED)
make bump-patch

# Manually update to new version
make update-version VERSION=1.0.0

# Update to pre-release version
make update-version VERSION=1.0.0-beta.1
```

### Version Usage in Code

```c
#include "version.h"

// Use version constants
printf("Claude C version %s\n", CLAUDE_C_VERSION);
printf("Full version: %s\n", CLAUDE_C_VERSION_FULL);

// Programmatic version checks
#if CLAUDE_C_VERSION_NUMBER >= 0x010000
// Use version 1.0.0+ features
#endif
```

### Command-Line Version Flag

The main executable supports `--version` flag:

```bash
./build/claude-c --version
# Output: Claude C version 0.0.2 (built 2025-10-28)
```

## Semantic Versioning

Claude C follows [Semantic Versioning 2.0.0](https://semver.org/):

### Version Format

```
MAJOR.MINOR.PATCH[-PRERELEASE[+BUILD]]
```

Examples:
- `1.0.0` - Stable release
- `1.0.1` - Patch release (bug fixes)
- `1.1.0` - Minor release (new features)
- `2.0.0` - Major release (breaking changes)
- `1.2.0-alpha.1` - Pre-release
- `1.2.0-beta.2` - Beta pre-release
- `1.2.0-rc.1` - Release candidate

### Version Increment Rules

| Type | Description | Example |
|------|-------------|---------|
| **MAJOR** | Breaking changes | 1.0.0 → 2.0.0 |
| **MINOR** | New features (backward compatible) | 1.0.0 → 1.1.0 |
| **PATCH** | Bug fixes (backward compatible) | 1.0.0 → 1.0.1 |
| **PRERELEASE** | Pre-release versions | 1.0.0 → 1.0.1-alpha.1 |

## Automated Release Workflow

### Triggering Releases

There are three ways to trigger automated releases:

#### 1. Automated Patch Bump (Recommended)
```bash
# Automatically increments patch version, regenerates version.h,
# commits changes, creates tag, and pushes to remote
make bump-patch

# Example: 0.0.2 → 0.0.3
# This will:
# - Update VERSION file
# - Regenerate src/version.h
# - Commit with message "chore: bump version to 0.0.3"
# - Create annotated tag "v0.0.3"
# - Push commit and tag to origin/master
```

#### 2. Manual Tag-based Releases
```bash
# Update version first
make update-version VERSION=1.0.0
git add VERSION src/version.h
git commit -m "chore: bump version to 1.0.0"

# Create a new version tag and push
git tag v1.0.0
git push origin master
git push origin v1.0.0

# For pre-releases
make update-version VERSION=1.0.0-beta.1
git add VERSION src/version.h
git commit -m "chore: bump version to 1.0.0-beta.1"
git tag v1.0.0-beta.1
git push origin master
git push origin v1.0.0-beta.1
```

#### 3. Manual Workflow Dispatch
1. Go to your repository's **Actions** tab
2. Select **Build and Release** workflow
3. Click **Run workflow**
4. Fill in the version and prerelease settings

### What Happens Automatically

The GitHub Actions workflow will:

1. **Read VERSION file** for version information
2. **Build binaries for multiple platforms:**
   - Linux x86_64 (statically linked)
   - macOS x86_64 (dynamically linked with Homebrew deps)
   - Windows x86_64 (MSVC build with vcpkg)

3. **Create release archives:**
   - `claude-c-linux-x86_64.tar.gz`
   - `claude-c-macos-x86_64.tar.gz`
   - `claude-c-windows-x86_64.zip`

4. **Generate GitHub Release:**
   - Automatic release notes from recent commits
   - All binaries uploaded as release assets
   - Prerelease flag automatically detected from version

5. **Build and publish Docker image:**
   - Multi-platform (linux/amd64, linux/arm64)
   - Tags: `latest`, `v1.0.0`, `v1.0`, `v1`
   - Hosted on GitHub Container Registry

### Version in Release Assets

All release binaries include version information:

```bash
./claude-c --version
# Claude C version 1.0.0 (built 2025-10-28)
```

## Integration with CI/CD

### GitHub Actions

The release workflow uses the VERSION file:

```yaml
- name: Get version
  id: version
  run: |
    VERSION=$(cat VERSION)
    echo "version=$VERSION" >> $GITHUB_OUTPUT
```

### Docker Images

Docker images are tagged with version:

```bash
# Tags automatically created
ghcr.io/username/claude-c:1.0.0
ghcr.io/username/claude-c:v1.0.0
ghcr.io/username/claude-c:latest
```

## Multiple Codebases

The project has multiple codebases that share version management:

### Main Project
- Location: `/src/`
- Version file: `VERSION`

### AWS Bedrock Provider
- Location: `/aws-bedrock-provider/src/`
- Uses same version.h from main project
- No separate version management needed

Both codebases include the same `version.h` and use identical version constants.

## Release Assets

### Binary Downloads

Each release includes:

- **Linux:** Static binary, no dependencies required
- **macOS:** Binary requiring Homebrew packages (`curl cjson sqlite3`)
- **Windows:** Self-contained executable with all dependencies

### Docker Images

Available from GitHub Container Registry:

```bash
# Pull the latest version
docker pull ghcr.io/yourusername/claude-c:latest

# Pull a specific version
docker pull ghcr.io/yourusername/claude-c:v1.0.0

# Run the container
docker run --rm -it \
  -e OPENAI_API_KEY="$OPENAI_API_KEY" \
  -v $(pwd):/workspace \
  ghcr.io/yourusername/claude-c:latest "your prompt here"
```

## Installation Instructions

### Binary Installation

#### Linux
```bash
# Download and extract
wget https://github.com/yourusername/claude-c/releases/download/v1.0.0/claude-c-linux-x86_64.tar.gz
tar -xzf claude-c-linux-x86_64.tar.gz
cd claude-c-linux-x86_64

# Install binary
sudo cp claude-c /usr/local/bin/
chmod +x /usr/local/bin/claude-c

# Or install to user directory
mkdir -p ~/.local/bin
cp claude-c ~/.local/bin/
echo 'export PATH="$HOME/.local/bin:$PATH"' >> ~/.bashrc
```

#### macOS
```bash
# Install dependencies first
brew install curl cjson sqlite3

# Download and extract
curl -L -o claude-c-macos-x86_64.tar.gz \
  https://github.com/yourusername/claude-c/releases/download/v1.0.0/claude-c-macos-x86_64.tar.gz
tar -xzf claude-c-macos-x86_64.tar.gz
cd claude-c-macos-x86_64

# Install
sudo cp claude-c /usr/local/bin/
chmod +x /usr/local/bin/claude-c
```

#### Windows
```powershell
# Download and extract
Invoke-WebRequest -Uri "https://github.com/yourusername/claude-c/releases/download/v1.0.0/claude-c-windows-x86_64.zip" -OutFile "claude-c.zip"
Expand-Archive -Path claude-c.zip -DestinationPath .
cd claude-c-windows-x86_64

# Add to PATH or copy to desired location
```

### Docker Installation
```bash
# Pull and run
docker run --rm -it \
  -e OPENAI_API_KEY="$OPENAI_API_KEY" \
  -v $(pwd):/workspace \
  ghcr.io/yourusername/claude-c:latest
```

### Package Manager Installation (Future)

Planned support for:
- Homebrew (macOS)
- APT (Debian/Ubuntu)
- YUM/DNF (Fedora/RHEL)
- Scoop (Windows)

## Release Checklist

Before creating a release:

### Pre-release Checklist
- [ ] All tests pass: `make test`
- [ ] Memory tests pass: `make memscan`
- [ ] Documentation updated (README.md, CHANGELOG.md)
- [ ] CHANGELOG.md updated with changes since last release
- [ ] License file present
- [ ] No sensitive information in binaries
- [ ] **Version bumped and tagged**:
  - For patch releases: `make bump-patch` (handles everything automatically)
  - For major/minor releases: `make update-version VERSION=1.0.0` + manual commit/tag

### Post-release Checklist
- [ ] Release created successfully
- [ ] All binaries uploaded
- [ ] Docker image built and pushed
- [ ] Release notes are accurate
- [ ] Test download and installation on each platform
- [ ] Update website/documentation with new version

## Version Troubleshooting

### Version Mismatch

If you see version mismatches:

1. **Check VERSION file**:
   ```bash
   cat VERSION
   ```

2. **Regenerate version.h**:
   ```bash
   make clean
   make
   ```

3. **Verify binary version**:
   ```bash
   ./build/claude-c --version
   ```

### Build Issues

If version.h generation fails:

1. **Check VERSION file exists**:
   ```bash
   ls -la VERSION
   ```

2. **Check VERSION file format**:
   ```bash
   cat VERSION  # Should be just version string
   ```

3. **Clean rebuild**:
   ```bash
   make clean
   rm -f src/version.h
   make
   ```

### Release Issues

If release fails due to version:

1. **Verify version format** follows semver
2. **Check tag matches VERSION file**:
   ```bash
   # Should match
   echo "v$(cat VERSION)"
   git tag --list
   ```

## Best Practices

### Development Workflow

#### Quick Patch Releases (Recommended)
For bug fixes and patch releases:
```bash
# Single command handles everything
make bump-patch
```

#### Manual Version Updates
For major/minor versions or pre-releases:

1. **Update version**:
   ```bash
   make update-version VERSION=1.0.0
   ```

2. **Commit version changes**:
   ```bash
   git add VERSION src/version.h
   git commit -m "chore: bump version to 1.0.0"
   ```

3. **Create matching tag**:
   ```bash
   git tag -a v1.0.0 -m "Release v1.0.0"
   git push origin master
   git push origin v1.0.0
   ```

### Branch Strategy

- **main/master**: Stable releases
- **develop**: Development with next version
- **feature/***: Feature branches
- **release/***: Release preparation

### Version Batching

For multiple changes in one release:

1. Accumulate changes
2. Update version once
3. Create single release
4. Tag with appropriate version

## Future Enhancements

### Planned Improvements

- [x] Automatic patch version bumping (`make bump-patch` - ✅ Implemented)
- [ ] Automatic minor/major bumping based on conventional commits
- [ ] Integration with package managers (Homebrew, APT, etc.)
- [ ] Version validation in CI/CD pipeline
- [ ] Automatic CHANGELOG generation
- [ ] Version comparison utilities

### Additional Automation Ideas

```bash
# Automatic version bump based on commit messages (analyze commits for breaking changes)
make auto-bump-version

# Bump minor version
make bump-minor

# Bump major version
make bump-major

# Validate version format
make validate-version

# Generate changelog since last version
make changelog
```

## Security Considerations

### Version Security

- VERSION file is plain text (no security risk)
- Version strings are not used for security decisions
- Build timestamps don't include sensitive information

### Supply Chain Security

- Version is reproducible (same VERSION → same binary)
- Version file is tracked in git (audit trail)
- No external dependencies for version management

## Troubleshooting

### Build Failures

#### Windows Build Issues
- Ensure vcpkg dependencies are properly installed
- Check CMake configuration for MSVC compatibility
- Verify Visual Studio Build Tools are available

#### macOS Build Issues
- Ensure Xcode Command Line Tools are installed
- Check Homebrew packages are up to date
- Verify code signing requirements (if any)

#### Linux Build Issues
- Check static linking flags
- Verify all dependencies are available
- Test on multiple distributions if possible

### Release Issues

#### Missing Assets
- Check GitHub Actions logs for build failures
- Verify artifact upload steps completed
- Check file paths and naming conventions

#### Version Conflicts
- Ensure tags are properly formatted (vX.Y.Z)
- Check for existing tags with same version
- Verify semantic versioning compliance

## Manual Release Process

If automated releases fail, you can create releases manually:

### 1. Build Binaries Locally
```bash
# Linux
make clean
make CFLAGS="-O3 -DNDEBUG -static"

# macOS
make clean
make

# Windows (in MSVC environment)
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . --config Release
```

### 2. Create Archives
```bash
# Linux
mkdir claude-c-linux-x86_64
cp build/claude-c claude-c-linux-x86_64/
cp README.md LICENSE claude-c-linux-x86_64/
tar -czf claude-c-linux-x86_64.tar.gz claude-c-linux-x86_64/

# Similar for other platforms
```

### 3. Create GitHub Release
1. Go to **Releases** page
2. Click **Create a new release**
3. Enter tag and title
4. Upload archives
5. Write release notes
6. Publish release

## Security Considerations

### Binary Security
- Binaries are built on GitHub's infrastructure
- Source code is publicly available for audit
- No proprietary binaries included
- Dependencies are from trusted sources

### Supply Chain Security
- GitHub Actions uses pinned action versions
- Dependencies are from official repositories
- Docker images use minimal base images
- No root privileges in Docker containers

### API Security
- API keys are not included in binaries
- Configuration via environment variables only
- No hardcoded credentials
- Secure communication with APIs

## Contributing to Releases

### Testing Pre-releases
- Download and test pre-release binaries
- Report issues on GitHub Issues
- Test on your specific platform/configuration
- Provide feedback on installation process

### Release Process Improvements
- Suggest improvements to the workflow
- Add support for new platforms
- Improve documentation
- Add automated tests

## Future Enhancements

### Planned Improvements
- [ ] Automatic package manager publishing (Homebrew, APT, etc.)
- [ ] Code signing for binaries
- [ ] Automated security scanning
- [ ] Performance benchmarking
- [ ] More architecture support (ARM64, etc.)

### Platform Support
- [ ] FreeBSD binaries
- [ ] ARM64 binaries for all platforms
- [ ] musl-based Alpine Linux binaries
- [ ] Embedded Linux variants

### Distribution
- [ ] Snap packages
- [ ] Flatpak packages
- [ ] Chocolatey packages (Windows)
- [ ] Winget packages (Windows)