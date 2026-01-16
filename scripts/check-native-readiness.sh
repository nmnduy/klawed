#!/bin/bash

# Script to check if the application is ready for native image build
# Catches common issues before deployment to production

set -e

echo "============================================"
echo "  Native Image Readiness Check"
echo "============================================"
echo ""

# Color codes for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

FAILED=0

# Check 1: Run reflection tests
echo "📋 Check 1: Running reflection/serialization tests..."
if mvn test -Dtest=NativeReflectionTest -q; then
    echo -e "${GREEN}✅ All reflection tests passed${NC}"
else
    echo -e "${RED}❌ Reflection tests failed - some DTOs may be missing @RegisterForReflection${NC}"
    FAILED=1
fi
echo ""

# Check 2: Look for REST resource classes with inner classes
echo "📋 Check 2: Scanning for REST DTOs without @RegisterForReflection..."
MISSING_ANNOTATION=0

# Find all Java files with @Path annotation (REST resources)
REST_RESOURCES=$(find src/main/java -name "*.java" -exec grep -l "@Path" {} \;)

for file in $REST_RESOURCES; do
    # Check if file has public static inner classes
    if grep -q "public static class" "$file"; then
        # Get the inner class names
        INNER_CLASSES=$(grep "public static class" "$file" | sed 's/.*public static class \([^ {]*\).*/\1/')

        for class in $INNER_CLASSES; do
            # Check if @RegisterForReflection is present near the class definition
            if ! grep -B2 "public static class $class" "$file" | grep -q "@RegisterForReflection"; then
                echo -e "${YELLOW}⚠️  Warning: $file has inner class '$class' without @RegisterForReflection${NC}"
                MISSING_ANNOTATION=1
            fi
        done
    fi
done

if [ $MISSING_ANNOTATION -eq 0 ]; then
    echo -e "${GREEN}✅ All REST DTOs have @RegisterForReflection${NC}"
else
    echo -e "${RED}❌ Some DTOs are missing @RegisterForReflection annotation${NC}"
    FAILED=1
fi
echo ""

# Check 3: Verify import of RegisterForReflection in REST resources
echo "📋 Check 3: Verifying RegisterForReflection imports..."
MISSING_IMPORT=0

for file in $REST_RESOURCES; do
    if grep -q "@RegisterForReflection" "$file"; then
        if ! grep -q "import io.quarkus.runtime.annotations.RegisterForReflection;" "$file"; then
            echo -e "${YELLOW}⚠️  Warning: $file uses @RegisterForReflection but is missing the import${NC}"
            MISSING_IMPORT=1
        fi
    fi
done

if [ $MISSING_IMPORT -eq 0 ]; then
    echo -e "${GREEN}✅ All RegisterForReflection imports are present${NC}"
else
    echo -e "${RED}❌ Some files are missing RegisterForReflection import${NC}"
    FAILED=1
fi
echo ""

# Check 4: Look for potential serialization issues
echo "📋 Check 4: Checking for DTOs without default constructors..."
echo -e "${YELLOW}ℹ️  Note: This is a heuristic check - manual verification recommended${NC}"

for file in $REST_RESOURCES; do
    if grep -q "public static class" "$file"; then
        INNER_CLASSES=$(grep "public static class" "$file" | sed 's/.*public static class \([^ {]*\).*/\1/')

        for class in $INNER_CLASSES; do
            # Extract the class definition (basic heuristic)
            # Look for explicit constructor with parameters but no default constructor
            if grep -A 20 "public static class $class" "$file" | grep -q "public $class(.*)" && \
               ! grep -A 20 "public static class $class" "$file" | grep -q "public $class()"; then
                echo -e "${YELLOW}⚠️  Warning: $file.$class may be missing a default constructor${NC}"
            fi
        done
    fi
done
echo -e "${GREEN}✅ Constructor check complete${NC}"
echo ""

# Summary
echo "============================================"
echo "  Summary"
echo "============================================"

if [ $FAILED -eq 0 ]; then
    echo -e "${GREEN}✅ All checks passed! Application appears ready for native build.${NC}"
    exit 0
else
    echo -e "${RED}❌ Some checks failed. Please fix the issues before deploying.${NC}"
    echo ""
    echo "Common fixes:"
    echo "  1. Add @RegisterForReflection to all REST DTO classes"
    echo "  2. Add default (no-arg) constructors to DTOs used with Jackson"
    echo "  3. Import: io.quarkus.runtime.annotations.RegisterForReflection"
    echo ""
    exit 1
fi
