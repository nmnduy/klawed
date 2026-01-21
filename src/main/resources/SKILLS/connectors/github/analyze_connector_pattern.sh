#!/bin/bash

# Connector Pattern Analyzer
# Analyze n8n connector patterns to understand implementation structure
# Extracts structured data for AI-assisted connector generation

set -euo pipefail

# Default configuration
CACHE_DIR="${CACHE_DIR:-$HOME/.cache/filesurf-connectors}"
ANALYSIS_DIR="${ANALYSIS_DIR:-$CACHE_DIR/analysis}"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Create directories
mkdir -p "$CACHE_DIR" "$ANALYSIS_DIR"

# Function to print usage
usage() {
    cat << EOF
Usage: $0 <connector1> [connector2 ...] [options]

Analyze n8n connector patterns to extract implementation templates.

Arguments:
  connector1, connector2, ...   Connector names to analyze

Options:
  -o, --output <file>     Output analysis to file (default: stdout)
  -f, --format <format>   Output format: json, yaml, markdown (default: markdown)
  -c, --compare           Compare patterns between connectors
  -p, --patterns-only     Show only extracted patterns (no details)
  -r, --refresh           Refresh cached connector data
  -d, --deep              Deep analysis (fetches more files, slower)
  -h, --help              Show this help message

Examples:
  $0 Airtable Slack
  $0 GoogleSheets GoogleDrive --compare
  $0 Stripe PayPal --output patterns.json --format json
  $0 --patterns-only Airtable
  $0 Notion --deep --format json

Environment Variables:
  CACHE_DIR         Cache directory (default: ~/.cache/filesurf-connectors)
  ANALYSIS_DIR      Analysis output directory (default: CACHE_DIR/analysis)
EOF
    exit 1
}

# Function to get script directory
get_script_dir() {
    local script_path="${BASH_SOURCE[0]}"
    cd "$(dirname "$script_path")" && pwd
}

# Function to fetch file content via GitHub API
fetch_file_content() {
    local connector="$1"
    local file_path="$2"
    local force_refresh="${3:-false}"
    
    local script_dir
    script_dir=$(get_script_dir)
    
    if [ -f "$script_dir/github_browse_connector.sh" ]; then
        "$script_dir/github_browse_connector.sh" "$connector" --file "$file_path" --raw 2>/dev/null || echo ""
    else
        echo ""
    fi
}

# Function to get file list for connector
get_connector_files() {
    local connector="$1"
    local force_refresh="${2:-false}"
    
    local script_dir
    script_dir=$(get_script_dir)
    
    if [ -f "$script_dir/github_browse_connector.sh" ]; then
        "$script_dir/github_browse_connector.sh" "$connector" --list 2>/dev/null || echo ""
    else
        echo ""
    fi
}

# Function to extract authentication pattern
extract_auth_pattern() {
    local content="$1"
    
    local auth_type="unknown"
    
    # Check for OAuth2
    if echo "$content" | grep -q "OAuth2"; then
        auth_type="oauth2"
    # Check for API Key in header
    elif echo "$content" | grep -qE "Authorization.*Bearer|apiKey|api_key|API-Key"; then
        auth_type="apiKey"
    # Check for Basic Auth
    elif echo "$content" | grep -qE "Basic.*auth|basicAuth"; then
        auth_type="basic"
    # Check for Query Parameter auth
    elif echo "$content" | grep -qE "qs\[.*key.*\]|query.*apiKey"; then
        auth_type="queryParam"
    fi
    
    echo "$auth_type"
}

# Function to extract resources from node description
extract_resources() {
    local content="$1"
    
    # Extract resource options using multiple patterns
    local resources=""
    
    # Pattern 1: name: 'Resource Name', value: 'resourceValue'
    resources=$(echo "$content" | grep -oP "name:\s*['\"]([^'\"]+)['\"],\s*value:\s*['\"]([^'\"]+)['\"]" | \
        sed "s/name:\s*['\"]//g; s/['\"],\s*value:\s*['\"]/ => /g; s/['\"]//g" | \
        head -20 || echo "")
    
    # If no resources found, try alternative pattern
    if [ -z "$resources" ]; then
        resources=$(echo "$content" | grep -oP "value:\s*['\"]([a-z]+)['\"]" | \
            sed "s/value:\s*['\"]//g; s/['\"]//g" | \
            sort -u | head -10 || echo "")
    fi
    
    echo "$resources"
}

# Function to extract operations
extract_operations() {
    local content="$1"
    
    # Common CRUD operations
    local operations=""
    
    # Look for operation options
    operations=$(echo "$content" | grep -oP "name:\s*['\"](?:Create|Read|Get|Update|Delete|List|Search|Upload|Download|Send|Execute)[^'\"]*['\"]" | \
        sed "s/name:\s*['\"]//g; s/['\"]//g" | \
        sort -u | tr '\n' ',' | sed 's/,$//' || echo "")
    
    # If empty, try looking for action patterns
    if [ -z "$operations" ]; then
        operations=$(echo "$content" | grep -oP "action:\s*['\"][^'\"]+['\"]" | \
            sed "s/action:\s*['\"]//g; s/['\"]//g" | \
            sort -u | head -10 | tr '\n' ',' | sed 's/,$//' || echo "")
    fi
    
    echo "$operations"
}

# Function to extract property types used
extract_property_types() {
    local content="$1"
    
    local types=""
    types=$(echo "$content" | grep -oP "type:\s*['\"]([a-zA-Z]+)['\"]" | \
        sed "s/type:\s*['\"]//g; s/['\"]//g" | \
        sort | uniq -c | sort -rn | head -10 | \
        awk '{print $2 " (" $1 ")"}' | tr '\n' ',' | sed 's/,$//' || echo "")
    
    echo "$types"
}

# Function to extract API endpoint patterns
extract_endpoint_patterns() {
    local content="$1"
    
    # Look for endpoint strings
    local endpoints=""
    endpoints=$(echo "$content" | grep -oP "['\"]\/[a-zA-Z0-9\/_\-\{\}\$]+['\"]" | \
        sed "s/['\"]//g" | sort -u | head -15 | tr '\n' ' ' || echo "")
    
    echo "$endpoints"
}

# Function to check for pagination support
check_pagination() {
    local content="$1"
    
    if echo "$content" | grep -qE "pagination|cursor|offset|page|nextPage|hasMore|returnAll"; then
        echo "yes"
    else
        echo "no"
    fi
}

# Function to check for batch operations
check_batch_operations() {
    local content="$1"
    
    if echo "$content" | grep -qE "batch|bulk|multiple|many"; then
        echo "yes"
    else
        echo "no"
    fi
}

# Function to analyze a single connector
analyze_connector() {
    local connector="$1"
    local deep="${2:-false}"
    local force_refresh="${3:-false}"
    
    echo -e "${BLUE}Analyzing connector: ${connector}${NC}" >&2
    
    # Get file list
    local file_list
    file_list=$(get_connector_files "$connector" "$force_refresh")
    
    if [ -z "$file_list" ]; then
        echo -e "${YELLOW}Warning: Could not get file list for ${connector}${NC}" >&2
        return 1
    fi
    
    # Initialize analysis data
    local main_file_content=""
    local credentials_content=""
    local generic_functions_content=""
    
    # Determine main node file
    local main_file=""
    if echo "$file_list" | grep -q "${connector}.node.ts"; then
        main_file="${connector}.node.ts"
    elif echo "$file_list" | grep -q "Node.ts"; then
        main_file=$(echo "$file_list" | grep -oP "[^/]*Node\.ts" | head -1)
    fi
    
    # Fetch main node content
    if [ -n "$main_file" ]; then
        main_file_content=$(fetch_file_content "$connector" "$main_file" "$force_refresh")
    fi
    
    # Deep analysis - fetch more files
    if [ "$deep" = "true" ]; then
        # Look for GenericFunctions
        if echo "$file_list" | grep -qi "GenericFunctions"; then
            generic_functions_content=$(fetch_file_content "$connector" "GenericFunctions.ts" "$force_refresh")
        fi
    fi
    
    # Combine content for analysis
    local all_content="$main_file_content $generic_functions_content"
    
    # Extract patterns
    local auth_pattern
    auth_pattern=$(extract_auth_pattern "$all_content")
    
    local resources
    resources=$(extract_resources "$main_file_content")
    
    local operations
    operations=$(extract_operations "$main_file_content")
    
    local property_types
    property_types=$(extract_property_types "$main_file_content")
    
    local endpoints
    endpoints=$(extract_endpoint_patterns "$all_content")
    
    local has_pagination
    has_pagination=$(check_pagination "$all_content")
    
    local has_batch
    has_batch=$(check_batch_operations "$all_content")
    
    # Count files by type
    local ts_files
    ts_files=$(echo "$file_list" | grep -c "\.ts" || echo 0)
    
    local has_actions_dir="no"
    if echo "$file_list" | grep -q "actions/"; then
        has_actions_dir="yes"
    fi
    
    local has_methods_dir="no"
    if echo "$file_list" | grep -q "methods/"; then
        has_methods_dir="yes"
    fi
    
    # Output structured analysis
    cat << EOF
{
  "connector": "$connector",
  "analyzed_at": "$(date -Iseconds)",
  "structure": {
    "main_file": "$main_file",
    "typescript_files": $ts_files,
    "has_actions_directory": $has_actions_dir,
    "has_methods_directory": $has_methods_dir
  },
  "authentication": {
    "type": "$auth_pattern"
  },
  "api": {
    "resources": "$(echo "$resources" | tr '\n' ',' | sed 's/,$//')",
    "operations": "$operations",
    "endpoints_sample": "$endpoints",
    "supports_pagination": $has_pagination,
    "supports_batch": $has_batch
  },
  "properties": {
    "types_used": "$property_types"
  }
}
EOF
}

# Function to generate markdown report
generate_markdown_report() {
    local connectors=("$@")
    
    local report="# n8n Connector Pattern Analysis\n\n"
    report+="Generated: $(date)\n"
    report+="Connectors analyzed: ${#connectors[@]}\n\n"
    
    for connector in "${connectors[@]}"; do
        report+="---\n\n"
        report+="## $connector\n\n"
        
        local analysis_json
        analysis_json=$(analyze_connector "$connector" "false" "false")
        
        if [ -n "$analysis_json" ]; then
            # Parse JSON and format as markdown
            local auth_type
            auth_type=$(echo "$analysis_json" | jq -r '.authentication.type // "unknown"')
            
            local resources
            resources=$(echo "$analysis_json" | jq -r '.api.resources // "none"')
            
            local operations
            operations=$(echo "$analysis_json" | jq -r '.api.operations // "none"')
            
            local pagination
            pagination=$(echo "$analysis_json" | jq -r '.api.supports_pagination // "unknown"')
            
            local main_file
            main_file=$(echo "$analysis_json" | jq -r '.structure.main_file // "unknown"')
            
            report+="### Structure\n"
            report+="- **Main File**: \`$main_file\`\n"
            report+="- **Has Actions Directory**: $(echo "$analysis_json" | jq -r '.structure.has_actions_directory')\n"
            report+="- **Has Methods Directory**: $(echo "$analysis_json" | jq -r '.structure.has_methods_directory')\n\n"
            
            report+="### Authentication\n"
            report+="- **Type**: $auth_type\n\n"
            
            report+="### API Patterns\n"
            report+="- **Resources**: $resources\n"
            report+="- **Operations**: $operations\n"
            report+="- **Supports Pagination**: $pagination\n"
            report+="- **Supports Batch**: $(echo "$analysis_json" | jq -r '.api.supports_batch')\n\n"
            
            report+="### Property Types Used\n"
            report+="$(echo "$analysis_json" | jq -r '.properties.types_used')\n\n"
        else
            report+="*Failed to analyze this connector*\n\n"
        fi
    done
    
    echo -e "$report"
}

# Function to generate comparison report
generate_comparison_report() {
    local connectors=("$@")
    
    echo -e "${CYAN}Comparing ${#connectors[@]} connectors...${NC}" >&2
    
    local report="# Connector Comparison\n\n"
    report+="| Feature | $(printf '%s | ' "${connectors[@]}")\n"
    report+="|---------|$(printf -- '--------|' "${connectors[@]}")\n"
    
    # Collect data for each connector
    local auth_types=()
    local pagination_support=()
    local batch_support=()
    
    for connector in "${connectors[@]}"; do
        local analysis
        analysis=$(analyze_connector "$connector" "false" "false" 2>/dev/null)
        
        auth_types+=("$(echo "$analysis" | jq -r '.authentication.type // "?"')")
        pagination_support+=("$(echo "$analysis" | jq -r '.api.supports_pagination // "?"')")
        batch_support+=("$(echo "$analysis" | jq -r '.api.supports_batch // "?"')")
    done
    
    # Build comparison table
    report+="| Auth Type | $(printf '%s | ' "${auth_types[@]}")\n"
    report+="| Pagination | $(printf '%s | ' "${pagination_support[@]}")\n"
    report+="| Batch Ops | $(printf '%s | ' "${batch_support[@]}")\n"
    
    echo -e "$report"
}

# Main script logic
main() {
    local connectors=()
    local output_file=""
    local format="markdown"
    local compare=false
    local patterns_only=false
    local force_refresh=false
    local deep_analysis=false
    
    # Parse arguments
    while [ $# -gt 0 ]; do
        case "$1" in
            -h|--help)
                usage
                ;;
            -o|--output)
                if [ -z "${2:-}" ]; then
                    echo -e "${RED}Error: --output requires a filename${NC}" >&2
                    exit 1
                fi
                output_file="$2"
                shift 2
                ;;
            -f|--format)
                if [ -z "${2:-}" ]; then
                    echo -e "${RED}Error: --format requires a format${NC}" >&2
                    exit 1
                fi
                format="$2"
                shift 2
                ;;
            -c|--compare)
                compare=true
                shift
                ;;
            -p|--patterns-only)
                patterns_only=true
                shift
                ;;
            -r|--refresh)
                force_refresh=true
                shift
                ;;
            -d|--deep)
                deep_analysis=true
                shift
                ;;
            -*)
                echo -e "${RED}Error: Unknown option $1${NC}" >&2
                usage
                ;;
            *)
                connectors+=("$1")
                shift
                ;;
        esac
    done
    
    if [ ${#connectors[@]} -eq 0 ]; then
        echo -e "${RED}Error: At least one connector name is required${NC}" >&2
        usage
    fi
    
    # Generate report based on format and options
    local report=""
    
    if [ "$compare" = "true" ] && [ ${#connectors[@]} -gt 1 ]; then
        report=$(generate_comparison_report "${connectors[@]}")
    elif [ "$format" = "json" ]; then
        # JSON output
        report="["
        local first=true
        for connector in "${connectors[@]}"; do
            if [ "$first" = "true" ]; then
                first=false
            else
                report+=","
            fi
            report+="\n$(analyze_connector "$connector" "$deep_analysis" "$force_refresh")"
        done
        report+="\n]"
    else
        # Markdown output (default)
        report=$(generate_markdown_report "${connectors[@]}")
    fi
    
    # Output report
    if [ -n "$output_file" ]; then
        echo -e "$report" > "$output_file"
        echo -e "${GREEN}Analysis saved to: $output_file${NC}" >&2
    else
        echo -e "$report"
    fi
    
    # Also save to analysis directory with timestamp
    local timestamp
    timestamp=$(date +%Y%m%d_%H%M%S)
    local analysis_file="${ANALYSIS_DIR}/analysis_${timestamp}.${format}"
    echo -e "$report" > "$analysis_file"
    echo -e "${BLUE}Analysis also saved to: $analysis_file${NC}" >&2
}

# Run main function
main "$@"
