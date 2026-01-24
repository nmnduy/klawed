#!/bin/bash
# Test script for session persistence after restart

set -e

BASE_URL="http://localhost:9090"
DB_PATH="data/sessions.db"

echo "=== FileSurf Session Persistence Test ==="
echo ""

# Step 1: Check if invited user exists
echo "Step 1: Checking for test user..."
USER_EMAIL="test@example.com"
./scripts/invite-user.sh "$USER_EMAIL" || true

# Step 2: Login and get session
echo ""
echo "Step 2: Logging in and generating session..."
LOGIN_RESPONSE=$(curl -s -c cookies.txt -X POST "$BASE_URL/auth/login" \
  -H "Content-Type: application/json" \
  -d "{\"email\":\"$USER_EMAIL\"}")

echo "Login response: $LOGIN_RESPONSE"

# Step 3: Generate session
echo ""
echo "Step 3: Generating session..."
SESSION_RESPONSE=$(curl -s -b cookies.txt -c cookies.txt "$BASE_URL/session/generate")
echo "Session response: $SESSION_RESPONSE"

SESSION_ID=$(echo "$SESSION_RESPONSE" | jq -r '.sessionId')
USER_ID=$(echo "$SESSION_RESPONSE" | jq -r '.userId')

if [ -z "$SESSION_ID" ] || [ "$SESSION_ID" == "null" ]; then
    echo "ERROR: Failed to generate session"
    exit 1
fi

echo "Session ID: $SESSION_ID"
echo "User ID: $USER_ID"

# Step 4: Check database
echo ""
echo "Step 4: Checking database for session..."
DB_RESULT=$(sqlite3 "$DB_PATH" "SELECT session_id, user_id, email, disconnected_at FROM sessions WHERE session_id='$SESSION_ID';")
echo "Database record: $DB_RESULT"

if [ -z "$DB_RESULT" ]; then
    echo "ERROR: Session not found in database"
    exit 1
fi

# Check if email is populated
EMAIL_IN_DB=$(sqlite3 "$DB_PATH" "SELECT email FROM sessions WHERE session_id='$SESSION_ID';")
if [ -z "$EMAIL_IN_DB" ] || [ "$EMAIL_IN_DB" == "null" ]; then
    echo "ERROR: Email not populated in database"
    exit 1
fi
echo "Email in DB: $EMAIL_IN_DB"

# Step 5: Check session count endpoint
echo ""
echo "Step 5: Checking session cache count..."
CACHE_COUNT=$(curl -s "$BASE_URL/session/count" | jq -r '.cachedSessions')
echo "Cached sessions: $CACHE_COUNT"

echo ""
echo "=== Test Complete ==="
echo ""
echo "MANUAL TEST REQUIRED:"
echo "1. Keep the application running"
echo "2. In another terminal, restart the application: kill the mvn process and restart"
echo "3. Run this curl command to test the session still works:"
echo ""
echo "   curl -s -b cookies.txt \"$BASE_URL/session/count\""
echo ""
echo "4. Try connecting WebSocket with session ID: $SESSION_ID"
echo "   The session should be validated from database and work correctly"
echo ""
echo "Session ID to test: $SESSION_ID"
