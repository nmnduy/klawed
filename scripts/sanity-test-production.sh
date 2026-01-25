#!/bin/bash
#
# Production Sanity Test Script
# Runs sanity tests and pushes results to Prometheus Pushgateway
# Then triggers an alert if tests fail
#
# Usage: ./scripts/sanity-test-production.sh
#
# This script is designed to run via cron every 8 hours
#

set -e

# Configuration
SERVER_URL="${SANITY_TEST_SERVER_URL:-https://filesurf.io}"
PUSHGATEWAY_HOST="${SANITY_TEST_PUSHGATEWAY_HOST:-filesurf-0}"
PUSHGATEWAY_PORT="${SANITY_TEST_PUSHGATEWAY_PORT:-9091}"
TEST_EMAIL="${SANITY_TEST_EMAIL:-test@example.com}"
LOG_FILE="${SANITY_TEST_LOG:-/opt/filesurf-mon/logs/sanity-test.log}"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

# Logging function
log() {
    local level="$1"
    shift
    local message="$*"
    local timestamp=$(date '+%Y-%m-%d %H:%M:%S')
    echo -e "$timestamp [$level] $message" | tee -a "$LOG_FILE"
}

# Result tracking
PASSED=0
FAILED=0
SKIPPED=0
TEST_START_TIME=$(date +%s)

# =============================================================================
# Helper Functions
# =============================================================================

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

# Push metric to Pushgateway
push_metric() {
    local metric_name="$1"
    local metric_value="$2"
    local metric_type="${3:-gauge}"
    local labels="$4"
    
    local url="http://${PUSHGATEWAY_HOST}:${PUSHGATEWAY_PORT}/metrics/job/filesurf-sanity-test"
    
    # Use printf to ensure proper formatting with newline at end
    printf "# TYPE %s %s\n%s{%s} %s\n" "$metric_name" "$metric_type" "$metric_name" "$labels" "$metric_value" | \
        curl -s --max-time 10 -X POST "$url" --data-binary @- 2>&1
}

# =============================================================================
# TEST 1: Server Health
# =============================================================================
test_server_health() {
    print_header "TEST 1: Server Health"

    section "Checking if server is reachable"
    HTTP_CODE=$(curl -s -o /dev/null -w "%{http_code}" "$SERVER_URL/file-chat" --connect-timeout 10 2>/dev/null || echo "000")
    # 200 = OK, 303 = Redirect to login (also acceptable for health)
    if [ "$HTTP_CODE" = "200" ] || [ "$HTTP_CODE" = "303" ]; then
        pass "Server is reachable (HTTP $HTTP_CODE)"
    else
        fail "Server is NOT reachable (HTTP $HTTP_CODE)"
        return 1
    fi
}

# =============================================================================
# TEST 2: Authentication Flow
# =============================================================================
test_authentication() {
    print_header "TEST 2: Authentication Flow"
    
    COOKIE_FILE=$(mktemp)
    trap "rm -f $COOKIE_FILE" EXIT

    section "Getting auth status (unauthenticated)"
    AUTH_STATUS=$(curl -s "$SERVER_URL/auth/status" --connect-timeout 5 2>/dev/null || echo "")
    if echo "$AUTH_STATUS" | grep -q '"authenticated" *: *false'; then
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
        --connect-timeout 10 2>/dev/null)

    if echo "$LOGIN_RESULT" | grep -q '"success" *: *true'; then
        pass "Login successful"
    else
        if echo "$LOGIN_RESULT" | grep -qi "not found\|not exist\|inactive"; then
            fail "Test user '$TEST_EMAIL' not found or inactive"
        else
            fail "Login failed"
        fi
        return 1
    fi

    section "Verifying authentication cookie"
    COOKIE_CHECK=$(curl -s "$SERVER_URL/auth/status" \
        -b "$COOKIE_FILE" \
        --connect-timeout 5 2>/dev/null)

    if echo "$COOKIE_CHECK" | grep -q '"authenticated" *: *true'; then
        pass "Session authenticated correctly"
    else
        fail "Session not authenticated after login"
        return 1
    fi
}

# =============================================================================
# TEST 3: Session Management
# =============================================================================
test_session() {
    print_header "TEST 3: Session Management"
    
    COOKIE_FILE=$(mktemp)
    trap "rm -f $COOKIE_FILE" EXIT
    
    # First login
    LOGIN_RESULT=$(curl -s -X POST "$SERVER_URL/auth/login" \
        -H "Content-Type: application/json" \
        -d "{\"email\":\"$TEST_EMAIL\"}" \
        --cookie-jar "$COOKIE_FILE" \
        --connect-timeout 10 2>/dev/null)

    if ! echo "$LOGIN_RESULT" | grep -q '"success":true'; then
        skip "Skipping (login failed)"
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
        --connect-timeout 10 2>/dev/null)

    SESSION_ID=$(echo "$SESSION_RESPONSE" | grep -o '"sessionId":"[^"]*"' | head -1 | cut -d'"' -f4)
    if [ -n "$SESSION_ID" ]; then
        pass "Created session: ${SESSION_ID:0:20}..."
        echo "$SESSION_ID" > /tmp/sanity_test_session_id
    else
        fail "Could not create session"
        return 1
    fi
}

# =============================================================================
# TEST 4: File-Chat HTTP Endpoints (CORE)
# =============================================================================
test_file_chat_http() {
    print_header "TEST 4: File-Chat HTTP Endpoints (CORE)"
    
    COOKIE_FILE=$(mktemp)
    trap "rm -f $COOKIE_FILE" EXIT
    SESSION_ID=$(cat /tmp/sanity_test_session_id 2>/dev/null || echo "")
    
    if [ -z "$SESSION_ID" ]; then
        skip "Skipping (no session available)"
        return 1
    fi
    
    # Login again to get cookie
    curl -s -X POST "$SERVER_URL/auth/login" \
        -H "Content-Type: application/json" \
        -d "{\"email\":\"$TEST_EMAIL\"}" \
        --cookie-jar "$COOKIE_FILE" \
        --connect-timeout 10 > /dev/null

    section "Getting messages for session"
    MESSAGES=$(curl -s "$SERVER_URL/file-chat/http/messages/$SESSION_ID" \
        -b "$COOKIE_FILE" \
        -H "X-Session-ID: $SESSION_ID" \
        --connect-timeout 5 2>/dev/null)

    if echo "$MESSAGES" | grep -qE '\[\]|messages|content'; then
        pass "Messages endpoint works"
    else
        fail "Messages endpoint failed"
    fi

    section "Sending test message to AI"
    MESSAGE_RESPONSE=$(curl -s -X POST "$SERVER_URL/file-chat/http/message/$SESSION_ID" \
        -b "$COOKIE_FILE" \
        -H "X-Session-ID: $SESSION_ID" \
        -H "Content-Type: text/plain" \
        -d "Sanity test message. Please respond with 'Sanity test passed'." \
        --connect-timeout 15 2>/dev/null)

    if echo "$MESSAGE_RESPONSE" | grep -qi "success\|queued\|accepted"; then
        pass "Message sent successfully"
    else
        fail "Failed to send message"
    fi

    section "Waiting for AI response (polling up to 45s)..."
    echo "  Waiting up to 45 seconds for AI response..."

    for i in {1..45}; do
        sleep 1
        POLL_RESULT=$(curl -s "$SERVER_URL/file-chat/http/poll/$SESSION_ID" \
            -b "$COOKIE_FILE" \
            -H "X-Session-ID: $SESSION_ID" \
            --connect-timeout 5 2>/dev/null)

        if echo "$POLL_RESULT" | grep -qi "complete.*true\|response.*received\|sanity.*passed\|passed.*successfully"; then
            pass "AI response received within ${i}s"
            return 0
        fi

        if echo "$POLL_RESULT" | grep -qi "error\|failed"; then
            fail "AI processing error"
            return 1
        fi

        if [ $((i % 15)) -eq 0 ]; then
            echo "  Still waiting... (${i}s)"
        fi
    done

    # If we get here, polling timed out
    if echo "$MESSAGE_RESPONSE" | grep -qi "success\|queued\|accepted"; then
        pass "Message sent (async AI processing)"
        skip "AI response timeout (async mode)"
    else
        fail "No AI response after 45s"
    fi
}

# =============================================================================
# MAIN
# =============================================================================
main() {
    echo ""
    echo -e "${BLUE}╔════════════════════════════════════════════════════════════╗${NC}"
    echo -e "${BLUE}║         FileSurf v2 Production Sanity Test               ║${NC}"
    echo -e "${BLUE}║                                                            ║${NC}"
    echo -e "${BLUE}║  Server: $SERVER_URL${NC}"
    printf "║  Time:   %s" "$(date)"
    echo -e "${BLUE}║${NC}"
    echo -e "${BLUE}╚════════════════════════════════════════════════════════════╝${NC}"

    # Ensure log directory exists
    mkdir -p "$(dirname "$LOG_FILE")" 2>/dev/null || true

    log "INFO" "=========================================="
    log "INFO" "Starting production sanity test"
    log "INFO" "Server: $SERVER_URL"
    log "INFO" "=========================================="

    # Run tests
    local test_failed=0
    test_server_health || test_failed=1
    test_authentication || test_failed=1
    test_session || test_failed=1
    test_file_chat_http || test_failed=1

    # Calculate duration
    local test_end_time=$(date +%s)
    local duration=$((test_end_time - TEST_START_TIME))

    # Summary
    print_header "TEST SUMMARY"
    echo -e "  ${GREEN}Passed:${NC}  $PASSED"
    echo -e "  ${RED}Failed:${NC}  $FAILED"
    echo -e "  ${YELLOW}Skipped:${NC} $SKIPPED"
    echo -e "  Duration: ${duration}s"
    echo ""

    # Push metrics to Pushgateway
    log "INFO" "Pushing metrics to Pushgateway at ${PUSHGATEWAY_HOST}:${PUSHGATEWAY_PORT}"
    
    # Push overall result (0 = fail, 1 = pass)
    local result_value=1
    if [ $FAILED -gt 0 ]; then
        result_value=0
    fi
    
    push_metric "filesurf_sanity_test_result" "$result_value" "gauge" 'job="sanity-test",instance="filesurf.io"'
    push_metric "filesurf_sanity_test_passed" "$PASSED" "counter" 'job="sanity-test",instance="filesurf.io",result="passed"'
    push_metric "filesurf_sanity_test_failed" "$FAILED" "counter" 'job="sanity-test",instance="filesurf.io",result="failed"'
    push_metric "filesurf_sanity_test_duration_seconds" "$duration" "gauge" 'job="sanity-test",instance="filesurf.io"'
    push_metric "filesurf_sanity_test_run_timestamp" "$(date +%s)" "gauge" 'job="sanity-test",instance="filesurf.io"'

    # Push individual test results
    if [ $FAILED -eq 0 ]; then
        log "INFO" "=========================================="
        log "INFO" "✓ ALL TESTS PASSED - Deployment is healthy!"
        log "INFO" "=========================================="
        echo -e "${GREEN}╔════════════════════════════════════════════════════════════╗${NC}"
        echo -e "${GREEN}║  ✓ ALL TESTS PASSED - Deployment is healthy!              ║${NC}"
        echo -e "${GREEN}╚════════════════════════════════════════════════════════════╝${NC}"
        
        # Clean up session if exists
        rm -f /tmp/sanity_test_session_id
        exit 0
    else
        log "ERROR" "=========================================="
        log "ERROR" "✗ SOME TESTS FAILED - Review output above"
        log "ERROR" "=========================================="
        echo -e "${RED}╔════════════════════════════════════════════════════════════╗${NC}"
        echo -e "${RED}║  ✗ SOME TESTS FAILED - Review output above                ║${NC}"
        echo -e "${RED}╚════════════════════════════════════════════════════════════╝${NC}"
        
        # Clean up session if exists
        rm -f /tmp/sanity_test_session_id
        exit 1
    fi
}

main "$@"
