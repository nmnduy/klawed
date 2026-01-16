#!/usr/bin/env bash
#
# OCR using Tesseract
# Extracts text from images and PDFs using Tesseract OCR
#
# Usage: ./ocr_tesseract.sh <input_file> [output_file] [language]
#
# Examples:
#   ./ocr_tesseract.sh invoice.png
#   ./ocr_tesseract.sh document.pdf output.txt
#   ./ocr_tesseract.sh receipt.jpg output.txt spa
#

set -euo pipefail

# Default configuration
OCR_LANG="${OCR_LANG:-eng}"
OCR_DPI="${OCR_DPI:-300}"
OCR_PSM="${OCR_PSM:-3}"  # Fully automatic page segmentation

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Function to print colored messages
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
    local missing_deps=()

    if ! command -v tesseract &> /dev/null; then
        missing_deps+=("tesseract")
    fi

    if [[ "${#missing_deps[@]}" -gt 0 ]]; then
        log_error "Missing dependencies: ${missing_deps[*]}"
        log_error "Install with: brew install ${missing_deps[*]} (macOS)"
        log_error "Or: apt-get install tesseract-ocr (Linux)"
        exit 2
    fi
}

# Check if file exists and is readable
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

# Get file extension
get_extension() {
    local filename="$1"
    echo "${filename##*.}" | tr '[:upper:]' '[:lower:]'
}

# Convert PDF to images if needed
convert_pdf_to_image() {
    local pdf_file="$1"
    local output_dir="$2"

    log_info "Converting PDF to images at ${OCR_DPI} DPI..."

    # Check for conversion tools
    if command -v pdftoppm &> /dev/null; then
        pdftoppm -png -r "$OCR_DPI" "$pdf_file" "$output_dir/page"
    elif command -v convert &> /dev/null; then
        convert -density "$OCR_DPI" "$pdf_file" "$output_dir/page.png"
    else
        log_error "No PDF conversion tool found. Install poppler-utils or imagemagick."
        exit 2
    fi

    # Return the list of generated images
    find "$output_dir" -name "page*.png" | sort
}

# Perform OCR on a single image
ocr_image() {
    local image_file="$1"
    local language="$2"

    log_info "Processing: $(basename "$image_file")"

    # Run tesseract
    tesseract "$image_file" stdout \
        -l "$language" \
        --psm "$OCR_PSM" \
        --dpi "$OCR_DPI" \
        2>/dev/null || {
        log_error "Tesseract OCR failed for $image_file"
        return 4
    }
}

# Main function
main() {
    if [[ $# -lt 1 ]]; then
        echo "Usage: $0 <input_file> [output_file] [language]"
        echo ""
        echo "Arguments:"
        echo "  input_file   : Path to image or PDF file"
        echo "  output_file  : (Optional) Path to output text file (default: stdout)"
        echo "  language     : (Optional) OCR language code (default: eng)"
        echo ""
        echo "Supported formats: PNG, JPG, JPEG, TIFF, PDF"
        echo "Common languages: eng (English), spa (Spanish), fra (French), deu (German)"
        exit 1
    fi

    local input_file="$1"
    local output_file="${2:-}"
    local language="${3:-$OCR_LANG}"

    # Check dependencies and input file
    check_dependencies
    check_file "$input_file"

    local extension
    extension=$(get_extension "$input_file")

    log_info "Starting OCR with Tesseract..."
    log_info "Language: $language, DPI: $OCR_DPI, PSM: $OCR_PSM"

    local ocr_output=""

    # Handle PDF files
    if [[ "$extension" == "pdf" ]]; then
        local temp_dir
        temp_dir=$(mktemp -d)
        trap 'rm -rf "$temp_dir"' EXIT

        # Convert PDF to images
        local images
        images=$(convert_pdf_to_image "$input_file" "$temp_dir")

        # OCR each page
        while IFS= read -r image; do
            ocr_output+=$(ocr_image "$image" "$language")
            ocr_output+=$'\n\n'
        done <<< "$images"
    else
        # Direct OCR for image files
        ocr_output=$(ocr_image "$input_file" "$language")
    fi

    # Output results
    if [[ -n "$output_file" ]]; then
        echo "$ocr_output" > "$output_file"
        log_info "OCR completed successfully. Output saved to: $output_file"
        log_info "Characters extracted: $(echo "$ocr_output" | wc -c)"
    else
        echo "$ocr_output"
    fi

    exit 0
}

main "$@"
