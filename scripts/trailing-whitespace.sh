#!/bin/bash
# Remove trailing whitespaces from files recursively
# Usage: ./trailing-whitespace.sh [directory]
# Default directory: current directory

DIR="${1:-.}"
find "$DIR" -type f \( \
    -name "*.java" -o \
    -name "*.js" -o \
    -name "*.ts" -o \
    -name "*.jsx" -o \
    -name "*.tsx" -o \
    -name "*.html" -o \
    -name "*.css" -o \
    -name "*.scss" -o \
    -name "*.json" -o \
    -name "*.xml" -o \
    -name "*.yaml" -o \
    -name "*.yml" -o \
    -name "*.sh" -o \
    -name "*.properties" -o \
    -name "*.md" -o \
    -name "*.txt" -o \
    -name "*.log" \
\) -exec sed -i 's/[[:space:]]*$//' {} \;

echo "Trailing whitespaces removed from supported files in: $DIR"
