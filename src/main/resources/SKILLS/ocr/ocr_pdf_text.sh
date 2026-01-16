#!/usr/bin/env bash
#
# Extract embedded text from PDFs (no OCR)
# This is much faster than OCR and should be tried first for text-based PDFs
#
# Usage: ./ocr_pdf_text.sh <pdf_file> [output_file]
#
# Examples:
#   ./ocr_pdf_text.sh invoice.pdf
#   ./ocr_pdf_text.sh document.pdf output.txt
#

set -euo pipefail

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

    if ! command -v pdftotext &> /dev/null; then
        missing_deps+=("poppler-utils")
    fi

    if [[ "${#missing_deps[@]}" -gt 0 ]]; then
        log_error "Missing dependencies: ${missing_deps[*]}"
        log_error "Install with: brew install poppler (macOS)"
        log_error "Or: apt-get install poppler-utils (Linux)"
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

    # Check if it's a PDF
    if ! file "$file" | grep -q "PDF"; then
        log_error "File is not a PDF: $file"
        exit 3
    fi
}

# Main function
main() {
    if [[ $# -lt 1 ]]; then
        echo "Usage: $0 <pdf_file> [output_file]"
        echo ""
        echo "Arguments:"
        echo "  pdf_file     : Path to PDF file"
        echo "  output_file  : (Optional) Path to output text file (default: stdout)"
        echo ""
        echo "This extracts embedded text from PDFs without OCR."
        echo "For scanned PDFs, use ocr_tesseract.sh or ocr_combined.sh instead."
        exit 1
    fi

    local pdf_file="$1"
    local output_file="${2:-}"

    # Check dependencies and input file
    check_dependencies
    check_file "$pdf_file"

    log_info "Extracting text from PDF..."

    # Extract text
    local extracted_text
    if extracted_text=$(pdftotext -layout "$pdf_file" - 2>&1); then
        local char_count
        char_count=$(echo "$extracted_text" | wc -c | tr -d ' ')

        # Check if we got meaningful text
        if [[ "$char_count" -lt 50 ]]; then
            log_warn "Very little text extracted ($char_count characters)"
            log_warn "This might be a scanned PDF requiring OCR"
            log_warn "Try using: ocr_combined.sh $pdf_file"
        else
            log_info "Successfully extracted $char_count characters"
        fi

        # Output results
        if [[ -n "$output_file" ]]; then
            echo "$extracted_text" > "$output_file"
            log_info "Text saved to: $output_file"
        else
            echo "$extracted_text"
        fi
    else
        log_error "Failed to extract text from PDF"
        log_error "$extracted_text"
        exit 4
    fi

    exit 0
}

main "$@"
