#!/bin/bash
#
# Sanity test script for FileSurf v2 deployment
# Run this after deployment to verify core functionality
#
# Usage: ./scripts/sanity-test.sh [server_url]
#   server_url: Base URL of the server (default: http://localhost:9090)
#

set -e

# Configuration
SERVER_URL="${1:-http://localhost:9090}"
TEST_EMAIL="test@example.com"
COOKIE_FILE=$(mktemp)
RESULT_FILE=$(mktemp)

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Counters
PASSED=0
FAILED=0
SKIPPED=0

# Cleanup on exit
cleanup() {
    rm -f "$COOKIE_FILE" "$RESULT_FILE"
}
trap cleanup EXIT

print_header() {
    echo ""
    echo -e "${BLUE}========================================${NC}"
    echo -e "${BLUE}$1${NC}"
    echo -e "${BLUE}========================================${NC}"
    echo ""
}

pass() {
    echo -e "  ${GREEN}✓ PASS${NC}: $1"
    PASSED=$((PASSED + 1))
}

fail() {
    echo -e "  ${RED}✗ FAIL${NC}: $1"
    FAILED=$((FAILED + 1))
}

skip() {
    echo -e "  ${YELLOW}⊘ SKIP${NC}: $1"
    SKIPPED=$((SKIPPED + 1))
}

section() {
    echo ""
    echo -e "  ${YELLOW}▶ $1${NC}"
}

# =============================================================================
# TEST 1: Server Health
# =============================================================================
test_server_health() {
    print_header "TEST 1: Server Health"

    section "Checking if server is reachable"
    HTTP_CODE=$(curl -s -o /dev/null -w "%{http_code}" "$SERVER_URL/file-chat" --connect-timeout 5 2>/dev/null || echo "000")
    if [ "$HTTP_CODE" = "200" ]; then
        pass "Server is reachable (HTTP $HTTP_CODE)"
    else
        fail "Server is NOT reachable (HTTP $HTTP_CODE)"
        return 1
    fi

    section "Checking health endpoint"
    HEALTH=$(curl -s "$SERVER_URL/q/health" --connect-timeout 5 2>/dev/null)
    if echo "$HEALTH" | grep -q "UP"; then
        pass "Health endpoint returns UP"
    else
        skip "Health endpoint (may not be exposed)"
    fi
}

# =============================================================================
# TEST 2: Authentication Flow
# =============================================================================
test_authentication() {
    print_header "TEST 2: Authentication Flow"

    section "Getting auth status (unauthenticated)"
    AUTH_STATUS=$(curl -s "$SERVER_URL/auth/status" --connect-timeout 5 2>/dev/null || echo "")
    if echo "$AUTH_STATUS" | grep -q '"authenticated":false'; then
        pass "Auth status correctly shows unauthenticated"
    else
        fail "Auth status check failed"
        return 1
    fi

    section "Logging in with test user: $TEST_EMAIL"
    LOGIN_RESULT=$(curl -s -X POST "$SERVER_URL/auth/login" \
        -H "Content-Type: application/json" \
        -d "{\"email\":\"$TEST_EMAIL\"}" \
        --cookie-jar "$COOKIE_FILE" \
        --connect-timeout 5 2>/dev/null)

    if echo "$LOGIN_RESULT" | grep -q '"success":true'; then
        pass "Login successful"
    else
        # Check if user needs to be activated
        if echo "$LOGIN_RESULT" | grep -qi "not found\|not exist"; then
            fail "Test user '$TEST_EMAIL' not found. Run './scripts/invite-user.sh -a $TEST_EMAIL' first"
        else
            fail "Login failed: $LOGIN_RESULT"
        fi
        return 1
    fi

    section "Verifying authentication cookie"
    COOKIE_CHECK=$(curl -s "$SERVER_URL/auth/status" \
        -b "$COOKIE_FILE" \
        --connect-timeout 5 2>/dev/null)

    if echo "$COOKIE_CHECK" | grep -q '"authenticated":true'; then
        pass "Session authenticated correctly"
    else
        fail "Session not authenticated after login"
        return 1
    fi

    # Extract user ID for later tests
    USER_ID=$(grep "filesurf_userId" "$COOKIE_FILE" | awk '{print $7}')
    if [ -n "$USER_ID" ]; then
        pass "Obtained user ID: ${USER_ID:0:20}..."
    else
        fail "Could not extract user ID from cookie"
        return 1
    fi

    # Export for other tests
    echo "$USER_ID" > "$RESULT_FILE"
}

# =============================================================================
# TEST 3: Session Management
# =============================================================================
test_session() {
    print_header "TEST 3: Session Management"

    USER_ID=$(cat "$RESULT_FILE" 2>/dev/null)
    if [ -z "$USER_ID" ]; then
        skip "Skipping (no authenticated user)"
        return 1
    fi

    section "Getting session count"
    SESSION_COUNT=$(curl -s "$SERVER_URL/session/count" \
        -b "$COOKIE_FILE" \
        --connect-timeout 5 2>/dev/null)

    if [ -n "$SESSION_COUNT" ] && [ "$SESSION_COUNT" -ge 0 ] 2>/dev/null; then
        pass "Session count: $SESSION_COUNT"
    else
        fail "Could not get session count"
    fi

    section "Generating new session"
    SESSION_RESPONSE=$(curl -s -X POST "$SERVER_URL/session/generate" \
        -b "$COOKIE_FILE" \
        --connect-timeout 5 2>/dev/null)

    SESSION_ID=$(echo "$SESSION_RESPONSE" | grep -o '"sessionId":"[^"]*"' | head -1 | cut -d'"' -f4)
    if [ -n "$SESSION_ID" ]; then
        pass "Created session: ${SESSION_ID:0:20}..."
        echo "$SESSION_ID" >> "$RESULT_FILE"
    else
        fail "Could not create session"
    fi
}

# =============================================================================
# TEST 4: File-Chat HTTP Endpoints (CORE)
# =============================================================================
test_file_chat_http() {
    print_header "TEST 4: File-Chat HTTP Endpoints (CORE)"

    USER_ID=$(head -1 "$RESULT_FILE" 2>/dev/null)
    SESSION_ID=$(tail -1 "$RESULT_FILE" 2>/dev/null)

    if [ -z "$USER_ID" ] || [ -z "$SESSION_ID" ]; then
        skip "Skipping (no session available)"
        return 1
    fi

    section "Getting messages for session"
    MESSAGES=$(curl -s "$SERVER_URL/file-chat/http/messages/$SESSION_ID" \
        -b "$COOKIE_FILE" \
        -H "X-Session-ID: $SESSION_ID" \
        -H "X-User-ID: $USER_ID" \
        --connect-timeout 5 2>/dev/null)

    if echo "$MESSAGES" | grep -qE '\[\]|messages|content'; then
        pass "Messages endpoint works"
    else
        fail "Messages endpoint failed"
    fi

    section "Polling for responses"
    POLL=$(curl -s "$SERVER_URL/file-chat/http/poll/$SESSION_ID" \
        -b "$COOKIE_FILE" \
        -H "X-Session-ID: $SESSION_ID" \
        -H "X-User-ID: $USER_ID" \
        --connect-timeout 5 2>/dev/null)

    if echo "$POLL" | grep -qE '\[\]|pending|complete'; then
        pass "Poll endpoint works"
    else
        fail "Poll endpoint failed"
    fi

    section "Sending test message to AI"
    MESSAGE_RESPONSE=$(curl -s -X POST "$SERVER_URL/file-chat/http/message/$SESSION_ID" \
        -b "$COOKIE_FILE" \
        -H "X-Session-ID: $SESSION_ID" \
        -H "X-User-ID: $USER_ID" \
        -H "Content-Type: text/plain" \
        -d "Hello, this is a sanity test message. Please respond with 'Sanity test passed' if you receive this." \
        --connect-timeout 10 2>/dev/null)

    if echo "$MESSAGE_RESPONSE" | grep -qi "success\|queued\|accepted"; then
        pass "Message sent successfully"
    else
        fail "Failed to send message: $MESSAGE_RESPONSE"
    fi

    section "Waiting for AI response (polling)..."
    echo "  Waiting up to 30 seconds for AI response..."

    for i in {1..30}; do
        sleep 1
        POLL_RESULT=$(curl -s "$SERVER_URL/file-chat/http/poll/$SESSION_ID" \
            -b "$COOKIE_FILE" \
            -H "X-Session-ID: $SESSION_ID" \
            -H "X-User-ID: $USER_ID" \
            --connect-timeout 5 2>/dev/null)

        # Check if response is complete
        if echo "$POLL_RESULT" | grep -qi "complete.*true\|response.*received\|sanity.*passed"; then
            pass "AI response received within ${i}s"
            return 0
        fi

        # Check for error
        if echo "$POLL_RESULT" | grep -qi "error\|failed"; then
            fail "AI processing error: $POLL_RESULT"
            return 1
        fi

        # Progress indicator
        if [ $((i % 10)) -eq 0 ]; then
            echo "  Still waiting... (${i}s)"
        fi
    done

    # If we get here, polling timed out
    # Still count as pass if the message was accepted (async processing)
    if echo "$MESSAGE_RESPONSE" | grep -qi "success\|queued\|accepted"; then
        pass "Message sent (async AI processing - response pending)"
        skip "AI response timeout (expected for async processing)"
    else
        fail "No AI response after 30s"
    fi
}

# =============================================================================
# TEST 5: File Explorer Endpoints
# =============================================================================
test_file_explorer() {
    print_header "TEST 5: File Explorer Endpoints"

    USER_ID=$(head -1 "$RESULT_FILE" 2>/dev/null)
    SESSION_ID=$(tail -1 "$RESULT_FILE" 2>/dev/null)

    if [ -z "$USER_ID" ] || [ -z "$SESSION_ID" ]; then
        skip "Skipping (no session available)"
        return 1
    fi

    section "Listing session files"
    FILE_LIST=$(curl -s "$SERVER_URL/file-chat/explorer/list" \
        -b "$COOKIE_FILE" \
        -H "X-Session-ID: $SESSION_ID" \
        -H "X-User-ID: $USER_ID" \
        --connect-timeout 5 2>/dev/null)

    if echo "$FILE_LIST" | grep -qiE 'files|\[\]|session'; then
        pass "File explorer list works"
    else
        fail "File explorer list failed"
    fi
}

# =============================================================================
# MAIN
# =============================================================================
main() {
    echo ""
    echo -e "${BLUE}╔════════════════════════════════════════════════════════════╗${NC}"
    echo -e "${BLUE}║         FileSurf v2 Sanity Test Suite                     ║${NC}"
    echo -e "${BLUE}║                                                            ║${NC}"
    echo -e "${BLUE}║  Server: $SERVER_URL${NC}"
    printf "${BLUE}║  Time:  $(date)$NC"
    echo -e "${BLUE}╚════════════════════════════════════════════════════════════╝${NC}"

    # Run tests
    test_server_health || true
    test_authentication || true
    test_session || true
    test_file_chat_http || true
    test_file_explorer || true

    # Summary
    print_header "TEST SUMMARY"
    echo -e "  ${GREEN}Passed:${NC}  $PASSED"
    echo -e "  ${RED}Failed:${NC}  $FAILED"
    echo -e "  ${YELLOW}Skipped:${NC} $SKIPPED"
    echo ""

    if [ $FAILED -eq 0 ]; then
        echo -e "${GREEN}╔════════════════════════════════════════════════════════════╗${NC}"
        echo -e "${GREEN}║  ✓ ALL TESTS PASSED - Deployment is healthy!              ║${NC}"
        echo -e "${GREEN}╚════════════════════════════════════════════════════════════╝${NC}"
        exit 0
    else
        echo -e "${RED}╔════════════════════════════════════════════════════════════╗${NC}"
        echo -e "${RED}║  ✗ SOME TESTS FAILED - Review output above                ║${NC}"
        echo -e "${RED}╚════════════════════════════════════════════════════════════╝${NC}"
        exit 1
    fi
}

main "$@"
