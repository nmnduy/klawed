#!/bin/bash

# Test script to verify metrics endpoint access control
# This script tests the /metrics endpoint with different IP headers

BASE_URL="http://localhost:8080"

echo "Testing /metrics endpoint access control..."
echo ""

echo "1. Testing WITHOUT any IP headers (should be denied):"
curl -s -o /dev/null -w "HTTP Status: %{http_code}\n" $BASE_URL/metrics
echo ""

echo "2. Testing with non-Tailscale IP via CF-Connecting-IP (should be denied):"
curl -s -o /dev/null -w "HTTP Status: %{http_code}\n" -H "CF-Connecting-IP: 192.168.1.1" $BASE_URL/metrics
echo ""

echo "3. Testing with Tailscale IP via CF-Connecting-IP (should be allowed):"
curl -s -o /dev/null -w "HTTP Status: %{http_code}\n" -H "CF-Connecting-IP: 100.64.0.1" $BASE_URL/metrics
echo ""

echo "4. Testing with Tailscale IP via X-Forwarded-For (should be allowed):"
curl -s -o /dev/null -w "HTTP Status: %{http_code}\n" -H "X-Forwarded-For: 100.64.0.1" $BASE_URL/metrics
echo ""

echo "5. Testing with non-Tailscale IP via X-Forwarded-For (should be denied):"
curl -s -o /dev/null -w "HTTP Status: %{http_code}\n" -H "X-Forwarded-For: 8.8.8.8" $BASE_URL/metrics
echo ""

echo "6. Testing /health endpoint (should work without IP restriction):"
curl -s -o /dev/null -w "HTTP Status: %{http_code}\n" $BASE_URL/q/health
echo ""

echo "Expected results:"
echo "  - Tests 1, 2, 5: HTTP 403 (Forbidden)"
echo "  - Tests 3, 4: HTTP 200 (OK) with metrics data"
echo "  - Test 6: HTTP 200 (OK)"
