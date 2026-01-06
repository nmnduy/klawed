# OCR Skills

This directory contains scripts for reliable document parsing using OCR (Optical Character Recognition).

## Available Scripts

### 1. `ocr_tesseract.sh`
Uses Tesseract OCR to extract text from images and PDFs.
- Supports: PNG, JPG, JPEG, TIFF, PDF
- Output: Plain text
- Dependencies: tesseract

### 2. `ocr_pdf_text.sh`
Extracts embedded text from PDFs (no OCR needed for text-based PDFs).
- Supports: PDF
- Output: Plain text
- Dependencies: pdftotext (poppler-utils)

### 3. `ocr_combined.sh`
Intelligent OCR that tries embedded text extraction first, falls back to OCR.
- Supports: PDF, images
- Output: Plain text
- Dependencies: pdftotext, tesseract, imagemagick

### 4. `ocr_to_json.sh`
Extracts text and provides structured JSON output with confidence scores.
- Supports: PDF, images
- Output: JSON format
- Dependencies: tesseract with hocr support

### 5. `preprocess_image.sh`
Preprocesses images for better OCR accuracy (deskew, denoise, contrast).
- Supports: PNG, JPG, JPEG, TIFF
- Output: Preprocessed image
- Dependencies: imagemagick

## Installation

### macOS
```bash
brew install tesseract poppler imagemagick
brew install tesseract-lang  # Optional: for multi-language support
```

### Ubuntu/Debian
```bash
sudo apt-get install tesseract-ocr poppler-utils imagemagick
sudo apt-get install tesseract-ocr-eng  # English language pack
```

## Usage Examples

```bash
# Basic OCR
./ocr_tesseract.sh invoice.png

# Extract text from PDF
./ocr_pdf_text.sh invoice.pdf

# Smart combined approach
./ocr_combined.sh document.pdf

# Get structured JSON output
./ocr_to_json.sh invoice.jpg

# Preprocess before OCR
./preprocess_image.sh noisy_scan.jpg cleaned.jpg
./ocr_tesseract.sh cleaned.jpg
```

## Environment Variables

- `TESSDATA_PREFIX`: Path to tessdata directory (language files)
- `OCR_LANG`: Language for OCR (default: eng)
- `OCR_DPI`: DPI for PDF to image conversion (default: 300)
- `OCR_PSM`: Page segmentation mode (default: 3)

## Exit Codes

- 0: Success
- 1: Invalid arguments
- 2: Missing dependencies
- 3: File not found
- 4: OCR failed
- 5: Preprocessing failed
