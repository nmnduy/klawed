#!/bin/bash
# Script to prepare blog post images for publication
# Updates markdown image paths to use /assets/blog/{post-slug}/ URLs

set -e

# Configuration
BLOG_POST_DIR="filesurf-demo"
POST_SLUG="filesurf-demo"
ASSETS_BASE="/assets/blog"

# Colors for output
GREEN='\033[0;32m'
BLUE='\033[0;34m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

log_info() {
    echo -e "${BLUE}ℹ${NC} $1"
}

log_success() {
    echo -e "${GREEN}✓${NC} $1"
}

log_warn() {
    echo -e "${YELLOW}⚠${NC} $1"
}

# Check if source blog post exists
if [ ! -f "$BLOG_POST_DIR/BLOG_POST.md" ]; then
    echo "Error: $BLOG_POST_DIR/BLOG_POST.md not found"
    exit 1
fi

log_info "Preparing blog post: $POST_SLUG"

# Create output file
OUTPUT_FILE="$BLOG_POST_DIR/BLOG_POST_WEB.md"

log_info "Updating image paths..."

# Update image paths in markdown
# Change: ![Alt](image.png) -> ![Alt](/assets/blog/filesurf-demo/image.png)
sed -E "s|!\[([^]]*)\]\(([0-9][0-9]-[^)]+\.png)\)|![\1]($ASSETS_BASE/$POST_SLUG/\2)|g" \
    "$BLOG_POST_DIR/BLOG_POST.md" > "$OUTPUT_FILE"

log_success "Created web-ready blog post: $OUTPUT_FILE"

# Count images
IMAGE_COUNT=$(grep -o "!\[.*\]($ASSETS_BASE" "$OUTPUT_FILE" | wc -l)
log_success "Updated $IMAGE_COUNT image paths"

# Verify all images exist
log_info "Verifying image files..."
MISSING_COUNT=0

while IFS= read -r line; do
    # Extract image filename from markdown
    PATTERN="!\[.*\]\($ASSETS_BASE/$POST_SLUG/([^)]+)\)"
    if [[ $line =~ $PATTERN ]]; then
        IMAGE_FILE="${BASH_REMATCH[1]}"
        FULL_PATH="src/main/resources/META-INF/resources$ASSETS_BASE/$POST_SLUG/$IMAGE_FILE"
        
        if [ ! -f "$FULL_PATH" ]; then
            log_warn "Missing image: $IMAGE_FILE"
            MISSING_COUNT=$((MISSING_COUNT + 1))
        fi
    fi
done < "$OUTPUT_FILE"

if [ $MISSING_COUNT -eq 0 ]; then
    log_success "All images verified ✓"
else
    log_warn "Found $MISSING_COUNT missing images"
fi

echo ""
log_success "Blog post ready for publication!"
echo ""
echo "Next steps:"
echo "  1. Review: $OUTPUT_FILE"
echo "  2. Run: ./scripts/publish-blog-post.sh $POST_SLUG"
echo ""
