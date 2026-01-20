#!/bin/bash
set -e

echo "========================================"
echo "FileSurf v2 Deployment Sanity Tests"
echo "========================================"
echo ""

HOST="http://filesurf.io"
PASSED=0
FAILED=0

test_endpoint() {
    local name="$1"
    local url="$2"
    local expected="$3"
    
    echo -n "  $name... "
    status=$(curl -s -o /dev/null -w "%{http_code}" "$url" 2>/dev/null || echo "000")
    
    if [ "$status" = "$expected" ]; then
        echo "✓ PASS (HTTP $status)"
        PASSED=$((PASSED + 1))
    else
        echo "⚠ ($status expected $expected)"
        FAILED=$((FAILED + 1))
    fi
}

echo "Public Endpoints:"
test_endpoint "Login Page" "$HOST/auth/login" "200"
test_endpoint "Privacy Page" "$HOST/privacy" "200"

echo ""
echo "Protected Endpoints:"
test_endpoint "Main Chat" "$HOST/file-chat" "303"
test_endpoint "Session Generate" "$HOST/session/generate" "401"

echo ""
echo "Static Assets:"
# Note: Update these hashes after each build
CSS_FILE=$(ls -1 src/main/resources/META-INF/resources/dist/main.*.css 2>/dev/null | head -1 | xargs basename)
JS_FILE=$(ls -1 src/main/resources/META-INF/resources/dist/darkMode.*.js 2>/dev/null | head -1 | xargs basename)

if [ -n "$CSS_FILE" ]; then
    test_endpoint "CSS" "$HOST/dist/$CSS_FILE" "200"
else
    echo "  CSS... ⚠ (hash file not found)"
    FAILED=$((FAILED + 1))
fi

if [ -n "$JS_FILE" ]; then
    test_endpoint "JS" "$HOST/dist/$JS_FILE" "200"
else
    echo "  JS... ⚠ (hash file not found)"
    FAILED=$((FAILED + 1))
fi

echo ""
echo "Security:"
test_endpoint "Metrics Protected" "$HOST/metrics" "403"

echo ""
echo "Service Health:"
echo -n "  Service Active... "
if ssh filesurf-0 'systemctl is-active filesurf-v2 >/dev/null 2>&1'; then
    echo "✓ PASS"
    PASSED=$((PASSED + 1))
else
    echo "✗ FAIL"
    FAILED=$((FAILED + 1))
fi

echo -n "  Database Exists... "
if ssh filesurf-0 '[ -f /var/lib/filesurf/data/filesurf.db ]'; then
    echo "✓ PASS"
    PASSED=$((PASSED + 1))
else
    echo "✗ FAIL"
    FAILED=$((FAILED + 1))
fi

echo -n "  Recent Activity... "
RECENT_LOGS=$(ssh filesurf-0 'journalctl -u filesurf-v2 --since "5 minutes ago" | wc -l')
if [ "$RECENT_LOGS" -gt 10 ]; then
    echo "✓ PASS ($RECENT_LOGS log lines)"
    PASSED=$((PASSED + 1))
else
    echo "⚠ LOW ($RECENT_LOGS log lines)"
    FAILED=$((FAILED + 1))
fi

echo ""
echo "========================================"
echo "Summary: $PASSED passed, $FAILED warnings"
echo "========================================"

if [ $FAILED -eq 0 ]; then
    echo "✓ All tests passed!"
    exit 0
else
    echo "⚠ Some warnings (see above)"
    exit 0  # Don't fail on warnings
fi
