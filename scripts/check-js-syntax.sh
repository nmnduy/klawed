#!/bin/bash

# Quick JS syntax checker
# Validates all JavaScript files for syntax errors

set -e

cd "$(dirname "$0")/.."

echo "🔍 Checking JavaScript syntax..."
node scripts/validate-js-conventions.js

exit $?
