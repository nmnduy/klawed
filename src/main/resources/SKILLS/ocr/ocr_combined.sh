#!/usr/bin/env bash
#
# Smart OCR - tries embedded text extraction first, falls back to OCR
# This is the recommended script for general-purpose document processing
#
# Usage: ./ocr_combined.sh <input_file> [output_file] [language]
#
# Examples:
#   ./ocr_combined.sh invoice.pdf
#   ./ocr_combined.sh document.jpg output.txt
#   ./ocr_combined.sh receipt.pdf output.txt spa
#

set -euo pipefail

# Get the directory where this script is located
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
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

log_debug() {
    echo -e "${BLUE}[DEBUG]${NC} $1" >&2
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

# Try to extract embedded text from PDF
try_pdf_text_extraction() {
    local pdf_file="$1"

    log_info "Attempting to extract embedded text from PDF..."

    if ! command -v pdftotext &> /dev/null; then
        log_warn "pdftotext not found, skipping text extraction"
        return 1
    fi

    local text
    text=$(pdftotext -layout "$pdf_file" - 2>/dev/null || echo "")

    local char_count
    char_count=$(echo "$text" | tr -d '[:space:]' | wc -c | tr -d ' ')

    log_debug "Extracted $char_count non-whitespace characters"

    # If we got meaningful text (more than 100 chars), return it
    if [[ "$char_count" -gt 100 ]]; then
        log_info "Successfully extracted embedded text"
        echo "$text"
        return 0
    else
        log_info "Insufficient embedded text found, will use OCR"
        return 1
    fi
}

# Perform OCR on the document
perform_ocr() {
    local input_file="$1"
    local language="$2"

    log_info "Performing OCR on document..."

    if [[ ! -x "$SCRIPT_DIR/ocr_tesseract.sh" ]]; then
        log_error "ocr_tesseract.sh not found or not executable"
        exit 4
    fi

    "$SCRIPT_DIR/ocr_tesseract.sh" "$input_file" "" "$language"
}

# Main function
main() {
    if [[ $# -lt 1 ]]; then
        echo "Usage: $0 <input_file> [output_file] [language]"
        echo ""
        echo "Arguments:"
        echo "  input_file   : Path to PDF or image file"
        echo "  output_file  : (Optional) Path to output text file (default: stdout)"
        echo "  language     : (Optional) OCR language code (default: eng)"
        echo ""
        echo "This script intelligently chooses between text extraction and OCR:"
        echo "  - For PDFs: tries embedded text first, falls back to OCR"
        echo "  - For images: uses OCR directly"
        echo ""
        echo "Supported formats: PDF, PNG, JPG, JPEG, TIFF"
        exit 1
    fi

    local input_file="$1"
    local output_file="${2:-}"
    local language="${3:-eng}"

    check_file "$input_file"

    local extension
    extension=$(get_extension "$input_file")

    log_info "Processing file: $(basename "$input_file")"
    log_info "File type: $extension"

    local extracted_text=""

    # Strategy depends on file type
    if [[ "$extension" == "pdf" ]]; then
        # Try embedded text first for PDFs
        if extracted_text=$(try_pdf_text_extraction "$input_file"); then
            log_info "Using embedded text from PDF"
        else
            # Fall back to OCR
            log_info "Falling back to OCR for PDF"
            extracted_text=$(perform_ocr "$input_file" "$language")
        fi
    else
        # For images, go straight to OCR
        log_info "Image file detected, using OCR"
        extracted_text=$(perform_ocr "$input_file" "$language")
    fi

    # Output results
    if [[ -n "$output_file" ]]; then
        echo "$extracted_text" > "$output_file"
        log_info "Text extraction completed successfully"
        log_info "Output saved to: $output_file"
        log_info "Total characters: $(echo "$extracted_text" | wc -c | tr -d ' ')"
    else
        echo "$extracted_text"
    fi

    exit 0
}

main "$@"
