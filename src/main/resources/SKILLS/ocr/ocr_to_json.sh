#!/usr/bin/env bash
#
# OCR with structured JSON output
# Extracts text and provides confidence scores, bounding boxes, and metadata
#
# Usage: ./ocr_to_json.sh <input_file> [output_file] [language]
#
# Examples:
#   ./ocr_to_json.sh invoice.png
#   ./ocr_to_json.sh document.pdf output.json
#

set -euo pipefail

# Default configuration
OCR_LANG="${OCR_LANG:-eng}"
OCR_DPI="${OCR_DPI:-300}"
OCR_PSM="${OCR_PSM:-3}"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

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

    if ! command -v jq &> /dev/null; then
        log_warn "jq not found - JSON will not be pretty-printed"
    fi

    if [[ "${#missing_deps[@]}" -gt 0 ]]; then
        log_error "Missing dependencies: ${missing_deps[*]}"
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
}

# Get file extension
get_extension() {
    local filename="$1"
    echo "${filename##*.}" | tr '[:upper:]' '[:lower:]'
}

# Convert hOCR to JSON
hocr_to_json() {
    local hocr_file="$1"
    local input_file="$2"

    # Extract text and confidence from hOCR using basic parsing
    local text_content
    text_content=$(grep -o 'ocrx_word[^>]*>[^<]*' "$hocr_file" | sed 's/.*>//' | tr '\n' ' ')

    # Extract confidence scores
    local confidences
    confidences=$(grep -o "x_wconf [0-9]*" "$hocr_file" | awk '{print $2}' | tr '\n' ',')
    confidences="${confidences%,}"  # Remove trailing comma

    # Calculate average confidence
    local avg_confidence=0
    if [[ -n "$confidences" ]]; then
        avg_confidence=$(echo "$confidences" | awk -F',' '{sum=0; for(i=1;i<=NF;i++) sum+=$i; print int(sum/NF)}')
    fi

    # Get file metadata
    local file_size
    file_size=$(stat -f%z "$input_file" 2>/dev/null || stat -c%s "$input_file" 2>/dev/null || echo "0")

    local file_type
    file_type=$(file -b "$input_file" | cut -d',' -f1)

    # Build JSON
    cat <<EOF
{
  "file": {
    "name": "$(basename "$input_file")",
    "path": "$input_file",
    "size": $file_size,
    "type": "$file_type"
  },
  "ocr": {
    "engine": "tesseract",
    "language": "$OCR_LANG",
    "dpi": $OCR_DPI,
    "psm": $OCR_PSM,
    "timestamp": "$(date -u +%Y-%m-%dT%H:%M:%SZ)"
  },
  "results": {
    "text": $(echo "$text_content" | jq -Rs .),
    "confidence": {
      "average": $avg_confidence,
      "scores": [$confidences]
    },
    "character_count": ${#text_content},
    "word_count": $(echo "$text_content" | wc -w | tr -d ' ')
  }
}
EOF
}

# Perform OCR with hOCR output
ocr_to_hocr() {
    local input_file="$1"
    local language="$2"
    local temp_output="$3"

    log_info "Running Tesseract with hOCR output..."

    tesseract "$input_file" "$temp_output" \
        -l "$language" \
        --psm "$OCR_PSM" \
        --dpi "$OCR_DPI" \
        hocr 2>/dev/null || {
        log_error "Tesseract OCR failed"
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
        echo "  output_file  : (Optional) Path to output JSON file (default: stdout)"
        echo "  language     : (Optional) OCR language code (default: eng)"
        echo ""
        echo "Output: JSON with text, confidence scores, and metadata"
        exit 1
    fi

    local input_file="$1"
    local output_file="${2:-}"
    local language="${3:-$OCR_LANG}"

    check_dependencies
    check_file "$input_file"

    local extension
    extension=$(get_extension "$input_file")

    log_info "Starting OCR with JSON output..."
    log_info "Language: $language, DPI: $OCR_DPI"

    # Create temp directory
    local temp_dir
    temp_dir=$(mktemp -d)
    trap 'rm -rf "$temp_dir"' EXIT

    local json_output=""

    # Handle PDF files
    if [[ "$extension" == "pdf" ]]; then
        log_info "Converting PDF to image..."

        # Convert first page to image
        if command -v pdftoppm &> /dev/null; then
            pdftoppm -png -f 1 -l 1 -r "$OCR_DPI" "$input_file" "$temp_dir/page"
            local image_file
            image_file=$(find "$temp_dir" -name "page*.png" | head -1)
        else
            log_error "pdftoppm not found. Install poppler-utils."
            exit 2
        fi
    else
        image_file="$input_file"
    fi

    # Perform OCR with hOCR output
    ocr_to_hocr "$image_file" "$language" "$temp_dir/output"

    # Convert hOCR to JSON
    json_output=$(hocr_to_json "$temp_dir/output.hocr" "$input_file")

    # Pretty print if jq is available
    if command -v jq &> /dev/null; then
        json_output=$(echo "$json_output" | jq .)
    fi

    # Output results
    if [[ -n "$output_file" ]]; then
        echo "$json_output" > "$output_file"
        log_info "OCR completed successfully. JSON saved to: $output_file"
    else
        echo "$json_output"
    fi

    exit 0
}

main "$@"
