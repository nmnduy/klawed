#!/bin/bash

# check-hardcoded-colors.sh
# Scans template files for hardcoded color values that should use CSS variables instead.
# Usage: ./scripts/check-hardcoded-colors.sh [--strict]
#
# Exit codes:
#   0 - No hardcoded colors found
#   1 - Hardcoded colors found (warning mode)
#   2 - Error in script execution

# Configuration
TEMPLATES_DIR="src/main/resources/templates"
STRICT_MODE=false

# Colors for output
RED='\033[0;31m'
YELLOW='\033[1;33m'
GREEN='\033[0;32m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Parse arguments
if [[ "${1:-}" == "--strict" ]]; then
    STRICT_MODE=true
fi

echo -e "${BLUE}================================================${NC}"
echo -e "${BLUE}  Checking for Hardcoded Colors in Templates${NC}"
echo -e "${BLUE}================================================${NC}"
echo

# Check if templates directory exists
if [ ! -d "$TEMPLATES_DIR" ]; then
    echo -e "${RED}ERROR: Templates directory not found: $TEMPLATES_DIR${NC}"
    exit 2
fi

# Count template files
TOTAL_FILES=$(find "$TEMPLATES_DIR" -type f -name "*.html" | wc -l)
echo -e "${BLUE}Scanning $TOTAL_FILES template files...${NC}"
echo

# Initialize counters
HEX_COUNT=0
RGB_COUNT=0
TAILWIND_COUNT=0
HSL_COUNT=0

# Temporary files for findings
TMP_DIR=$(mktemp -d)
trap "rm -rf $TMP_DIR" EXIT

# 1. Check for hex colors (e.g., #fff, #ffffff)
# Exclude: those within var() context
echo "Checking for hex colors..."
if grep -rn -E "(^|[^a-zA-Z0-9-])#[0-9a-fA-F]{3,6}([^0-9a-fA-F]|$)" "$TEMPLATES_DIR" 2>/dev/null | \
   grep -v "var(--" > "$TMP_DIR/hex.txt" 2>/dev/null; then
    HEX_COUNT=$(wc -l < "$TMP_DIR/hex.txt")
fi

# 2. Check for RGB/RGBA colors
# Exclude: those within var() context
echo "Checking for RGB/RGBA colors..."
if grep -rn -E "rgba?\([0-9]+,\s*[0-9]+,\s*[0-9]+" "$TEMPLATES_DIR" 2>/dev/null | \
   grep -v "var(--" > "$TMP_DIR/rgb.txt" 2>/dev/null; then
    RGB_COUNT=$(wc -l < "$TMP_DIR/rgb.txt")
fi

# 3. Check for Tailwind color utilities (e.g., bg-orange-500, text-blue-600)
# Exclude: theme colors like bg-background, text-foreground, etc.
echo "Checking for Tailwind color classes..."
if grep -rn -E "(bg|text|border|ring|from|to|via|fill|stroke|divide|placeholder|caret)-(red|orange|amber|yellow|lime|green|emerald|teal|cyan|sky|blue|indigo|violet|purple|fuchsia|pink|rose|gray|slate|zinc|neutral|stone)(-[0-9]+)?" "$TEMPLATES_DIR" 2>/dev/null > "$TMP_DIR/tailwind.txt"; then
    TAILWIND_COUNT=$(wc -l < "$TMP_DIR/tailwind.txt")
fi

# 4. Check for HSL/HSLA colors (but not hsl(var(...)))
echo "Checking for HSL/HSLA colors..."
if grep -rn -E "hsla?\([0-9]+,\s*[0-9]+%,\s*[0-9]+%" "$TEMPLATES_DIR" 2>/dev/null | \
   grep -v "hsl(var(--" > "$TMP_DIR/hsl.txt" 2>/dev/null; then
    HSL_COUNT=$(wc -l < "$TMP_DIR/hsl.txt")
fi

# Calculate total
TOTAL_HARDCODED=$((HEX_COUNT + RGB_COUNT + TAILWIND_COUNT + HSL_COUNT))

# Display results
echo
echo -e "${BLUE}Results:${NC}"
echo -e "${BLUE}--------${NC}"
echo

if [ $TOTAL_HARDCODED -eq 0 ]; then
    echo -e "${GREEN}✓ No hardcoded colors found!${NC}"
    echo -e "${GREEN}  All templates use CSS variables or theme classes.${NC}"
    echo
    exit 0
fi

# Display summary
echo -e "${YELLOW}⚠ Found $TOTAL_HARDCODED hardcoded color values:${NC}"
echo

if [ $HEX_COUNT -gt 0 ]; then
    echo -e "  ${YELLOW}• Hex colors: $HEX_COUNT${NC}"
fi
if [ $RGB_COUNT -gt 0 ]; then
    echo -e "  ${YELLOW}• RGB/RGBA colors: $RGB_COUNT${NC}"
fi
if [ $TAILWIND_COUNT -gt 0 ]; then
    echo -e "  ${YELLOW}• Tailwind color classes: $TAILWIND_COUNT${NC}"
fi
if [ $HSL_COUNT -gt 0 ]; then
    echo -e "  ${YELLOW}• HSL/HSLA colors: $HSL_COUNT${NC}"
fi

echo
echo -e "${BLUE}Recommendations:${NC}"
echo -e "  • Use CSS variables: ${GREEN}hsl(var(--primary))${NC} instead of ${RED}#ff6b35${NC}"
echo -e "  • Use theme classes: ${GREEN}bg-primary${NC} instead of ${RED}bg-orange-500${NC}"
echo -e "  • Use theme classes: ${GREEN}text-foreground${NC} instead of ${RED}text-gray-900${NC}"
echo -e "  • For transparency: ${GREEN}color-mix(in srgb, hsl(var(--primary)) 50%, transparent)${NC}"
echo

# Display sample findings (first 20)
echo -e "${BLUE}Sample Findings (showing first 20):${NC}"
echo -e "${BLUE}-----------------------------------${NC}"

SHOWN=0
MAX_SHOW=20

# Show hex colors
if [ -f "$TMP_DIR/hex.txt" ] && [ $SHOWN -lt $MAX_SHOW ]; then
    while IFS= read -r line && [ $SHOWN -lt $MAX_SHOW ]; do
        file=$(echo "$line" | cut -d: -f1 | xargs basename)
        linenum=$(echo "$line" | cut -d: -f2)
        content=$(echo "$line" | cut -d: -f3- | sed 's/^[[:space:]]*//' | cut -c1-80)
        echo -e "${YELLOW}  [HEX] $file:$linenum${NC} $content"
        ((SHOWN++))
    done < "$TMP_DIR/hex.txt"
fi

# Show RGB colors
if [ -f "$TMP_DIR/rgb.txt" ] && [ $SHOWN -lt $MAX_SHOW ]; then
    while IFS= read -r line && [ $SHOWN -lt $MAX_SHOW ]; do
        file=$(echo "$line" | cut -d: -f1 | xargs basename)
        linenum=$(echo "$line" | cut -d: -f2)
        content=$(echo "$line" | cut -d: -f3- | sed 's/^[[:space:]]*//' | cut -c1-80)
        echo -e "${YELLOW}  [RGB] $file:$linenum${NC} $content"
        ((SHOWN++))
    done < "$TMP_DIR/rgb.txt"
fi

# Show Tailwind colors
if [ -f "$TMP_DIR/tailwind.txt" ] && [ $SHOWN -lt $MAX_SHOW ]; then
    while IFS= read -r line && [ $SHOWN -lt $MAX_SHOW ]; do
        file=$(echo "$line" | cut -d: -f1 | xargs basename)
        linenum=$(echo "$line" | cut -d: -f2)
        content=$(echo "$line" | cut -d: -f3- | sed 's/^[[:space:]]*//' | cut -c1-80)
        echo -e "${YELLOW}  [TW] $file:$linenum${NC} $content"
        ((SHOWN++))
    done < "$TMP_DIR/tailwind.txt"
fi

# Show HSL colors
if [ -f "$TMP_DIR/hsl.txt" ] && [ $SHOWN -lt $MAX_SHOW ]; then
    while IFS= read -r line && [ $SHOWN -lt $MAX_SHOW ]; do
        file=$(echo "$line" | cut -d: -f1 | xargs basename)
        linenum=$(echo "$line" | cut -d: -f2)
        content=$(echo "$line" | cut -d: -f3- | sed 's/^[[:space:]]*//' | cut -c1-80)
        echo -e "${YELLOW}  [HSL] $file:$linenum${NC} $content"
        ((SHOWN++))
    done < "$TMP_DIR/hsl.txt"
fi

if [ $TOTAL_HARDCODED -gt $MAX_SHOW ]; then
    REMAINING=$((TOTAL_HARDCODED - MAX_SHOW))
    echo -e "${YELLOW}  ... and $REMAINING more occurrences${NC}"
fi

echo

# Exit based on mode
if [ "$STRICT_MODE" = true ]; then
    echo -e "${RED}ERROR: Hardcoded colors found in strict mode${NC}"
    exit 1
else
    echo -e "${YELLOW}WARNING: Hardcoded colors detected${NC}"
    echo -e "${YELLOW}Run with --strict to fail the build on hardcoded colors${NC}"
    exit 1
fi
