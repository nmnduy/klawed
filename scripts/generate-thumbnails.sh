#!/bin/bash
# Generate thumbnails for demo videos
# Usage: ./scripts/generate-thumbnails.sh [demo-directory]

set -e

DEMO_DIR="${1:-data/demos}"

if [ ! -d "$DEMO_DIR" ]; then
    echo "Error: Directory $DEMO_DIR does not exist"
    exit 1
fi

if ! command -v ffmpeg &> /dev/null; then
    echo "Error: ffmpeg is not installed"
    echo "Install with: sudo apt-get install ffmpeg"
    exit 1
fi

echo "Generating thumbnails for videos in: $DEMO_DIR"
echo ""

count=0
for video in "$DEMO_DIR"/*.mp4; do
    if [ ! -f "$video" ]; then
        continue
    fi

    name="${video%.mp4}"
    thumb="${name}.jpg"

    if [ -f "$thumb" ]; then
        echo "⏭️  Skipping (thumbnail exists): $(basename "$video")"
        continue
    fi

    echo "🎬 Processing: $(basename "$video")"

    # Extract frame at 5 seconds (or 10% into the video, whichever is smaller)
    # Use high quality JPEG (q:v 2 = very high quality)
    if ffmpeg -i "$video" -ss 00:00:05 -vframes 1 -q:v 2 "$thumb" -y &>/dev/null; then
        size=$(du -h "$thumb" | cut -f1)
        echo "   ✓ Created: $(basename "$thumb") (${size})"
        ((count++))
    else
        echo "   ✗ Failed to generate thumbnail"
    fi
done

echo ""
if [ $count -eq 0 ]; then
    echo "No new thumbnails generated (all exist already)"
else
    echo "✅ Successfully generated $count thumbnail(s)"
fi
