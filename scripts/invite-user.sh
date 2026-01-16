#!/bin/bash
#
# Invite a user to FileSurf by adding their email to the database.
# Usage: ./scripts/invite-user.sh <email>
#
# This script directly inserts the user into the SQLite database.
# The application must be stopped or the database must not be locked.
#

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
DB_PATH="${PROJECT_DIR}/data/filesurf.db"

# If database doesn't exist in current directory, check parent (for worktrees)
if [ ! -f "$DB_PATH" ]; then
    PARENT_PROJECT_DIR="$(dirname "$PROJECT_DIR")"
    PARENT_DB_PATH="${PARENT_PROJECT_DIR}/data/filesurf.db"
    if [ -f "$PARENT_DB_PATH" ]; then
        DB_PATH="$PARENT_DB_PATH"
        PROJECT_DIR="$PARENT_PROJECT_DIR"
    fi
fi

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

usage() {
    echo "Usage: $0 <email>"
    echo ""
    echo "Invite a user to FileSurf by adding their email to the database."
    echo ""
    echo "Examples:"
    echo "  $0 user@example.com                 # Invite/create a new user"
    echo "  $0 -a user@example.com              # Activate user (or create if doesn't exist)"
    echo "  $0 -d user@example.com              # Deactivate user"
    echo "  $0 -l                               # List all users"
    echo ""
    echo "Options:"
    echo "  -a, --activate     Activate a user (or create if doesn't exist)"
    echo "  -d, --deactivate   Deactivate a user"
    echo "  -l, --list         List all invited users"
    echo "  -h, --help         Show this help message"
    exit 1
}

list_users() {
    if [ ! -f "$DB_PATH" ]; then
        echo -e "${RED}Error: Database not found at ${DB_PATH}${NC}"
        echo "Make sure the application has been started at least once."
        exit 1
    fi

    echo -e "${YELLOW}Invited Users:${NC}"
    echo "=============================================="
    sqlite3 -header -column "$DB_PATH" "SELECT id, email, user_id, is_active, created_at FROM users ORDER BY created_at DESC;"
    echo ""
    TOTAL=$(sqlite3 "$DB_PATH" "SELECT COUNT(*) FROM users;")
    ACTIVE=$(sqlite3 "$DB_PATH" "SELECT COUNT(*) FROM users WHERE is_active = 1;")
    echo -e "Total: ${GREEN}${TOTAL}${NC} users (${GREEN}${ACTIVE}${NC} active)"
}

invite_user() {
    local EMAIL="$1"

    # Validate email format
    if [[ ! "$EMAIL" =~ ^[a-zA-Z0-9._%+-]+@[a-zA-Z0-9.-]+\.[a-zA-Z]{2,}$ ]]; then
        echo -e "${RED}Error: Invalid email format: ${EMAIL}${NC}"
        exit 1
    fi

    # Normalize email (lowercase)
    EMAIL=$(echo "$EMAIL" | tr '[:upper:]' '[:lower:]')

    # Check if database exists
    if [ ! -f "$DB_PATH" ]; then
        echo -e "${RED}Error: Database not found at ${DB_PATH}${NC}"
        echo "Make sure the application has been started at least once to initialize the database."
        exit 1
    fi

    # Check if user already exists
    EXISTING=$(sqlite3 "$DB_PATH" "SELECT email FROM users WHERE LOWER(email) = LOWER('${EMAIL}');")
    if [ -n "$EXISTING" ]; then
        echo -e "${YELLOW}User already exists: ${EMAIL}${NC}"
        sqlite3 -header -column "$DB_PATH" "SELECT id, email, user_id, is_active, created_at FROM users WHERE LOWER(email) = LOWER('${EMAIL}');"
        exit 0
    fi

    # Generate userId
    USER_ID="user-$(uuidgen | tr '[:upper:]' '[:lower:]')"

    # Insert user
    sqlite3 "$DB_PATH" "INSERT INTO users (user_id, email, created_at, is_active) VALUES ('${USER_ID}', '${EMAIL}', datetime('now'), 1);"

    if [ $? -eq 0 ]; then
        echo -e "${GREEN}✓ User invited successfully!${NC}"
        echo ""
        echo "Email:   ${EMAIL}"
        echo "User ID: ${USER_ID}"
        echo ""
        echo "The user can now log in at /auth/login with their email."
    else
        echo -e "${RED}Error: Failed to invite user${NC}"
        exit 1
    fi
}

deactivate_user() {
    local EMAIL="$1"
    EMAIL=$(echo "$EMAIL" | tr '[:upper:]' '[:lower:]')

    if [ ! -f "$DB_PATH" ]; then
        echo -e "${RED}Error: Database not found at ${DB_PATH}${NC}"
        exit 1
    fi

    # Check if user exists first
    EXISTING=$(sqlite3 "$DB_PATH" "SELECT email FROM users WHERE LOWER(email) = LOWER('${EMAIL}');")
    if [ -z "$EXISTING" ]; then
        echo -e "${YELLOW}No user found with email: ${EMAIL}${NC}"
        exit 0
    fi

    sqlite3 "$DB_PATH" "UPDATE users SET is_active = 0 WHERE LOWER(email) = LOWER('${EMAIL}');"

    if [ $? -eq 0 ]; then
        echo -e "${GREEN}✓ User deactivated: ${EMAIL}${NC}"
    else
        echo -e "${RED}Error: Failed to deactivate user${NC}"
        exit 1
    fi
}

activate_user() {
    local EMAIL="$1"
    EMAIL=$(echo "$EMAIL" | tr '[:upper:]' '[:lower:]')

    if [ ! -f "$DB_PATH" ]; then
        echo -e "${RED}Error: Database not found at ${DB_PATH}${NC}"
        exit 1
    fi

    # Check if user exists first
    EXISTING=$(sqlite3 "$DB_PATH" "SELECT email FROM users WHERE LOWER(email) = LOWER('${EMAIL}');")
    if [ -z "$EXISTING" ]; then
        echo -e "${YELLOW}User not found. Creating new user: ${EMAIL}${NC}"
        invite_user "$EMAIL"
        exit 0
    fi

    sqlite3 "$DB_PATH" "UPDATE users SET is_active = 1 WHERE LOWER(email) = LOWER('${EMAIL}');"

    if [ $? -eq 0 ]; then
        echo -e "${GREEN}✓ User activated: ${EMAIL}${NC}"
    else
        echo -e "${RED}Error: Failed to activate user${NC}"
        exit 1
    fi
}

# Parse arguments
case "$1" in
    -h|--help)
        usage
        ;;
    -l|--list)
        list_users
        ;;
    -d|--deactivate)
        if [ -z "$2" ]; then
            echo -e "${RED}Error: Email required${NC}"
            usage
        fi
        deactivate_user "$2"
        ;;
    -a|--activate)
        if [ -z "$2" ]; then
            echo -e "${RED}Error: Email required${NC}"
            usage
        fi
        activate_user "$2"
        ;;
    "")
        usage
        ;;
    *)
        invite_user "$1"
        ;;
esac
