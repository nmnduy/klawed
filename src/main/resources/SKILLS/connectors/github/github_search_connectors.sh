#!/bin/bash

# GitHub Connector Search
# Search for n8n connectors by name, category, or pattern

set -euo pipefail

# Default configuration
N8N_REPO="${N8N_REPO:-n8n-io/n8n}"
BASE_URL="https://api.github.com/repos/${N8N_REPO}/contents"
CACHE_DIR="${CACHE_DIR:-$HOME/.cache/filesurf-connectors}"
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
Usage: $0 [search_term] [options]

Search for n8n connectors in the GitHub repository.

Arguments:
  search_term       Optional search term (name, category, or keyword)

Options:
  -l, --list-all    List all available connectors
  -c, --category    Search by category (e.g., google, microsoft, database)
  -p, --pattern     Search by file pattern (e.g., *Action.ts, *Description.ts)
  -m, --match-case  Case-sensitive search
  -r, --refresh     Refresh connector list cache
  -h, --help        Show this help message

Examples:
  $0 --list-all
  $0 google --category
  $0 slack
  $0 "http request" --pattern
  $0 --pattern "*Action.ts"

Environment Variables:
  GITHUB_TOKEN      GitHub API token for higher rate limits
  CACHE_DIR         Cache directory (default: ~/.cache/filesurf-connectors)
  N8N_REPO          n8n repository (default: n8n-io/n8n)
EOF
    exit 1
}

# Function to get all connectors (cached)
get_all_connectors() {
    local force_refresh="${1:-false}"
    local cache_file="${CACHE_DIR}/all_connectors.json"
    
    # Check cache first
    if [ "$force_refresh" = "false" ] && [ -f "$cache_file" ]; then
        local cache_age=$(( $(date +%s) - $(stat -c %Y "$cache_file") ))
        if [ $cache_age -lt 86400 ]; then  # 24 hours
            cat "$cache_file"
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
    
    # Get list of connectors
    local url="${BASE_URL}/packages/nodes-base/nodes"
    local response
    if ! response=$(eval "$curl_cmd '$url'"); then
        echo "Failed to get connector list from GitHub" >&2
        return 1
    fi
    
    # Check for API errors
    if echo "$response" | jq -e '.message' >/dev/null 2>&1; then
        local error_msg=$(echo "$response" | jq -r '.message')
        echo "GitHub API error: $error_msg" >&2
        return 1
    fi
    
    # Cache the response
    echo "$response" > "$cache_file"
    echo "$response"
}

# Function to search connectors by name
search_by_name() {
    local search_term="$1"
    local match_case="${2:-false}"
    local connectors_json="$3"
    
    if [ "$match_case" = "true" ]; then
        echo "$connectors_json" | jq -r ".[] | select(.name | contains(\"$search_term\")) | .name"
    else
        echo "$connectors_json" | jq -r ".[] | select(.name | ascii_downcase | contains(\"$search_term\" | ascii_downcase)) | .name"
    fi
}

# Function to search by category (heuristic)
search_by_category() {
    local category="$1"
    local connectors_json="$2"
    
    # Define category patterns (heuristic based on naming conventions)
    declare -A category_patterns=(
        ["google"]="Google|Gmail|Sheets|Drive|Calendar|YouTube|BigQuery"
        ["microsoft"]="Microsoft|Office|Outlook|Teams|SharePoint|Azure|OneDrive"
        ["database"]="Postgres|MySQL|MongoDB|Redis|Elasticsearch|Supabase|Airtable"
        ["social"]="Twitter|Facebook|Instagram|LinkedIn|Slack|Discord|Telegram"
        ["cloud"]="AWS|Azure|GCP|Cloud|S3|Lambda|Heroku"
        ["payment"]="Stripe|PayPal|Square|Chargebee|Razorpay"
        ["crm"]="Salesforce|HubSpot|Pipedrive|Zoho|Freshworks"
        ["communication"]="Email|SMS|Chat|Message|Twilio|SendGrid|Mailchimp"
        ["file"]="Dropbox|Box|GoogleDrive|OneDrive|S3|FTP|SFTP"
    )
    
    local pattern="${category_patterns[$category]:-}"
    if [ -z "$pattern" ]; then
        # If no predefined pattern, use the category name
        pattern="$category"
    fi
    
    echo "$connectors_json" | jq -r ".[] | select(.name | test(\"(?i)$pattern\")) | .name"
}

# Function to search by file pattern
search_by_file_pattern() {
    local pattern="$1"
    local connectors_json="$2"
    
    echo -e "${YELLOW}Note: File pattern search is slower as it checks each connector${NC}" >&2
    
    # Get all connector names
    local connector_names
    connector_names=$(echo "$connectors_json" | jq -r '.[].name')
    
    local results=()
    for connector in $connector_names; do
        # Check if connector has files matching the pattern
        local url="${BASE_URL}/packages/nodes-base/nodes/${connector}"
        
        local curl_cmd="curl -s"
        if [ -n "$GITHUB_TOKEN" ]; then
            curl_cmd="$curl_cmd -H 'Authorization: token $GITHUB_TOKEN'"
        fi
        curl_cmd="$curl_cmd -H 'Accept: application/vnd.github.v3+json'"
        
        local connector_files
        if connector_files=$(eval "$curl_cmd '$url'" 2>/dev/null); then
            if echo "$connector_files" | jq -r '.[].name' 2>/dev/null | grep -q "$pattern"; then
                results+=("$connector")
            fi
        fi
        
        # Be nice to GitHub API
        sleep 0.1
    done
    
    printf "%s\n" "${results[@]}"
}

# Main script logic
main() {
    local search_term=""
    local list_all=false
    local by_category=false
    local by_pattern=false
    local match_case=false
    local force_refresh=false
    
    # Parse arguments
    while [ $# -gt 0 ]; do
        case "$1" in
            -h|--help)
                usage
                ;;
            -l|--list-all)
                list_all=true
                shift
                ;;
            -c|--category)
                by_category=true
                shift
                ;;
            -p|--pattern)
                by_pattern=true
                shift
                ;;
            -m|--match-case)
                match_case=true
                shift
                ;;
            -r|--refresh)
                force_refresh=true
                shift
                ;;
            -*)
                echo -e "${RED}Error: Unknown option $1${NC}" >&2
                usage
                ;;
            *)
                if [ -z "$search_term" ]; then
                    search_term="$1"
                else
                    echo -e "${RED}Error: Multiple search terms specified${NC}" >&2
                    usage
                fi
                shift
                ;;
        esac
    done
    
    # Get all connectors
    local connectors_json
    if ! connectors_json=$(get_all_connectors "$force_refresh"); then
        echo -e "${RED}Failed to get connector list${NC}" >&2
        exit 1
    fi
    
    local connector_count
    connector_count=$(echo "$connectors_json" | jq 'length')
    echo -e "${BLUE}Found ${connector_count} connectors in n8n repository${NC}" >&2
    
    # Execute based on options
    if [ "$list_all" = "true" ]; then
        echo "$connectors_json" | jq -r '.[].name' | sort
    elif [ -n "$search_term" ]; then
        if [ "$by_category" = "true" ]; then
            search_by_category "$search_term" "$connectors_json"
        elif [ "$by_pattern" = "true" ]; then
            search_by_file_pattern "$search_term" "$connectors_json"
        else
            search_by_name "$search_term" "$match_case" "$connectors_json"
        fi
    else
        # Default: show some statistics
        echo ""
        echo -e "${GREEN}Connector Categories (heuristic):${NC}"
        echo "  google     - Google services (Sheets, Drive, Calendar, etc.)"
        echo "  microsoft  - Microsoft services (Office, Teams, Azure, etc.)"
        echo "  database   - Databases (Postgres, MySQL, MongoDB, etc.)"
        echo "  social     - Social media (Twitter, Facebook, Slack, etc.)"
        echo "  cloud      - Cloud services (AWS, Azure, GCP, etc.)"
        echo "  payment    - Payment processors (Stripe, PayPal, etc.)"
        echo "  crm        - CRM systems (Salesforce, HubSpot, etc.)"
        echo ""
        echo "Examples:"
        echo "  $0 --list-all"
        echo "  $0 google --category"
        echo "  $0 slack"
        echo "  $0 --pattern \"*Action.ts\""
    fi
}

# Run main function
main "$@"