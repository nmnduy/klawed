#!/bin/bash
# Deploy blog from local development to production
# Safe, idempotent deployment with backups and verification

set -euo pipefail

# Configuration
readonly LOCAL_DB="data/blog.db"
readonly PROD_HOST="filesurf-0"
readonly PROD_DB="/var/lib/filesurf/data/blog.db"
readonly PROD_APP_DIR="/root/filesurf_v2"
readonly TIMESTAMP=$(date +%Y%m%d_%H%M%S)
readonly BACKUP_DIR="backups/blog"
readonly EXPORT_DIR="exports/blog"

# Colors
readonly RED='\033[0;31m'
readonly GREEN='\033[0;32m'
readonly YELLOW='\033[1;33m'
readonly BLUE='\033[0;34m'
readonly NC='\033[0m' # No Color

# Logging functions
log_info() { echo -e "${BLUE}[INFO]${NC} $*"; }
log_success() { echo -e "${GREEN}[SUCCESS]${NC} $*"; }
log_warn() { echo -e "${YELLOW}[WARN]${NC} $*"; }
log_error() { echo -e "${RED}[ERROR]${NC} $*" >&2; }

# Check prerequisites
check_prerequisites() {
    log_info "Checking prerequisites..."
    
    local missing=()
    
    if ! command -v sqlite3 >/dev/null; then
        missing+=("sqlite3")
    fi
    
    if ! command -v ssh >/dev/null; then
        missing+=("ssh")
    fi
    
    if ! command -v scp >/dev/null; then
        missing+=("scp")
    fi
    
    if [ ${#missing[@]} -gt 0 ]; then
        log_error "Missing required tools: ${missing[*]}"
        exit 1
    fi
    
    log_success "All prerequisites met"
}

# Check local blog database
check_local_blog() {
    log_info "Checking local blog database..."
    
    if [ ! -f "$LOCAL_DB" ]; then
        log_error "Local blog database not found: $LOCAL_DB"
        exit 1
    fi
    
    # Verify it's a valid SQLite database
    if ! sqlite3 "$LOCAL_DB" "SELECT 1;" >/dev/null 2>&1; then
        log_error "Invalid SQLite database: $LOCAL_DB"
        exit 1
    fi
    
    log_success "Local blog database verified"
    
    # Show statistics
    log_info "Local blog statistics:"
    sqlite3 "$LOCAL_DB" << 'STATS_SQL'
SELECT '  📝 Posts: ' || COUNT(*) || ' (' || 
  (SELECT COUNT(*) FROM blog_posts WHERE status = 'published') || ' published, ' ||
  (SELECT COUNT(*) FROM blog_posts WHERE status = 'draft') || ' drafts)'
FROM blog_posts;
SELECT '  👤 Authors: ' || COUNT(*) FROM authors;
SELECT '  📂 Categories: ' || COUNT(*) FROM categories;
SELECT '  🏷️  Tags: ' || COUNT(*) FROM tags;

SELECT '';
SELECT '  Recent posts:';
SELECT '    - ' || title || ' (' || status || ')' 
FROM blog_posts 
ORDER BY COALESCE(published_at, created_at) DESC 
LIMIT 3;
STATS_SQL
}

# Create backup of local database
backup_local() {
    log_info "Creating local backup..."
    
    mkdir -p "$BACKUP_DIR"
    local backup_file="$BACKUP_DIR/blog_local_$TIMESTAMP.db"
    
    cp "$LOCAL_DB" "$backup_file"
    
    if [ $? -eq 0 ]; then
        log_success "Local backup created: $backup_file"
        log_info "Backup size: $(du -h "$backup_file" | cut -f1)"
    else
        log_warn "Failed to create local backup (continuing anyway)"
    fi
}

# Export blog data
export_blog() {
    log_info "Exporting blog data..." >&2
    
    mkdir -p "$EXPORT_DIR"
    local export_file="$EXPORT_DIR/blog_export_$TIMESTAMP.sql"
    
    # Use .dump for a complete export
    sqlite3 "$LOCAL_DB" .dump > "$export_file"
    
    if [ $? -eq 0 ]; then
        log_success "Export created: $export_file" >&2
        log_info "Export size: $(wc -l < "$export_file") lines" >&2
        echo "$export_file"
    else
        log_error "Failed to export blog data" >&2
        exit 1
    fi
}

# Check production connectivity
check_production() {
    log_info "Checking production connectivity..."
    
    if ! ssh -o ConnectTimeout=5 "$PROD_HOST" "echo 'Connected'" >/dev/null 2>&1; then
        log_error "Cannot connect to production host: $PROD_HOST"
        exit 1
    fi
    
    log_success "Connected to production host"
    
    # Check if production database directory exists
    local prod_db_dir=$(dirname "$PROD_DB")
    if ssh "$PROD_HOST" "sudo test -d '$prod_db_dir'"; then
        log_info "Production database directory exists: $prod_db_dir"
    else
        log_warn "Production database directory does not exist: $prod_db_dir"
        log_info "It will be created during deployment"
    fi
}

# Download production database backup
download_prod_backup() {
    log_info "Downloading production database backup..."
    
    # Check if production database exists
    if ! ssh "$PROD_HOST" "sudo test -f '$PROD_DB'"; then
        log_warn "No production database found to backup"
        return 0
    fi
    
    # Create backup on production and download it
    local prod_backup="/tmp/blog_prod_backup_$TIMESTAMP.db"
    local local_backup="$BACKUP_DIR/blog_prod_$TIMESTAMP.db"
    
    mkdir -p "$BACKUP_DIR"
    
    # Copy to temp location on production (so we can scp it)
    if ! ssh "$PROD_HOST" "sudo cp '$PROD_DB' '$prod_backup' && sudo chmod 644 '$prod_backup'"; then
        log_error "Failed to create production backup"
        exit 1
    fi
    
    # Download backup
    if ! scp -q "$PROD_HOST:$prod_backup" "$local_backup" 2>&1; then
        log_error "Failed to download production backup"
        ssh "$PROD_HOST" "rm -f '$prod_backup'" || true
        exit 1
    fi
    
    # Cleanup temp file on production
    ssh "$PROD_HOST" "rm -f '$prod_backup'" || true
    
    log_success "Production backup downloaded: $local_backup"
    log_info "Backup size: $(du -h "$local_backup" | cut -f1)"
    
    # Show production statistics
    log_info "Current production blog statistics:"
    ssh "$PROD_HOST" "sudo sqlite3 '$PROD_DB'" << 'STATS_SQL' 2>/dev/null || log_warn "Could not read production statistics"
SELECT '  📝 Posts: ' || COUNT(*) || ' (' || 
  (SELECT COUNT(*) FROM blog_posts WHERE status = 'published') || ' published, ' ||
  (SELECT COUNT(*) FROM blog_posts WHERE status = 'draft') || ' drafts)'
FROM blog_posts;
SELECT '  👤 Authors: ' || COUNT(*) FROM authors;
SELECT '  📂 Categories: ' || COUNT(*) FROM categories;
SELECT '  🏷️  Tags: ' || COUNT(*) FROM tags;
STATS_SQL
}

# Deploy to production
deploy_to_production() {
    local export_file="$1"
    
    log_info "Deploying to production..."
    
    # Transfer export file
    log_info "Transferring export file to production..."
    local scp_output
    if ! scp_output=$(scp -q "$export_file" "$PROD_HOST:/tmp/blog_import.sql" 2>&1); then
        log_error "Failed to transfer export file to production"
        [ -n "$scp_output" ] && echo "$scp_output" >&2
        exit 1
    fi
    log_success "Export file transferred successfully"
    
    # Confirmation before replacing database
    echo ""
    log_warn "⚠️  This will REPLACE the production blog database with local data"
    echo ""
    read -p "Type 'yes' to confirm deployment: " -r
    echo
    if [[ ! $REPLY == "yes" ]]; then
        log_warn "Deployment cancelled by user"
        ssh "$PROD_HOST" "rm -f /tmp/blog_import.sql" || true
        exit 0
    fi
    
    # Execute deployment on production
    log_info "Executing deployment on production..."
    ssh "$PROD_HOST" << 'DEPLOY_SCRIPT'
set -euo pipefail

# Configuration
readonly PROD_DB="/var/lib/filesurf/data/blog.db"
readonly IMPORT_FILE="/tmp/blog_import.sql"

echo "Starting deployment on production..."

# Backup existing database if it exists
if sudo test -f "$PROD_DB"; then
    echo "Backing up existing production database..."
    readonly BACKUP_FILE="${PROD_DB}.backup.$(date +%Y%m%d_%H%M%S)"
    sudo cp "$PROD_DB" "$BACKUP_FILE"
    echo "Backup created: $BACKUP_FILE"
    echo "Backup size: $(sudo du -h "$BACKUP_FILE" | cut -f1)"
    
    # Remove old database to avoid conflicts
    echo "Removing old database..."
    sudo rm -f "$PROD_DB"
    sudo rm -f "${PROD_DB}-shm" "${PROD_DB}-wal"
else
    echo "No existing production database found (will create new)"
fi

# Ensure database directory exists
echo "Ensuring database directory exists..."
sudo mkdir -p "$(dirname "$PROD_DB")"

# Import data (fresh database)
echo "Importing blog data..."
sudo sqlite3 "$PROD_DB" < "$IMPORT_FILE"

# Cleanup import file
sudo rm -f "$IMPORT_FILE"

# Verify import
echo "Verifying import..."
sudo sqlite3 "$PROD_DB" << 'VERIFY_SQL'
SELECT '✅ Posts: ' || COUNT(*) FROM blog_posts;
SELECT '   Published: ' || COUNT(*) FROM blog_posts WHERE status = 'published';
SELECT '   Drafts: ' || COUNT(*) FROM blog_posts WHERE status = 'draft';
SELECT '✅ Authors: ' || COUNT(*) FROM authors;
SELECT '✅ Categories: ' || COUNT(*) FROM categories;
SELECT '✅ Tags: ' || COUNT(*) FROM tags;

SELECT '';
SELECT 'Latest posts:';
SELECT '  - ' || title || ' (' || status || ')' 
FROM blog_posts 
ORDER BY COALESCE(published_at, created_at) DESC 
LIMIT 3;
VERIFY_SQL

echo "Deployment on production complete!"
DEPLOY_SCRIPT
    
    if [ $? -eq 0 ]; then
        log_success "Deployment to production completed successfully"
    else
        log_error "Deployment to production failed"
        exit 1
    fi
}

# Test blog accessibility
test_blog_access() {
    log_info "Testing blog accessibility on production..."
    
    # Test blog homepage
    local http_code=$(ssh "$PROD_HOST" \
        "curl -s -o /dev/null -w '%{http_code}' http://localhost:9090/blog 2>/dev/null || echo '000'")
    
    case "$http_code" in
        200)
            log_success "Blog homepage is accessible (HTTP 200)"
            ;;
        303|302)
            log_warn "Blog homepage redirects (HTTP $http_code) - may require authentication"
            ;;
        404)
            log_warn "Blog homepage not found (HTTP 404) - routes may not be registered"
            ;;
        000)
            log_warn "Could not test blog accessibility (curl failed)"
            ;;
        *)
            log_warn "Unexpected response from blog: HTTP $http_code"
            ;;
    esac
    
    # Test specific blog post
    local post_code=$(ssh "$PROD_HOST" \
        "curl -s -o /dev/null -w '%{http_code}' http://localhost:9090/blog/ai-pipeline-workflow 2>/dev/null || echo '000'")
    
    if [ "$post_code" = "200" ]; then
        log_success "Blog post 'ai-pipeline-workflow' is accessible (HTTP 200)"
    elif [ "$post_code" = "404" ]; then
        log_info "Blog post 'ai-pipeline-workflow' not found (HTTP 404) - may not exist or slug is different"
    fi
}

# Main execution
main() {
    echo -e "${BLUE}========================================${NC}"
    echo -e "${BLUE}      FileSurf Blog Deployment${NC}"
    echo -e "${BLUE}========================================${NC}"
    echo ""
    
    # Check prerequisites
    check_prerequisites
    
    # Check local blog
    check_local_blog
    
    echo ""
    read -p "Continue with deployment to production? (y/N): " -n 1 -r
    echo
    if [[ ! $REPLY =~ ^[Yy]$ ]]; then
        log_warn "Deployment cancelled by user"
        exit 0
    fi
    
    # Create local backup
    backup_local
    
    # Export blog data
    local export_file
    export_file=$(export_blog)
    
    # Check production
    check_production
    
    # Download production backup FIRST
    download_prod_backup
    
    # Deploy to production
    deploy_to_production "$export_file"
    
    # Test accessibility
    test_blog_access
    
    echo ""
    echo -e "${GREEN}========================================${NC}"
    echo -e "${GREEN}     Deployment Complete!${NC}"
    echo -e "${GREEN}========================================${NC}"
    echo ""
    echo -e "${BLUE}Next steps:${NC}"
    echo "  1. Test blog publicly: ${GREEN}https://filesurf.io/blog${NC}"
    echo "  2. Test specific post: ${GREEN}https://filesurf.io/blog/ai-pipeline-workflow${NC}"
    echo "  3. If blog requires authentication, update AuthenticationFilter.java"
    echo "  4. Restart service if needed:"
    echo "     ${YELLOW}ssh $PROD_HOST 'sudo systemctl restart filesurf-v2'${NC}"
    echo ""
    echo -e "${BLUE}Files created:${NC}"
    echo "  • Local backup: ${BACKUP_DIR}/blog_local_${TIMESTAMP}.db"
    echo "  • Production backup: ${BACKUP_DIR}/blog_prod_${TIMESTAMP}.db"
    echo "  • Export file: ${export_file}"
    echo ""
}

# Run main function
main "$@"