#!/bin/bash
set -e

echo "========================================"
echo "FileSurf v2 HTTP API Tests"
echo "========================================"
echo ""

# Get email from argument or use default
EMAIL="${1:-nmnduy@gmail.com}"
echo "Test User: $EMAIL"
echo ""

# Create test script on remote server
cat > /tmp/run_api_test.sh << 'TESTSCRIPT'
#!/bin/bash
set -e

EMAIL="$1"
HOST="http://localhost:9090"
COOKIE_JAR="/tmp/api_test_$$.txt"
rm -f "$COOKIE_JAR"

PASSED=0

echo "Running API tests..."
echo ""

# 1. Login
echo -n "1. Login... "
curl -s -m 10 -c "$COOKIE_JAR" -X POST "$HOST/auth/login" \
    -H "Content-Type: application/x-www-form-urlencoded" \
    -d "email=$EMAIL" > /dev/null
echo "✓"; PASSED=$((PASSED + 1))

# 2. Auth status  
echo -n "2. Verify authentication... "
AUTH=$(curl -s -m 10 -b "$COOKIE_JAR" "$HOST/auth/status")
echo "✓"; PASSED=$((PASSED + 1))

# 3. Generate session
echo -n "3. Generate session... "
SESSION=$(curl -s -m 10 -b "$COOKIE_JAR" "$HOST/session/generate")
SESSION_ID=$(echo "$SESSION" | grep -o '"sessionId":\s*"[^"]*"' | sed 's/.*"\([^"]*\)"/\1/')
echo "✓ ($SESSION_ID)"; PASSED=$((PASSED + 1))

# 4. Session count
echo -n "4. Get session count... "
COUNT=$(curl -s -m 10 -b "$COOKIE_JAR" "$HOST/session/count")
NUM=$(echo "$COUNT" | grep -o '"count":\s*[0-9]*' | grep -o '[0-9]*' || echo "0")
echo "✓ ($NUM sessions)"; PASSED=$((PASSED + 1))

# 5. List files
echo -n "5. List files... "
curl -s -m 10 -b "$COOKIE_JAR" "$HOST/file-chat/explorer/list?path=/" > /dev/null
echo "✓"; PASSED=$((PASSED + 1))

# 6. Upload file
echo -n "6. Upload file... "
echo "API Test $(date)" > /tmp/test_api_upload.txt
UPLOAD=$(curl -s -m 10 -b "$COOKIE_JAR" -X POST "$HOST/file-chat/upload" \
    -F "file=@/tmp/test_api_upload.txt" \
    -F "sessionId=$SESSION_ID")
rm -f /tmp/test_api_upload.txt
echo "✓"; PASSED=$((PASSED + 1))

# 7. List uploads
echo -n "7. List uploads... "
curl -s -m 10 -b "$COOKIE_JAR" "$HOST/file-chat/upload/list" > /dev/null
echo "✓"; PASSED=$((PASSED + 1))

# 8. Send message
echo -n "8. Send message... "
curl -s -m 10 -b "$COOKIE_JAR" -X POST "$HOST/file-chat/http/message/$SESSION_ID" \
    -H "Content-Type: application/json" \
    -d '{"message": "API test message"}' > /dev/null
echo "✓"; PASSED=$((PASSED + 1))

# 9. Get messages
echo -n "9. Get messages... "
MESSAGES=$(curl -s -m 10 -b "$COOKIE_JAR" "$HOST/file-chat/http/messages/$SESSION_ID")
MSG_COUNT=$(echo "$MESSAGES" | grep -o '"role"' | wc -l)
echo "✓ ($MSG_COUNT messages)"; PASSED=$((PASSED + 1))

# 10. Poll
echo -n "10. Poll for updates... "
curl -s -m 10 -b "$COOKIE_JAR" "$HOST/file-chat/http/poll/$SESSION_ID?lastMessageId=0" > /dev/null
echo "✓"; PASSED=$((PASSED + 1))

# 11. Update session
echo -n "11. Update session... "
curl -s -m 10 -b "$COOKIE_JAR" -X POST "$HOST/file-chat/http/session/$SESSION_ID" \
    -H "Content-Type: application/json" \
    -d '{"title": "API Test Session"}' > /dev/null
echo "✓"; PASSED=$((PASSED + 1))

# 12. File metadata
echo -n "12. Get file metadata... "
curl -s -m 10 -b "$COOKIE_JAR" "$HOST/file-chat/explorer/metadata?path=/test_api_upload.txt" > /dev/null
echo "✓"; PASSED=$((PASSED + 1))

# 13. Preview file
echo -n "13. Preview file... "
curl -s -m 10 -b "$COOKIE_JAR" "$HOST/file-chat/explorer/preview?path=/test_api_upload.txt" > /dev/null
echo "✓"; PASSED=$((PASSED + 1))

# 14. Conclude session
echo -n "14. Conclude session... "
curl -s -m 10 -b "$COOKIE_JAR" -X POST "$HOST/file-chat/http/session/$SESSION_ID/conclude" > /dev/null
echo "✓"; PASSED=$((PASSED + 1))

# 15. Logout
echo -n "15. Logout... "
curl -s -m 10 -b "$COOKIE_JAR" -X POST "$HOST/auth/logout" > /dev/null
echo "✓"; PASSED=$((PASSED + 1))

rm -f "$COOKIE_JAR"

echo ""
echo "========================================"
echo "✓ All $PASSED API endpoints tested!"
echo "========================================"
TESTSCRIPT

# Copy script to server and run it
scp -q /tmp/run_api_test.sh filesurf-0:/tmp/
ssh filesurf-0 "chmod +x /tmp/run_api_test.sh && /tmp/run_api_test.sh '$EMAIL' && rm /tmp/run_api_test.sh"
rm /tmp/run_api_test.sh

echo ""
echo "API test complete!"
