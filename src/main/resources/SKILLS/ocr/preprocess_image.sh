#!/usr/bin/env bash
#
# Image preprocessing for better OCR accuracy
# Applies deskewing, denoising, contrast enhancement, and sharpening
#
# Usage: ./preprocess_image.sh <input_image> <output_image> [options]
#
# Examples:
#   ./preprocess_image.sh scan.jpg cleaned.jpg
#   ./preprocess_image.sh photo.png output.png --aggressive
#

set -euo pipefail

# Default configuration
DESKEW="${DESKEW:-true}"
DENOISE="${DENOISE:-true}"
ENHANCE_CONTRAST="${ENHANCE_CONTRAST:-true}"
SHARPEN="${SHARPEN:-true}"
CONVERT_GRAYSCALE="${CONVERT_GRAYSCALE:-true}"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

log_info() {
    echo -e "${GREEN}[INFO]${NC} $1" >&2
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1" >&2
}

log_warn() {
    echo -e "${YELLOW}[WARN]${NC} $1" >&2
}

# Check dependencies
check_dependencies() {
    if ! command -v convert &> /dev/null; then
        log_error "ImageMagick 'convert' command not found"
        log_error "Install with: brew install imagemagick (macOS)"
        log_error "Or: apt-get install imagemagick (Linux)"
        exit 2
    fi
}

# Check file
check_file() {
    local file="$1"

    if [[ ! -f "$file" ]]; then
        log_error "File not found: $file"
        exit 3
    fi

    if [[ ! -r "$file" ]]; then
        log_error "File not readable: $file"
        exit 3
    fi
}

# Preprocess image
preprocess_image() {
    local input_file="$1"
    local output_file="$2"
    local aggressive="${3:-false}"

    log_info "Starting image preprocessing..."
    log_info "Input: $(basename "$input_file")"
    log_info "Output: $(basename "$output_file")"

    # Build ImageMagick command
    local cmd="convert '$input_file'"

    # Convert to grayscale
    if [[ "$CONVERT_GRAYSCALE" == "true" ]]; then
        log_info "Converting to grayscale..."
        cmd="$cmd -colorspace Gray"
    fi

    # Deskew (straighten)
    if [[ "$DESKEW" == "true" ]]; then
        log_info "Applying deskew..."
        cmd="$cmd -deskew 40%"
    fi

    # Denoise
    if [[ "$DENOISE" == "true" ]]; then
        log_info "Applying noise reduction..."
        if [[ "$aggressive" == "true" ]]; then
            cmd="$cmd -despeckle -despeckle"
        else
            cmd="$cmd -despeckle"
        fi
    fi

    # Enhance contrast
    if [[ "$ENHANCE_CONTRAST" == "true" ]]; then
        log_info "Enhancing contrast..."
        if [[ "$aggressive" == "true" ]]; then
            cmd="$cmd -normalize -contrast -contrast"
        else
            cmd="$cmd -normalize -contrast"
        fi
    fi

    # Sharpen
    if [[ "$SHARPEN" == "true" ]]; then
        log_info "Applying sharpening..."
        if [[ "$aggressive" == "true" ]]; then
            cmd="$cmd -sharpen 0x2"
        else
            cmd="$cmd -sharpen 0x1"
        fi
    fi

    # Apply threshold for black and white (good for text)
    if [[ "$aggressive" == "true" ]]; then
        log_info "Applying adaptive threshold..."
        cmd="$cmd -lat 15x15+5%"
    fi

    # Trim whitespace
    cmd="$cmd -trim +repage"

    # Output file
    cmd="$cmd '$output_file'"

    # Execute
    log_info "Executing preprocessing pipeline..."
    if eval "$cmd" 2>/dev/null; then
        log_info "Preprocessing completed successfully"

        # Show file sizes
        local input_size
        local output_size
        input_size=$(stat -f%z "$input_file" 2>/dev/null || stat -c%s "$input_file" 2>/dev/null)
        output_size=$(stat -f%z "$output_file" 2>/dev/null || stat -c%s "$output_file" 2>/dev/null)

        log_info "Input size: $(numfmt --to=iec-i --suffix=B $input_size 2>/dev/null || echo "$input_size bytes")"
        log_info "Output size: $(numfmt --to=iec-i --suffix=B $output_size 2>/dev/null || echo "$output_size bytes")"
    else
        log_error "Preprocessing failed"
        exit 5
    fi
}

# Main function
main() {
    if [[ $# -lt 2 ]]; then
        echo "Usage: $0 <input_image> <output_image> [--aggressive]"
        echo ""
        echo "Arguments:"
        echo "  input_image  : Path to input image file"
        echo "  output_image : Path to output (preprocessed) image file"
        echo "  --aggressive : (Optional) Apply more aggressive preprocessing"
        echo ""
        echo "Preprocessing steps:"
        echo "  - Convert to grayscale"
        echo "  - Deskew (straighten)"
        echo "  - Denoise"
        echo "  - Enhance contrast"
        echo "  - Sharpen"
        echo "  - Trim whitespace"
        echo ""
        echo "For scanned documents with heavy distortion, use --aggressive"
        exit 1
    fi

    local input_file="$1"
    local output_file="$2"
    local aggressive="false"

    if [[ "${3:-}" == "--aggressive" ]]; then
        aggressive="true"
        log_info "Aggressive mode enabled"
    fi

    check_dependencies
    check_file "$input_file"

    # Ensure output directory exists
    local output_dir
    output_dir=$(dirname "$output_file")
    if [[ ! -d "$output_dir" ]]; then
        mkdir -p "$output_dir"
    fi

    preprocess_image "$input_file" "$output_file" "$aggressive"

    exit 0
}

main "$@"
