# OCR (Optical Character Recognition)

## Recommended Tool: Tesseract

Tesseract is a powerful, open-source OCR engine maintained by Google. It has improved significantly and provides excellent results for most use cases.

## Installation

### Ubuntu/Debian
```bash
sudo apt install tesseract-ocr
# For additional language packs:
sudo apt install tesseract-ocr-<lang>  # e.g., tesseract-ocr-deu for German
```

### macOS
```bash
brew install tesseract
```

### Arch Linux
```bash
sudo pacman -S tesseract tesseract-data-eng
```

## Basic Usage

```bash
# Basic image to text
tesseract image.png output

# Output to stdout
tesseract image.png stdout

# Specify output format (txt, pdf, hocr, tsv)
tesseract image.png output pdf

# Specify language
tesseract image.png output -l eng

# Multiple languages
tesseract image.png output -l eng+deu
```

## Improving Accuracy

### Page Segmentation Modes (--psm)
```bash
tesseract image.png output --psm 6
```

| Mode | Description |
|------|-------------|
| 0 | Orientation and script detection (OSD) only |
| 1 | Automatic page segmentation with OSD |
| 3 | Fully automatic page segmentation (default) |
| 4 | Assume a single column of text |
| 6 | Assume a single uniform block of text |
| 7 | Treat the image as a single text line |
| 8 | Treat the image as a single word |
| 11 | Sparse text - find as much text as possible |
| 13 | Raw line - treat as single line, no Tesseract-specific processing |

### OCR Engine Modes (--oem)
```bash
tesseract image.png output --oem 1
```

| Mode | Description |
|------|-------------|
| 0 | Legacy engine only |
| 1 | Neural nets LSTM engine only (best accuracy) |
| 2 | Legacy + LSTM engines |
| 3 | Default, based on what is available |

## Image Preprocessing Tips

For best results, preprocess images before OCR:

```bash
# Using ImageMagick to improve image quality
convert input.png -resize 300% -type Grayscale -sharpen 0x1 output.png

# Increase contrast
convert input.png -contrast-stretch 5%x5% output.png

# Binarization (black and white)
convert input.png -threshold 50% output.png
```

## Python Integration

```python
# Using pytesseract
import pytesseract
from PIL import Image

# Basic usage
text = pytesseract.image_to_string(Image.open('image.png'))

# With options
text = pytesseract.image_to_string(
    Image.open('image.png'),
    lang='eng',
    config='--psm 6 --oem 1'
)

# Get bounding boxes
boxes = pytesseract.image_to_boxes(Image.open('image.png'))

# Get detailed data as dict
data = pytesseract.image_to_data(Image.open('image.png'), output_type=pytesseract.Output.DICT)
```

Install pytesseract:
```bash
pip install pytesseract pillow
```

## Common Issues & Solutions

| Issue | Solution |
|-------|----------|
| Low accuracy | Preprocess image (resize, sharpen, binarize) |
| Wrong orientation | Use `--psm 0` to detect, or preprocess to fix |
| Partial text detected | Try different `--psm` modes |
| Special characters missing | Ensure correct language pack installed |
| Slow processing | Use `--oem 1` (LSTM only) for speed |

## Alternatives

- **EasyOCR** - Python library, good for multiple languages, GPU support
- **PaddleOCR** - High accuracy, especially for Asian languages
- **ocrmypdf** - Adds OCR text layer to PDFs
- **GOCR** - Lightweight alternative
