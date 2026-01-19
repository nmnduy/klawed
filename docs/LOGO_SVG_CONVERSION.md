# Logo SVG Conversion

## Problem
The original PNG logo files only showed black/dark lines when viewed, and the orange colored parts were not visible in SVG conversions.

## Solution
Used image processing and tracing to extract and preserve both the black lines AND the orange colored parts from the original PNG logo.

## Process

### 1. Color Analysis
First, analyzed the original PNG to identify the color components:
- **Dark/Black parts**: RGB(36, 48, 57) → `#243139` (document lines, text)
- **Orange parts**: RGB(234, 128, 41) → `#EA8029` (accent highlights, cursor)

### 2. Color Separation
Used ImageMagick to separate the colors:
```bash
# Extract orange parts with 15% fuzz tolerance
convert filesurf-logo-320x320.png -fuzz 15% -fill black +opaque "#EA8029" -fill white -opaque "#EA8029" orange-mask.png

# Extract dark parts using threshold
convert filesurf-logo-320x320.png -channel RGB -threshold 40% -negate black-parts.png
```

### 3. Bitmap Conversion
Converted to high-contrast black/white bitmaps for tracing:
```bash
convert orange-clean.png -alpha extract -negate -threshold 50% orange-bw.pbm
convert dark-clean.png -alpha extract -negate -threshold 50% dark-bw.pbm
```

### 4. Vector Tracing
Used potrace to convert bitmaps to SVG paths:
```bash
potrace -s -k 0.5 -t 3 -o dark-traced-v2.svg dark-bw.pbm
potrace -s -k 0.5 -t 3 -o orange-traced-v2.svg orange-bw.pbm
```

### 5. SVG Combination
Combined both traced SVGs into a single file with proper colors:
- Orange layer (`#EA8029`): Main logo structure, highlights, cursor
- Dark layer (`#243139`): Text lines, document lines overlay

## Result

### Files Created
- **filesurf-logo.svg** (3.2KB) - Main SVG logo with both colors
- **filesurf-logo-preview.png** (14KB) - Preview render
- **filesurf-logo-500.png** (53KB) - 500x500 PNG render
- **filesurf-logo-1500.png** (360KB) - 1500x1500 PNG render

### Colors Used
- `#EA8029` - Orange (primary logo color, highlights)
- `#243139` - Dark gray/blue (text, lines)

### Updated Templates
- `src/main/resources/templates/index.html` - Landing page header and footer
- `src/main/resources/templates/partials/chatHeader.html` - Chat interface header

## Benefits
1. ✅ **Vector format**: Scales perfectly at any size
2. ✅ **Both colors preserved**: Orange and dark gray/blue visible
3. ✅ **Small file size**: 3.2KB vs 23KB-284KB for PNGs
4. ✅ **Sharp rendering**: No pixelation at any zoom level
5. ✅ **Easy color customization**: Can change colors by editing fill attributes

## Tools Used
- **ImageMagick** (`convert`) - Color extraction and separation
- **potrace** - Bitmap to SVG path tracing
- **Manual SVG editing** - Combining layers and setting colors

## Testing
To verify the logo displays correctly:
1. Start Quarkus dev mode: `mvn quarkus:dev`
2. Open http://localhost:8080
3. Check landing page header/footer and file-chat header
4. Verify both orange and dark colors are visible
5. Try dark mode to ensure contrast works

## Original Files
The original PNG files are retained for reference and as favicons:
- `filesurf-logo-152x152.png` (23KB) - Still used as favicon
- `filesurf-logo-320x320.png` (97KB)
- `filesurf-logo-500x500.png` (260KB)
- `filesurf-logo-1500x500.png` (284KB)
