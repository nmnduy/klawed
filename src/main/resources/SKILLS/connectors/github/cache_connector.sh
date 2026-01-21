#!/bin/bash

# Connector Cache Manager
# Cache frequently accessed n8n connectors for faster offline access

set -euo pipefail

# Default configuration
CACHE_DIR="${CACHE_DIR:-$HOME/.cache/filesurf-connectors}"
ANALYSIS_DIR="${ANALYSIS_DIR:-$CACHE_DIR/analysis}"
PATTERNS_DIR="${PATTERNS_DIR:-$CACHE_DIR/patterns}"
GITHUB_TOKEN="${GITHUB_TOKEN:-}"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

# Create directories
mkdir -p "$CACHE_DIR" "$ANALYSIS_DIR" "$PATTERNS_DIR"

# Function to print usage
usage() {
    cat << EOF
Usage: $0 <command> [options]

Manage connector cache for n8n connector browsing.

Commands:
  add <connector>         Cache a connector (all files)
  add-file <connector> <path>  Cache specific file from connector
  add-pattern <pattern>   Cache all connectors matching pattern
  list                    List cached connectors
  list-files <connector>  List cached files for a connector
  get <connector>         Get cached connector data
  remove <connector>      Remove connector from cache
  clean                   Remove expired cache entries
  clear                   Clear all cache
  status                  Show cache statistics
  sync                    Sync cached connectors with remote

Options:
  -r, --refresh           Force refresh (ignore TTL)
  -v, --verbose           Verbose output
  -h, --help              Show this help message

Examples:
  $0 add Airtable
  $0 add GoogleSheets --refresh
  $0 add-pattern "google*" --category
  $0 list
  $0 status
  $0 clean

Environment Variables:
  CACHE_DIR         Cache directory (default: ~/.cache/filesurf-connectors)
  GITHUB_TOKEN      GitHub API token for higher rate limits
EOF
    exit 1
}

# Function to get script directory
get_script_dir() {
    local script_path="${BASH_SOURCE[0]}"
    local script_dir
    script_dir=$(cd "$(dirname "$script_path")" && pwd)
    echo "$script_dir"
}

# Function to make GitHub API request
github_api_request() {
    local url="$1"
    local cache_key="$2"
    local force_refresh="${3:-false}"
    
    # Check cache first (unless force refresh)
    if [ "$force_refresh" = "false" ] && [ -f "${CACHE_DIR}/${cache_key}" ]; then
        local cache_age=$(( $(date +%s) - $(stat -c %Y "${CACHE_DIR}/${cache_key}" 2>/dev/null || echo 0) ))
        if [ $cache_age -lt 86400 ]; then  # 24 hours default TTL
            echo "Using cached response for ${cache_key}" >&2
            cat "${CACHE_DIR}/${cache_key}"
            return 0
        fi
    fi
    
    # Prepare curl command
    local curl_cmd="curl -s -L"  # -L follow redirects
    
    # Add authentication if token is provided
    if [ -n "$GITHUB_TOKEN" ]; then
        curl_cmd="$curl_cmd -H 'Authorization: token $GITHUB_TOKEN'"
    fi
    
    # Add accept header for GitHub API v3
    curl_cmd="$curl_cmd -H 'Accept: application/vnd.github.v3+json'"
    
    # Make the request with retry logic
    local max_retries=3
    local retry_delay=1
    local response=""
    
    for ((i=1; i<=max_retries; i++)); do
        if response=$(eval "$curl_cmd '$url'" 2>&1); then
            break
        fi
        echo "Retry $i/$max_retries after error: $response" >&2
        sleep $retry_delay
    done
    
    # Check for API errors
    if echo "$response" | jq -e '.message' >/dev/null 2>&1; then
        local error_msg=$(echo "$response" | jq -r '.message')
        echo "GitHub API error: $error_msg" >&2
        return 1
    fi
    
    # Cache the response
    echo "$response" > "${CACHE_DIR}/${cache_key}"
    echo "$response"
}

# Function to cache entire connector
cache_connector() {
    local connector="$1"
    local force_refresh="${2:-false}"
    local verbose="${3:-false}"
    
    local script_dir
    script_dir=$(get_script_dir)
    
    echo -e "${CYAN}Caching connector: ${connector}${NC}" >&2
    
    # Get connector file list
    local list_cache_key="list_${connector}.json"
    local file_list
    if ! file_list=$(github_api_request \
        "https://api.github.com/repos/n8n-io/n8n/contents/packages/nodes-base/nodes/${connector}" \
        "$list_cache_key" \
        "$force_refresh"); then
        echo -e "${RED}Failed to get file list for ${connector}${NC}" >&2
        return 1
    fi
    
    # Count files and directories
    local file_count dir_count
    file_count=$(echo "$file_list" | jq '[.[] | select(.type == "file")] | length')
    dir_count=$(echo "$file_list" | jq '[.[] | select(.type == "dir")] | length')
    
    echo -e "${BLUE}Found ${file_count} files and ${dir_count} directories${NC}" >&2
    
    # Cache each file
    local cached=0
    local failed=0
    
    echo "$file_list" | jq -r '.[] | select(.type == "file") | .name, .download_url' | while read -r name; read -r download_url; do
        if [ -z "$name" ]; then
            continue
        fi
        
        local cache_key="raw_${connector}_$(echo "$name" | tr '/' '_')"
        
        if [ "$force_refresh" = "false" ] && [ -f "${CACHE_DIR}/${cache_key}" ]; then
            [ "$verbose" = "true" ] && echo -e "  ${YELLOW}Skipped (cached):${NC} $name" >&2
            ((cached++))
            continue
        fi
        
        # Download raw content
        local content
        if content=$(curl -s -L "$download_url"); then
            echo "$content" > "${CACHE_DIR}/${cache_key}"
            [ "$verbose" = "true" ] && echo -e "  ${GREEN}Cached:${NC} $name" >&2
            ((cached++))
        else
            echo -e "  ${RED}Failed:${NC} $name" >&2
            ((failed++))
        fi
        
        # Be nice to GitHub API
        sleep 0.05
    done
    
    echo -e "${GREEN}Cached ${cached} files for ${connector}${NC}" >&2
    if [ $failed -gt 0 ]; then
        echo -e "${YELLOW}Warning: ${failed} files failed to cache${NC}" >&2
    fi
    
    # Cache pattern analysis
    if [ -f "${script_dir}/analyze_connector_pattern.sh" ]; then
        local pattern_file="${PATTERNS_DIR}/${connector}.json"
        "${script_dir}/analyze_connector_pattern.sh" "$connector" --output "$pattern_file" --format json --refresh 2>/dev/null || true
        echo -e "${BLUE}Pattern analysis cached${NC}" >&2
    fi
    
    return 0
}

# Function to cache specific file
cache_file() {
    local connector="$1"
    local file_path="$2"
    local force_refresh="${3:-false}"
    
    local cache_key="raw_${connector}_$(echo "$file_path" | tr '/' '_')"
    
    if [ "$force_refresh" = "false" ] && [ -f "${CACHE_DIR}/${cache_key}" ]; then
        cat "${CACHE_DIR}/${cache_key}"
        echo "File already cached" >&2
        return 0
    fi
    
    # Get download URL
    local file_info
    file_info=$(github_api_request \
        "https://api.github.com/repos/n8n-io/n8n/contents/packages/nodes-base/nodes/${connector}/${file_path}" \
        "file_info_${connector}_$(echo "$file_path" | tr '/' '_').json" \
        "$force_refresh")
    
    local download_url
    download_url=$(echo "$file_info" | jq -r '.download_url')
    
    if [ "$download_url" = "null" ]; then
        echo "Cannot get download URL for $file_path" >&2
        return 1
    fi
    
    # Download and cache
    local content
    content=$(curl -s -L "$download_url")
    echo "$content" > "${CACHE_DIR}/${cache_key}"
    
    echo "Cached: ${connector}/${file_path}" >&2
    echo "$content"
}

# Function to cache connectors matching pattern
cache_pattern() {
    local pattern="$1"
    local force_refresh="${2:-false}"
    
    local script_dir
    script_dir=$(get_script_dir)
    
    # Get all connectors
    local connectors_json
    connectors_json=$("$script_dir/github_search_connectors.sh" --list-all 2>/dev/null || echo "[]")
    
    # Filter by pattern
    local matching_connectors
    matching_connectors=$(echo "$connectors_json" | jq -r ".[] | select(.name | test(\"$pattern\"; \"i\")) | .name")
    
    if [ -z "$matching_connectors" ]; then
        echo -e "${YELLOW}No connectors match pattern: ${pattern}${NC}" >&2
        return 0
    fi
    
    local count
    count=$(echo "$matching_connectors" | wc -l)
    echo -e "${CYAN}Found ${count} connectors matching: ${pattern}${NC}" >&2
    
    echo "$matching_connectors" | while read -r connector; do
        if [ -n "$connector" ]; then
            cache_connector "$connector" "$force_refresh"
        fi
    done
}

# Function to list cached connectors
list_cached_connectors() {
    local verbose="${1:-false}"
    
    # Find all cached connectors
    local cached_connectors
    cached_connectors=$(find "$CACHE_DIR" -maxdepth 1 -name "list_*.json" -o -name "raw_*" 2>/dev/null | \
        sed 's|.*/raw_||' | sed 's/_.*//' | sort -u)
    
    if [ -z "$cached_connectors" ]; then
        echo -e "${YELLOW}No connectors in cache${NC}" >&2
        return 0
    fi
    
    echo -e "${GREEN}Cached Connectors:${NC}" >&2
    echo "---" >&2
    
    echo "$cached_connectors" | while read -r connector; do
        if [ -n "$connector" ] && [ "$connector" != "connector_list" ]; then
            local cached_files
            cached_files=$(find "$CACHE_DIR" -maxdepth 1 -name "raw_${connector}_*" 2>/dev/null | wc -l)
            local pattern_file="${PATTERNS_DIR}/${connector}.json"
            
            if [ "$verbose" = "true" ]; then
                local last_access
                last_access=$(stat -c %Y "${CACHE_DIR}/raw_${connector}_"* 2>/dev/null | sort -rn | head -1 || echo 0)
                if [ "$last_access" -gt 0 ]; then
                    last_access=$(date -d "@$last_access" '+%Y-%m-%d %H:%M' 2>/dev/null || echo "unknown")
                else
                    last_access="unknown"
                fi
                echo -e "  ${CYAN}${connector}${NC} - ${cached_files} files - last accessed: $last_access"
            else
                echo -e "  ${GREEN}${connector}${NC} (${cached_files} files)"
            fi
        fi
    done
}

# Function to list files for a cached connector
list_connector_files() {
    local connector="$1"
    
    local pattern="raw_${connector}_"
    local files
    files=$(find "$CACHE_DIR" -maxdepth 1 -name "${pattern}*" 2>/dev/null | sort)
    
    if [ -z "$files" ]; then
        echo -e "${YELLOW}Connector ${connector} not in cache${NC}" >&2
        return 1
    fi
    
    echo -e "${GREEN}Cached files for ${connector}:${NC}" >&2
    echo "---" >&2
    
    echo "$files" | while read -r file; do
        local filename
        filename=$(basename "$file" | sed "s/^${pattern}//")
        local size
        size=$(stat -c %s "$file" 2>/dev/null || echo 0)
        
        if [ "$size" -gt 1024 ]; then
            size=$(echo "scale=2; $size/1024" | bc 2>/dev/null || echo "$size")
            size="${size}KB"
        else
            size="${size}B"
        fi
        
        echo -e "  📄 ${filename} (${size})"
    done
}

# Function to get cached connector data
get_cached_connector() {
    local connector="$1"
    local output_format="${2:-json}"
    
    local pattern="raw_${connector}_"
    local files
    files=$(find "$CACHE_DIR" -maxdepth 1 -name "${pattern}*" 2>/dev/null | sort)
    
    if [ -z "$files" ]; then
        echo -e "${YELLOW}Connector ${connector} not in cache${NC}" >&2
        return 1
    fi
    
    if [ "$output_format" = "json" ]; then
        echo "{"
        echo "  \"connector\": \"$connector\","
        echo "  \"files\": ["
        
        local first=true
        echo "$files" | while read -r file; do
            if [ -n "$file" ]; then
                local filename
                filename=$(basename "$file" | sed "s/^${pattern}//")
                local content
                content=$(cat "$file" | jq -Rs .)
                
                if [ "$first" = "true" ]; then
                    first=false
                else
                    echo ","
                fi
                
                echo -n "    {\"name\": \"$filename\", \"content\": $content}"
            fi
        done
        
        echo ""
        echo "  ]"
        echo "}"
    else
        # Plain text output
        echo "$files" | while read -r file; do
            if [ -n "$file" ]; then
                local filename
                filename=$(basename "$file" | sed "s/^${pattern}//")
                echo "=== ${filename} ==="
                cat "$file"
                echo ""
            fi
        done
    fi
}

# Function to remove connector from cache
remove_connector() {
    local connector="$1"
    
    local pattern="raw_${connector}_*"
    local files
    files=$(find "$CACHE_DIR" -maxdepth 1 -name "${pattern}" 2>/dev/null)
    
    if [ -z "$files" ]; then
        echo -e "${YELLOW}Connector ${connector} not in cache${NC}" >&2
        return 0
    fi
    
    local count=0
    echo "$files" | while read -r file; do
        if [ -n "$file" ]; then
            rm -f "$file"
            ((count++))
        fi
    done
    
    # Remove pattern file
    rm -f "${PATTERNS_DIR}/${connector}.json"
    rm -f "${CACHE_DIR}/list_${connector}.json"
    
    echo -e "${GREEN}Removed ${count} cached files for ${connector}${NC}" >&2
}

# Function to clean expired cache entries
clean_cache() {
    local ttl="${1:-604800}"  # Default: 7 days
    
    local cleaned=0
    local total=0
    
    echo -e "${CYAN}Cleaning cache entries older than ${ttl} seconds${NC}" >&2
    
    # Find and remove old files
    find "$CACHE_DIR" -maxdepth 1 -type f -mtime +${ttl} -delete 2>/dev/null
    cleaned=$?
    
    # Also clean old pattern files
    find "$PATTERNS_DIR" -type f -mtime +${ttl} -delete 2>/dev/null
    
    # Clean empty directories
    find "$CACHE_DIR" -type d -empty -delete 2>/dev/null
    find "$PATTERNS_DIR" -type d -empty -delete 2>/dev/null
    
    echo -e "${GREEN}Cache cleanup complete${NC}" >&2
}

# Function to clear all cache
clear_cache() {
    echo -e "${RED}This will delete ALL cached connectors. Continue? [y/N]${NC}" >&2
    read -r response
    
    if [ "$response" = "y" ] || [ "$response" = "Y" ]; then
        rm -rf "$CACHE_DIR"/*
        mkdir -p "$CACHE_DIR" "$ANALYSIS_DIR" "$PATTERNS_DIR"
        echo -e "${GREEN}Cache cleared${NC}" >&2
    else
        echo -e "${YELLOW}Cache clear cancelled${NC}" >&2
    fi
}

# Function to show cache status
show_status() {
    local total_size=0
    local connector_count=0
    local file_count=0
    
    echo -e "${GREEN}=== Connector Cache Status ===${NC}" >&2
    echo "" >&2
    
    # Count connectors and files
    local connectors
    connectors=$(find "$CACHE_DIR" -maxdepth 1 -name "raw_*" -o -name "list_*.json" 2>/dev/null | \
        sed 's|.*/raw_||' | sed 's/_.*//' | sort -u | grep -v "^$")
    
    connector_count=$(echo "$connectors" | grep -c "^" || echo 0)
    
    if [ "$connector_count" -gt 0 ]; then
        file_count=$(find "$CACHE_DIR" -maxdepth 1 -name "raw_*" 2>/dev/null | wc -l)
        
        # Calculate total size
        total_size=$(du -sh "$CACHE_DIR" 2>/dev/null | cut -f1 || echo "0")
        
        echo -e "${BLUE}Cache Directory:${NC} $CACHE_DIR" >&2
        echo -e "${BLUE}Total Size:${NC} $total_size" >&2
        echo -e "${BLUE}Connectors:${NC} $connector_count" >&2
        echo -e "${BLUE}Files:${NC} $file_count" >&2
        echo "" >&2
        
        echo -e "${GREEN}Pattern Cache:${NC}" >&2
        local pattern_count
        pattern_count=$(find "$PATTERNS_DIR" -name "*.json" 2>/dev/null | wc -l)
        echo -e "  Analyzed connectors: $pattern_count" >&2
    else
        echo -e "${YELLOW}No cached connectors${NC}" >&2
    fi
    
    # Show API rate limit info
    if [ -n "$GITHUB_TOKEN" ]; then
        echo "" >&2
        echo -e "${GREEN}GitHub Token:${NC} Configured" >&2
    else
        echo "" >&2
        echo -e "${YELLOW}GitHub Token:${NC} Not configured (rate limit: 60/hr)" >&2
    fi
}

# Function to sync cached connectors with remote
sync_cache() {
    local force_refresh="${1:-false}"
    
    echo -e "${CYAN}Syncing cached connectors with remote...${NC}" >&2
    
    local connectors
    connectors=$(find "$CACHE_DIR" -maxdepth 1 -name "list_*.json" -o -name "raw_*" 2>/dev/null | \
        sed 's|.*/list_||' | sed 's/_.*//' | sort -u | grep -v "^connector_list$" | grep -v "^$")
    
    if [ -z "$connectors" ]; then
        echo -e "${YELLOW}No cached connectors to sync${NC}" >&2
        return 0
    fi
    
    local synced=0
    local skipped=0
    local failed=0
    
    echo "$connectors" | while read -r connector; do
        if [ -n "$connector" ]; then
            echo -e "Syncing ${connector}..." >&2
            if cache_connector "$connector" "$force_refresh" 2>/dev/null; then
                ((synced++))
            else
                ((skipped++))
            fi
        fi
    done
    
    echo "" >&2
    echo -e "${GREEN}Sync complete:${NC} $synced synced, $skipped skipped" >&2
}

# Main script logic
main() {
    if [ $# -eq 0 ]; then
        usage
    fi
    
    local command="$1"
    shift
    
    local force_refresh=false
    local verbose=false
    
    # Parse common options
    while [ $# -gt 0 ]; do
        case "$1" in
            -r|--refresh)
                force_refresh=true
                shift
                ;;
            -v|--verbose)
                verbose=true
                shift
                ;;
            -h|--help)
                usage
                ;;
            *)
                break
                ;;
        esac
    done
    
    # Execute command
    case "$command" in
        add)
            if [ -z "${1:-}" ]; then
                echo -e "${RED}Error: Connector name required${NC}" >&2
                exit 1
            fi
            cache_connector "$1" "$force_refresh" "$verbose"
            ;;
        add-file)
            if [ -z "${1:-}" ] || [ -z "${2:-}" ]; then
                echo -e "${RED}Error: Connector and file path required${NC}" >&2
                exit 1
            fi
            cache_file "$1" "$2" "$force_refresh"
            ;;
        add-pattern)
            if [ -z "${1:-}" ]; then
                echo -e "${RED}Error: Pattern required${NC}" >&2
                exit 1
            fi
            cache_pattern "$1" "$force_refresh"
            ;;
        list)
            list_cached_connectors "$verbose"
            ;;
        list-files|ls)
            if [ -z "${1:-}" ]; then
                echo -e "${RED}Error: Connector name required${NC}" >&2
                exit 1
            fi
            list_connector_files "$1"
            ;;
        get|cat)
            if [ -z "${1:-}" ]; then
                echo -e "${RED}Error: Connector name required${NC}" >&2
                exit 1
            fi
            get_cached_connector "$1" "${2:-json}"
            ;;
        remove|rm|delete)
            if [ -z "${1:-}" ]; then
                echo -e "${RED}Error: Connector name required${NC}" >&2
                exit 1
            fi
            remove_connector "$1"
            ;;
        clean)
            clean_cache
            ;;
        clear)
            clear_cache
            ;;
        status)
            show_status
            ;;
        sync)
            sync_cache "$force_refresh"
            ;;
        *)
            echo -e "${RED}Error: Unknown command: $command${NC}" >&2
            usage
            ;;
    esac
}

# Run main function
main "$@"
