#!/bin/bash
# Verify blog post publication status
# Usage: ./scripts/verify-blog-post.sh <slug>

set -e

# Colors
GREEN='\033[0;32m'
BLUE='\033[0;34m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
NC='\033[0m'

log_info() { echo -e "${BLUE}ℹ${NC} $1"; }
log_success() { echo -e "${GREEN}✓${NC} $1"; }
log_warn() { echo -e "${YELLOW}⚠${NC} $1"; }
log_error() { echo -e "${RED}✗${NC} $1"; }

if [ $# -lt 1 ]; then
    echo "Usage: $0 <post-slug>"
    exit 1
fi

POST_SLUG="$1"
DB_PATH="data/blog.db"

echo ""
echo "=========================================="
echo "  Blog Post Verification"
echo "=========================================="
echo ""

# Check database
if [ ! -f "$DB_PATH" ]; then
    log_error "Database not found: $DB_PATH"
    exit 1
fi
log_success "Database found"

# Check post exists
POST_ID=$(sqlite3 "$DB_PATH" "SELECT id FROM blog_posts WHERE slug = '$POST_SLUG';" 2>/dev/null || echo "")
if [ -z "$POST_ID" ]; then
    log_error "Blog post not found with slug: $POST_SLUG"
    exit 1
fi
log_success "Blog post found (ID: $POST_ID)"

# Get post details
POST_DATA=$(sqlite3 "$DB_PATH" "SELECT title, status, reading_time_minutes, views, published_at FROM blog_posts WHERE slug = '$POST_SLUG';" 2>/dev/null)
TITLE=$(echo "$POST_DATA" | cut -d'|' -f1)
STATUS=$(echo "$POST_DATA" | cut -d'|' -f2)
READING_TIME=$(echo "$POST_DATA" | cut -d'|' -f3)
VIEWS=$(echo "$POST_DATA" | cut -d'|' -f4)
PUBLISHED_AT=$(echo "$POST_DATA" | cut -d'|' -f5)

echo ""
echo "Post Details:"
echo "  Title: $TITLE"
echo "  Slug: $POST_SLUG"
echo "  Status: $STATUS"
echo "  Reading Time: $READING_TIME minutes"
echo "  Views: $VIEWS"
echo "  Published: $PUBLISHED_AT"
echo ""

# Check status
if [ "$STATUS" = "published" ]; then
    log_success "Status is 'published' ✓"
else
    log_warn "Status is '$STATUS' (not published)"
fi

# Check featured image
FEATURED_IMAGE=$(sqlite3 "$DB_PATH" "SELECT featured_image_url FROM blog_posts WHERE slug = '$POST_SLUG';" 2>/dev/null)
if [ -n "$FEATURED_IMAGE" ]; then
    log_success "Featured image set: $FEATURED_IMAGE"
    
    # Check if file exists
    IMAGE_PATH="src/main/resources/META-INF/resources${FEATURED_IMAGE}"
    if [ -f "$IMAGE_PATH" ]; then
        log_success "Featured image file exists"
    else
        log_error "Featured image file not found: $IMAGE_PATH"
    fi
else
    log_warn "No featured image set"
fi

# Check inline images
CONTENT=$(sqlite3 "$DB_PATH" "SELECT content FROM blog_posts WHERE slug = '$POST_SLUG';" 2>/dev/null)
IMAGE_COUNT=$(echo "$CONTENT" | grep -o "!\[.*\](/assets/blog/" | wc -l)

if [ $IMAGE_COUNT -gt 0 ]; then
    log_success "Found $IMAGE_COUNT inline images in content"
    
    # Check image directory
    IMAGE_DIR="src/main/resources/META-INF/resources/assets/blog/$POST_SLUG"
    if [ -d "$IMAGE_DIR" ]; then
        FILE_COUNT=$(ls "$IMAGE_DIR" | wc -l)
        log_success "Image directory exists with $FILE_COUNT files"
    else
        log_error "Image directory not found: $IMAGE_DIR"
    fi
else
    log_warn "No inline images found in content"
fi

# Check tags
TAG_COUNT=$(sqlite3 "$DB_PATH" "SELECT COUNT(*) FROM post_tags WHERE post_id = $POST_ID;" 2>/dev/null)
if [ $TAG_COUNT -gt 0 ]; then
    log_success "Post has $TAG_COUNT tags"
    TAGS=$(sqlite3 "$DB_PATH" "SELECT t.name FROM tags t JOIN post_tags pt ON t.id = pt.tag_id WHERE pt.post_id = $POST_ID;" 2>/dev/null | tr '\n' ', ' | sed 's/,$//')
    echo "  Tags: $TAGS"
else
    log_warn "No tags associated with post"
fi

echo ""
echo "=========================================="
if [ "$STATUS" = "published" ]; then
    echo ""
    log_success "Blog post is ready!"
    echo ""
    echo "View at:"
    echo "  Local:      http://localhost:9090/blog/$POST_SLUG"
    echo "  Production: https://filesurf.io/blog/$POST_SLUG"
    echo ""
    echo "To deploy:"
    echo "  1. Test locally:  mvn quarkus:dev"
    echo "  2. Deploy DB:     ./scripts/deploy-blog.sh"
    echo "  3. Deploy app:    ./deployment/deploy-rsync.sh"
else
    echo ""
    log_warn "Blog post is in '$STATUS' state"
    echo ""
    echo "To publish:"
    echo "  sqlite3 $DB_PATH \"UPDATE blog_posts SET status='published', published_at=CURRENT_TIMESTAMP WHERE id=$POST_ID;\""
fi
echo ""
echo "=========================================="
echo ""
