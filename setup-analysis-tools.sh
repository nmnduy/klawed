#!/bin/bash
# setup-analysis-tools.sh - Install static analysis tools for Klawed

set -e

echo "=========================================="
echo "Setting up analysis tools for Klawed"
echo "=========================================="
echo ""

# Detect OS
if [[ "$(uname)" == "Darwin" ]]; then
    echo "Detected macOS"
    echo ""
    
    # Check if Homebrew is installed
    if ! command -v brew &> /dev/null; then
        echo "Error: Homebrew not found. Please install Homebrew first:"
        echo "  /bin/bash -c \"\$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)\""
        exit 1
    fi
    
    echo "Installing analysis tools via Homebrew..."
    echo ""
    
    # Install LLVM (includes clang-tidy, scan-build)
    if ! brew list llvm &> /dev/null; then
        echo "Installing llvm..."
        brew install llvm
        echo "✓ llvm installed"
    else
        echo "✓ llvm already installed"
    fi
    
    # Install cppcheck
    if ! brew list cppcheck &> /dev/null; then
        echo "Installing cppcheck..."
        brew install cppcheck
        echo "✓ cppcheck installed"
    else
        echo "✓ cppcheck already installed"
    fi
    
    # Install flawfinder
    if ! brew list flawfinder &> /dev/null; then
        echo "Installing flawfinder..."
        brew install flawfinder
        echo "✓ flawfinder installed"
    else
        echo "✓ flawfinder already installed"
    fi
    
    # Create symlinks for LLVM tools if they're not in PATH
    LLVM_BIN="/opt/homebrew/opt/llvm/bin"
    if [ -d "$LLVM_BIN" ]; then
        echo ""
        echo "LLVM tools are installed at: $LLVM_BIN"
        echo "You may want to add this to your PATH or create symlinks:"
        echo ""
        echo "Option 1: Add to PATH in your shell config:"
        echo "  export PATH=\"$LLVM_BIN:\$PATH\""
        echo ""
        echo "Option 2: Create symlinks in /opt/homebrew/bin:"
        echo "  ln -s $LLVM_BIN/clang-tidy /opt/homebrew/bin/ 2>/dev/null || true"
        echo "  ln -s $LLVM_BIN/scan-build /opt/homebrew/bin/ 2>/dev/null || true"
        echo ""
        
        # Check if tools exist in LLVM bin directory
        if [ -f "$LLVM_BIN/clang-tidy" ]; then
            echo "Found clang-tidy at: $LLVM_BIN/clang-tidy"
            # Create symlink if not already in PATH
            if ! command -v clang-tidy &> /dev/null; then
                echo "Creating symlink for clang-tidy..."
                ln -sf "$LLVM_BIN/clang-tidy" /opt/homebrew/bin/
                echo "✓ clang-tidy symlink created"
            else
                echo "✓ clang-tidy already in PATH"
            fi
        fi
        
        if [ -f "$LLVM_BIN/scan-build" ]; then
            echo "Found scan-build at: $LLVM_BIN/scan-build"
            # Create symlink if not already in PATH
            if ! command -v scan-build &> /dev/null; then
                echo "Creating symlink for scan-build..."
                ln -sf "$LLVM_BIN/scan-build" /opt/homebrew/bin/
                echo "✓ scan-build symlink created"
            else
                echo "✓ scan-build already in PATH"
            fi
        fi
    fi
    
elif [[ "$(uname)" == "Linux" ]]; then
    echo "Detected Linux"
    echo ""
    
    # Check package manager
    if command -v apt-get &> /dev/null; then
        echo "Using apt-get (Debian/Ubuntu)"
        echo ""
        echo "Installing analysis tools..."
        sudo apt-get update
        sudo apt-get install -y clang-tidy cppcheck flawfinder valgrind
        echo "✓ Analysis tools installed"
        
    elif command -v yum &> /dev/null; then
        echo "Using yum (RHEL/CentOS/Fedora)"
        echo ""
        echo "Installing analysis tools..."
        sudo yum install -y clang-tools-extra cppcheck flawfinder valgrind
        echo "✓ Analysis tools installed"
        
    elif command -v dnf &> /dev/null; then
        echo "Using dnf (Fedora)"
        echo ""
        echo "Installing analysis tools..."
        sudo dnf install -y clang-tools-extra cppcheck flawfinder valgrind
        echo "✓ Analysis tools installed"
        
    else
        echo "Error: Could not detect package manager"
        echo "Please install manually:"
        echo "  clang-tidy, cppcheck, flawfinder, valgrind"
        exit 1
    fi
    
else
    echo "Error: Unsupported OS: $(uname)"
    exit 1
fi

echo ""
echo "=========================================="
echo "Setup complete!"
echo "=========================================="
echo ""
echo "Available tools:"
command -v clang-tidy &> /dev/null && echo "  ✓ clang-tidy" || echo "  ○ clang-tidy (not found)"
command -v cppcheck &> /dev/null && echo "  ✓ cppcheck" || echo "  ○ cppcheck (not found)"
command -v flawfinder &> /dev/null && echo "  ✓ flawfinder" || echo "  ○ flawfinder (not found)"
command -v scan-build &> /dev/null && echo "  ✓ scan-build" || echo "  ○ scan-build (not found)"
command -v valgrind &> /dev/null && echo "  ✓ valgrind" || echo "  ○ valgrind (not found)"
echo ""
echo "Run comprehensive scan:"
echo "  make comprehensive-scan"
echo ""
echo "Individual analysis commands:"
echo "  make clang-tidy    # Run clang-tidy static analysis"
echo "  make cppcheck      # Run cppcheck security analysis"
echo "  make flawfinder    # Run flawfinder security analysis"
echo "  make analyze       # Run compiler static analysis"
echo "  make sanitize-all  # Build with address + undefined behavior sanitizers"
echo ""