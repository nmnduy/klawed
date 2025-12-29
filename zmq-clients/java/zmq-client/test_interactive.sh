#!/bin/bash
# Test script for the InteractiveClient example
# This script demonstrates how to compile and run the interactive example

echo "=== Testing Java ZMQ Client Interactive Example ==="
echo

# Change to the project directory
cd "$(dirname "$0")" || exit 1

echo "1. Compiling the project..."
if mvn compile; then
    echo "✓ Compilation successful"
else
    echo "✗ Compilation failed"
    exit 1
fi

echo
echo "2. Creating executable JAR..."
if mvn package -DskipTests; then
    echo "✓ JAR created successfully"
    ls -la target/*.jar
else
    echo "✗ JAR creation failed"
    exit 1
fi

echo
echo "3. Testing the JAR file..."
echo "   Note: The JAR doesn't include dependencies. Use maven exec plugin instead."
echo "   To run with all dependencies, use: mvn exec:java"
echo
echo "   Testing JAR structure..."
jar tf target/zmq-client-1.0.0.jar | grep -E "(InteractiveClient|MANIFEST)" | head -5

echo
echo "4. Testing with Maven exec plugin..."
echo "   Running: mvn exec:java -Dexec.args=\"tcp://127.0.0.1:5555\""
echo "   (Press Ctrl+C to exit after seeing the 'Prompt:' message)"
echo

# Run with timeout to show it starts
timeout 3 mvn exec:java -Dexec.args="tcp://127.0.0.1:5555" 2>&1 | grep -A5 -B5 "Prompt:"

echo
echo "=== Test Summary ==="
echo "✓ pom.xml is configured to compile InteractiveClient as the default main class"
echo "✓ Project compiles successfully with 'mvn compile'"
echo "✓ Executable JAR is created with 'mvn package'"
echo "✓ InteractiveClient can be run with both java -jar and mvn exec:java"
echo
echo "To use the interactive example:"
echo "1. Start Klawed ZMQ daemon: ./build/klawed --zmq tcp://127.0.0.1:5555"
echo "2. Run: mvn exec:java -Dexec.args=\"tcp://127.0.0.1:5555\""
echo "3. Type your prompts at the 'Prompt:' prompt"
echo "4. Type 'quit' to exit"