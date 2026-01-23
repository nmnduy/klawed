#!/bin/bash
# Script to publish a blog post to the database
# Usage: ./scripts/publish-blog-post.sh <post-slug> [--draft]

set -e

# Configuration
DB_PATH="data/blog.db"
BLOG_POST_DIR="filesurf-demo"
DEFAULT_AUTHOR_ID=1
DEFAULT_CATEGORY_ID=1

# Colors for output
GREEN='\033[0;32m'
BLUE='\033[0;34m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
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

log_error() {
    echo -e "${RED}✗${NC} $1"
}

# Check arguments
if [ $# -lt 1 ]; then
    echo "Usage: $0 <post-slug> [--draft]"
    echo ""
    echo "Example:"
    echo "  $0 filesurf-demo          # Publish as published"
    echo "  $0 filesurf-demo --draft  # Publish as draft"
    exit 1
fi

POST_SLUG="$1"
STATUS="published"

if [ "$2" = "--draft" ]; then
    STATUS="draft"
fi

# Check if database exists
if [ ! -f "$DB_PATH" ]; then
    log_error "Database not found: $DB_PATH"
    exit 1
fi

# Determine blog post file
if [ -f "$BLOG_POST_DIR/BLOG_POST_WEB.md" ]; then
    BLOG_POST_FILE="$BLOG_POST_DIR/BLOG_POST_WEB.md"
    log_info "Using web-ready version: $BLOG_POST_FILE"
elif [ -f "$BLOG_POST_DIR/BLOG_POST.md" ]; then
    BLOG_POST_FILE="$BLOG_POST_DIR/BLOG_POST.md"
    log_warn "Using original file (run ./scripts/prepare-blog-images.sh first!)"
else
    log_error "Blog post not found in $BLOG_POST_DIR/"
    exit 1
fi

log_info "Publishing blog post: $POST_SLUG"
log_info "Status: $STATUS"

# Extract title (first H1 heading)
TITLE=$(grep -m 1 "^# " "$BLOG_POST_FILE" | sed 's/^# //')
if [ -z "$TITLE" ]; then
    log_error "Could not extract title from blog post"
    exit 1
fi
log_info "Title: $TITLE"

# Extract excerpt (first paragraph after title)
EXCERPT=$(sed -n '/^# /,/^$/p' "$BLOG_POST_FILE" | grep -v "^#" | grep -v "^\*" | grep -v "^---" | grep "^[A-Z]" | head -1)
if [ -z "$EXCERPT" ]; then
    EXCERPT="An in-depth case study of building a complete invoice automation system with FileSurf."
fi
log_info "Excerpt: ${EXCERPT:0:80}..."

# Read full content
CONTENT=$(cat "$BLOG_POST_FILE")

# Calculate reading time (rough estimate: 200 words per minute)
WORD_COUNT=$(echo "$CONTENT" | wc -w)
READING_TIME=$(( (WORD_COUNT + 199) / 200 ))
log_info "Word count: $WORD_COUNT (~$READING_TIME min read)"

# Featured image (first image in content)
FEATURED_IMAGE=$(grep -m 1 "!\[.*\](" "$BLOG_POST_FILE" | sed -E 's/.*\]\(([^)]+)\).*/\1/')
if [ -z "$FEATURED_IMAGE" ]; then
    FEATURED_IMAGE="/assets/blog/$POST_SLUG/01-filesurf-initial-chat-interface.png"
fi
log_info "Featured image: $FEATURED_IMAGE"

# Meta description
META_DESCRIPTION="$EXCERPT"

# Meta keywords
META_KEYWORDS="filesurf, ai automation, invoice generation, google sheets, sqlite, pdf generation, case study"

# Check if post already exists
EXISTING_ID=$(sqlite3 "$DB_PATH" "SELECT id FROM blog_posts WHERE slug = '$POST_SLUG';" 2>/dev/null || echo "")

if [ -n "$EXISTING_ID" ]; then
    log_warn "Blog post with slug '$POST_SLUG' already exists (ID: $EXISTING_ID)"
    read -p "Do you want to update it? (y/N): " -n 1 -r
    echo
    if [[ ! $REPLY =~ ^[Yy]$ ]]; then
        log_info "Cancelled"
        exit 0
    fi
    
    # Update existing post
    log_info "Updating existing post..."
    
    sqlite3 "$DB_PATH" <<EOF
UPDATE blog_posts SET
    title = '$TITLE',
    content = '$(echo "$CONTENT" | sed "s/'/''/g")',
    excerpt = '$(echo "$EXCERPT" | sed "s/'/''/g")',
    featured_image_url = '$FEATURED_IMAGE',
    meta_description = '$(echo "$META_DESCRIPTION" | sed "s/'/''/g")',
    meta_keywords = '$META_KEYWORDS',
    reading_time_minutes = $READING_TIME,
    status = '$STATUS',
    updated_at = CURRENT_TIMESTAMP
WHERE slug = '$POST_SLUG';
EOF
    
    log_success "Blog post updated (ID: $EXISTING_ID)"
    POST_ID=$EXISTING_ID
else
    # Insert new post
    log_info "Creating new blog post..."
    
    PUBLISHED_AT=""
    if [ "$STATUS" = "published" ]; then
        PUBLISHED_AT="CURRENT_TIMESTAMP"
    else
        PUBLISHED_AT="NULL"
    fi
    
    sqlite3 "$DB_PATH" <<EOF
INSERT INTO blog_posts (
    title,
    slug,
    content,
    excerpt,
    author_id,
    category_id,
    featured_image_url,
    meta_description,
    meta_keywords,
    status,
    reading_time_minutes,
    published_at,
    created_at,
    updated_at,
    views
) VALUES (
    '$TITLE',
    '$POST_SLUG',
    '$(echo "$CONTENT" | sed "s/'/''/g")',
    '$(echo "$EXCERPT" | sed "s/'/''/g")',
    $DEFAULT_AUTHOR_ID,
    $DEFAULT_CATEGORY_ID,
    '$FEATURED_IMAGE',
    '$(echo "$META_DESCRIPTION" | sed "s/'/''/g")',
    '$META_KEYWORDS',
    '$STATUS',
    $READING_TIME,
    $PUBLISHED_AT,
    CURRENT_TIMESTAMP,
    CURRENT_TIMESTAMP,
    0
);
EOF
    
    POST_ID=$(sqlite3 "$DB_PATH" "SELECT id FROM blog_posts WHERE slug = '$POST_SLUG';")
    log_success "Blog post created (ID: $POST_ID)"
fi

# Add tags
log_info "Adding tags..."
TAGS=("case-study" "automation" "invoice-generation" "google-sheets" "tutorial")

for TAG in "${TAGS[@]}"; do
    # Check if tag exists
    TAG_ID=$(sqlite3 "$DB_PATH" "SELECT id FROM tags WHERE slug = '$TAG';" 2>/dev/null || echo "")
    
    if [ -z "$TAG_ID" ]; then
        # Create tag
        TAG_NAME=$(echo "$TAG" | sed 's/-/ /g' | sed 's/\b\(.\)/\u\1/g')
        sqlite3 "$DB_PATH" <<EOF
INSERT OR IGNORE INTO tags (name, slug, created_at)
VALUES ('$TAG_NAME', '$TAG', CURRENT_TIMESTAMP);
EOF
        TAG_ID=$(sqlite3 "$DB_PATH" "SELECT id FROM tags WHERE slug = '$TAG';")
        log_info "  Created tag: $TAG_NAME (ID: $TAG_ID)"
    else
        log_info "  Tag exists: $TAG (ID: $TAG_ID)"
    fi
    
    # Link tag to post
    sqlite3 "$DB_PATH" <<EOF
INSERT OR IGNORE INTO post_tags (post_id, tag_id)
VALUES ($POST_ID, $TAG_ID);
EOF
done

log_success "Tags added"

echo ""
log_success "Blog post published successfully!"
echo ""
echo "Post Details:"
echo "  ID: $POST_ID"
echo "  Title: $TITLE"
echo "  Slug: $POST_SLUG"
echo "  Status: $STATUS"
echo "  Reading time: $READING_TIME minutes"
echo "  Word count: $WORD_COUNT"
echo ""
echo "View at:"
if [ "$STATUS" = "published" ]; then
    echo "  Local:  http://localhost:9090/blog/$POST_SLUG"
    echo "  Production: https://filesurf.io/blog/$POST_SLUG"
else
    echo "  (Draft - not publicly visible yet)"
fi
echo ""
echo "Next steps:"
echo "  1. Test locally: mvn quarkus:dev"
echo "  2. Deploy: ./scripts/deploy-blog.sh"
echo ""
