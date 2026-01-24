#!/bin/bash
# Script to manage user subscriptions (admin tool)

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

DB_PATH="${DB_PATH:-$PROJECT_ROOT/data/filesurf.db}"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

function print_usage() {
    echo "Usage: $0 <command> [options]"
    echo ""
    echo "Commands:"
    echo "  list                     List all active subscriptions"
    echo "  show <email>            Show subscription details for user"
    echo "  grant <email> <plan>    Manually grant a plan to user"
    echo "  revoke <email>          Revoke user's subscription"
    echo "  usage <email>           Show usage statistics for user"
    echo ""
    echo "Plans: basic, pro, enterprise"
    echo ""
    echo "Examples:"
    echo "  $0 list"
    echo "  $0 show user@example.com"
    echo "  $0 grant user@example.com pro"
    echo "  $0 revoke user@example.com"
    echo "  $0 usage user@example.com"
}

function check_db() {
    if [ ! -f "$DB_PATH" ]; then
        echo -e "${RED}Error: Database not found at $DB_PATH${NC}"
        exit 1
    fi
}

function get_user_id() {
    local email="$1"
    sqlite3 "$DB_PATH" "SELECT user_id FROM users WHERE email = '$email';"
}

function list_subscriptions() {
    echo -e "${BLUE}Active Subscriptions:${NC}"
    echo ""
    
    sqlite3 -header -column "$DB_PATH" <<EOF
SELECT 
    u.email,
    s.plan_code as plan,
    s.status,
    datetime(s.started_at, 'localtime') as started,
    CASE 
        WHEN s.expires_at IS NULL THEN 'Never'
        ELSE datetime(s.expires_at, 'localtime')
    END as expires
FROM user_subscriptions s
JOIN users u ON s.user_id = u.user_id
WHERE s.status = 'active'
ORDER BY s.started_at DESC;
EOF
}

function show_subscription() {
    local email="$1"
    
    if [ -z "$email" ]; then
        echo -e "${RED}Error: Email required${NC}"
        print_usage
        exit 1
    fi
    
    local user_id=$(get_user_id "$email")
    
    if [ -z "$user_id" ]; then
        echo -e "${RED}Error: User not found: $email${NC}"
        exit 1
    fi
    
    echo -e "${BLUE}Subscription Details for: $email${NC}"
    echo ""
    
    sqlite3 -header -column "$DB_PATH" <<EOF
SELECT 
    plan_code as plan,
    status,
    datetime(started_at, 'localtime') as started,
    CASE 
        WHEN expires_at IS NULL THEN 'Never'
        ELSE datetime(expires_at, 'localtime')
    END as expires,
    stripe_customer_id,
    stripe_subscription_id
FROM user_subscriptions
WHERE user_id = '$user_id'
ORDER BY started_at DESC
LIMIT 1;
EOF
    
    echo ""
    echo -e "${BLUE}Plan Features:${NC}"
    echo ""
    
    local plan_code=$(sqlite3 "$DB_PATH" "SELECT plan_code FROM user_subscriptions WHERE user_id = '$user_id' AND status = 'active' ORDER BY started_at DESC LIMIT 1;")
    
    if [ -n "$plan_code" ]; then
        sqlite3 -header -column "$DB_PATH" <<EOF
SELECT 
    display_name as feature,
    feature_value as value
FROM plan_features
WHERE plan_code = '$plan_code'
ORDER BY sort_order;
EOF
    else
        echo "No active subscription"
    fi
}

function grant_subscription() {
    local email="$1"
    local plan="$2"
    
    if [ -z "$email" ] || [ -z "$plan" ]; then
        echo -e "${RED}Error: Email and plan required${NC}"
        print_usage
        exit 1
    fi
    
    # Validate plan
    if [[ ! "$plan" =~ ^(basic|pro|enterprise)$ ]]; then
        echo -e "${RED}Error: Invalid plan. Must be: basic, pro, or enterprise${NC}"
        exit 1
    fi
    
    local user_id=$(get_user_id "$email")
    
    if [ -z "$user_id" ]; then
        echo -e "${RED}Error: User not found: $email${NC}"
        exit 1
    fi
    
    echo -e "${YELLOW}Granting $plan plan to $email...${NC}"
    
    # Cancel any existing active subscriptions
    sqlite3 "$DB_PATH" <<EOF
UPDATE user_subscriptions 
SET status = 'cancelled', 
    cancelled_at = CURRENT_TIMESTAMP,
    cancellation_reason = 'Replaced by manual grant'
WHERE user_id = '$user_id' 
AND status = 'active';
EOF
    
    # Create new subscription
    sqlite3 "$DB_PATH" <<EOF
INSERT INTO user_subscriptions (user_id, plan_code, status, started_at)
VALUES ('$user_id', '$plan', 'active', CURRENT_TIMESTAMP);
EOF
    
    echo -e "${GREEN}Successfully granted $plan plan to $email${NC}"
    echo ""
    show_subscription "$email"
}

function revoke_subscription() {
    local email="$1"
    
    if [ -z "$email" ]; then
        echo -e "${RED}Error: Email required${NC}"
        print_usage
        exit 1
    fi
    
    local user_id=$(get_user_id "$email")
    
    if [ -z "$user_id" ]; then
        echo -e "${RED}Error: User not found: $email${NC}"
        exit 1
    fi
    
    echo -e "${YELLOW}Revoking subscription for $email...${NC}"
    
    sqlite3 "$DB_PATH" <<EOF
UPDATE user_subscriptions 
SET status = 'cancelled', 
    cancelled_at = CURRENT_TIMESTAMP,
    cancellation_reason = 'Manual revocation'
WHERE user_id = '$user_id' 
AND status = 'active';
EOF
    
    echo -e "${GREEN}Successfully revoked subscription for $email${NC}"
}

function show_usage() {
    local email="$1"
    
    if [ -z "$email" ]; then
        echo -e "${RED}Error: Email required${NC}"
        print_usage
        exit 1
    fi
    
    local user_id=$(get_user_id "$email")
    
    if [ -z "$user_id" ]; then
        echo -e "${RED}Error: User not found: $email${NC}"
        exit 1
    fi
    
    echo -e "${BLUE}Usage Statistics for: $email${NC}"
    echo ""
    
    # Get plan limits
    local plan_code=$(sqlite3 "$DB_PATH" "SELECT plan_code FROM user_subscriptions WHERE user_id = '$user_id' AND status = 'active' ORDER BY started_at DESC LIMIT 1;")
    
    if [ -n "$plan_code" ]; then
        echo -e "${GREEN}Current Plan: $plan_code${NC}"
        echo ""
        
        echo -e "${BLUE}Plan Limits:${NC}"
        sqlite3 -header -column "$DB_PATH" <<EOF
SELECT 
    feature_key as metric,
    feature_value as limit
FROM plan_features
WHERE plan_code = '$plan_code'
AND feature_key IN ('heavy_model_limit', 'storage_gb', 'compute_minutes');
EOF
        
        echo ""
    fi
    
    echo -e "${BLUE}Current Usage (Last 30 Days):${NC}"
    
    sqlite3 -header -column "$DB_PATH" <<EOF
SELECT 
    heavy_model_requests,
    cerebras_requests,
    ROUND(storage_bytes / 1073741824.0, 2) as storage_gb,
    compute_minutes,
    datetime(last_updated, 'localtime') as last_updated
FROM user_usage
WHERE user_id = '$user_id'
AND period_start >= datetime('now', '-30 days')
ORDER BY period_start DESC
LIMIT 1;
EOF
    
    # Check if no usage data
    local has_usage=$(sqlite3 "$DB_PATH" "SELECT COUNT(*) FROM user_usage WHERE user_id = '$user_id' AND period_start >= datetime('now', '-30 days');")
    
    if [ "$has_usage" -eq 0 ]; then
        echo ""
        echo -e "${YELLOW}No usage data found for the last 30 days${NC}"
    fi
}

# Main script logic
check_db

COMMAND="${1:-}"

case "$COMMAND" in
    list)
        list_subscriptions
        ;;
    show)
        show_subscription "$2"
        ;;
    grant)
        grant_subscription "$2" "$3"
        ;;
    revoke)
        revoke_subscription "$2"
        ;;
    usage)
        show_usage "$2"
        ;;
    *)
        print_usage
        exit 1
        ;;
esac
