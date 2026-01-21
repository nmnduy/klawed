#!/bin/bash

# GitHub Connector Browser
# Browse n8n connectors via GitHub API without downloading entire repository

set -euo pipefail

# Default configuration
N8N_REPO="${N8N_REPO:-n8n-io/n8n}"
BASE_URL="https://api.github.com/repos/${N8N_REPO}/contents"
CACHE_DIR="${CACHE_DIR:-$HOME/.cache/filesurf-connectors}"
CACHE_TTL="${CACHE_TTL:-86400}"  # 24 hours in seconds
GITHUB_TOKEN="${GITHUB_TOKEN:-}"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Create cache directory
mkdir -p "$CACHE_DIR"

# Function to print usage
usage() {
    cat << EOF
Usage: $0 <connector_name> [options]

Browse a specific n8n connector from GitHub.

Arguments:
  connector_name    Name of the connector (e.g., Airtable, Slack, GoogleSheets)

Options:
  -l, --list        List files in the connector directory
  -f, --file <path> Get specific file content
  -r, --raw         Get raw file content (no JSON)
  -c, --cache       Force cache refresh
  -h, --help        Show this help message

Examples:
  $0 Airtable --list
  $0 Slack --file Node.ts --raw
  $0 GoogleSheets

Environment Variables:
  GITHUB_TOKEN      GitHub API token for higher rate limits
  CACHE_DIR         Cache directory (default: ~/.cache/filesurf-connectors)
  CACHE_TTL         Cache TTL in seconds (default: 86400)
  N8N_REPO          n8n repository (default: n8n-io/n8n)
EOF
    exit 1
}

# Function to make GitHub API request
github_api_request() {
    local url="$1"
    local cache_key="$2"
    local force_refresh="${3:-false}"
    
    # Check cache first (unless force refresh)
    if [ "$force_refresh" = "false" ] && [ -f "${CACHE_DIR}/${cache_key}" ]; then
        local cache_age=$(( $(date +%s) - $(stat -c %Y "${CACHE_DIR}/${cache_key}") ))
        if [ $cache_age -lt $CACHE_TTL ]; then
            echo "Using cached response for ${cache_key}" >&2
            cat "${CACHE_DIR}/${cache_key}"
            return 0
        fi
    fi
    
    # Prepare curl command
    local curl_cmd="curl -s"
    
    # Add authentication if token is provided
    if [ -n "$GITHUB_TOKEN" ]; then
        curl_cmd="$curl_cmd -H 'Authorization: token $GITHUB_TOKEN'"
    fi
    
    # Add accept header for GitHub API v3
    curl_cmd="$curl_cmd -H 'Accept: application/vnd.github.v3+json'"
    
    # Make the request
    local response
    if ! response=$(eval "$curl_cmd '$url'"); then
        echo "Failed to make GitHub API request" >&2
        return 1
    fi
    
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

# Function to list connector files
list_connector_files() {
    local connector="$1"
    local cache_key="list_${connector}.json"
    local force_refresh="${2:-false}"
    
    local url="${BASE_URL}/packages/nodes-base/nodes/${connector}"
    
    echo -e "${BLUE}Listing files for connector: ${connector}${NC}" >&2
    
    local response
    if ! response=$(github_api_request "$url" "$cache_key" "$force_refresh"); then
        return 1
    fi
    
    # Parse and display file list
    echo "$response" | jq -r '.[] | "\(.type == "dir" ? "📁" : "📄") \(.name) (\(.size) bytes)"'
}

# Function to get file content
get_file_content() {
    local connector="$1"
    local file_path="$2"
    local raw="${3:-false}"
    local cache_key="file_${connector}_$(echo "$file_path" | tr '/' '_').json"
    local force_refresh="${4:-false}"
    
    local url="${BASE_URL}/packages/nodes-base/nodes/${connector}/${file_path}"
    
    echo -e "${BLUE}Getting file: ${connector}/${file_path}${NC}" >&2
    
    local response
    if ! response=$(github_api_request "$url" "$cache_key" "$force_refresh"); then
        return 1
    fi
    
    if [ "$raw" = "true" ]; then
        # Get download_url and fetch raw content
        local download_url=$(echo "$response" | jq -r '.download_url')
        if [ "$download_url" = "null" ]; then
            echo "Cannot get raw content for directory" >&2
            return 1
        fi
        
        # Cache raw content separately
        local raw_cache_key="raw_${connector}_$(echo "$file_path" | tr '/' '_')"
        if [ "$force_refresh" = "false" ] && [ -f "${CACHE_DIR}/${raw_cache_key}" ]; then
            local cache_age=$(( $(date +%s) - $(stat -c %Y "${CACHE_DIR}/${raw_cache_key}") ))
            if [ $cache_age -lt $CACHE_TTL ]; then
                cat "${CACHE_DIR}/${raw_cache_key}"
                return 0
            fi
        fi
        
        local raw_content
        if ! raw_content=$(curl -s "$download_url"); then
            echo "Failed to download raw content" >&2
            return 1
        fi
        
        echo "$raw_content" > "${CACHE_DIR}/${raw_cache_key}"
        echo "$raw_content"
    else
        # Return JSON response
        echo "$response"
    fi
}

# Function to check if connector exists
connector_exists() {
    local connector="$1"
    local cache_key="exists_${connector}.json"
    
    local url="${BASE_URL}/packages/nodes-base/nodes"
    
    local response
    if ! response=$(github_api_request "$url" "connector_list.json" "false"); then
        return 1
    fi
    
    # Check if connector directory exists
    if echo "$response" | jq -r '.[].name' | grep -q "^${connector}$"; then
        return 0
    else
        return 1
    fi
}

# Main script logic
main() {
    if [ $# -eq 0 ]; then
        usage
    fi
    
    local connector=""
    local list_files=false
    local file_path=""
    local raw_content=false
    local force_refresh=false
    
    # Parse arguments
    while [ $# -gt 0 ]; do
        case "$1" in
            -h|--help)
                usage
                ;;
            -l|--list)
                list_files=true
                shift
                ;;
            -f|--file)
                if [ -z "${2:-}" ]; then
                    echo -e "${RED}Error: --file requires a file path${NC}" >&2
                    exit 1
                fi
                file_path="$2"
                shift 2
                ;;
            -r|--raw)
                raw_content=true
                shift
                ;;
            -c|--cache)
                force_refresh=true
                shift
                ;;
            -*)
                echo -e "${RED}Error: Unknown option $1${NC}" >&2
                usage
                ;;
            *)
                if [ -z "$connector" ]; then
                    connector="$1"
                else
                    echo -e "${RED}Error: Multiple connectors specified${NC}" >&2
                    usage
                fi
                shift
                ;;
        esac
    done
    
    if [ -z "$connector" ]; then
        echo -e "${RED}Error: Connector name is required${NC}" >&2
        usage
    fi
    
    # Check if connector exists
    if ! connector_exists "$connector"; then
        echo -e "${RED}Error: Connector '$connector' not found in n8n repository${NC}" >&2
        echo -e "${YELLOW}Tip: Use ./github_search_connectors.sh to find available connectors${NC}" >&2
        exit 1
    fi
    
    # Execute based on options
    if [ "$list_files" = "true" ]; then
        list_connector_files "$connector" "$force_refresh"
    elif [ -n "$file_path" ]; then
        get_file_content "$connector" "$file_path" "$raw_content" "$force_refresh"
    else
        # Default: show connector info
        echo -e "${GREEN}Connector: ${connector}${NC}"
        echo -e "${BLUE}Available in: packages/nodes-base/nodes/${connector}${NC}"
        echo ""
        echo "Common files in n8n connectors:"
        echo "  📄 Node.ts              - Main node implementation"
        echo "  📄 ${connector}.node.ts - Node description/metadata"
        echo "  📁 actions/             - Action implementations"
        echo "  📁 methods/             - API methods"
        echo "  📁 test/                - Test files"
        echo ""
        echo "Use --list to see all files, or --file <path> to get specific file content."
    fi
}

# Run main function
main "$@"