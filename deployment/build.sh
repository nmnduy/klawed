#!/bin/bash
set -e

echo "================================"
echo "FileSurf v2 Build Script"
echo "================================"
echo ""
echo "Building optimized package"
echo ""

# Set JAVA_HOME for Maven (use GraalVM if available, fallback to system Java)
if [ -d "/home/fandalf/.local/graalvm-jdk-21.0.8+12.1" ]; then
    export JAVA_HOME=/home/fandalf/.local/graalvm-jdk-21.0.8+12.1
elif [ -d "/usr/lib/jvm/java-21-openjdk-amd64" ]; then
    export JAVA_HOME=/usr/lib/jvm/java-21-openjdk-amd64
else
    # Use JAVA_HOME from environment or let Maven detect it
    export JAVA_HOME=${JAVA_HOME:-$(dirname $(dirname $(readlink -f $(which java))))}
fi
export PATH=$JAVA_HOME/bin:$PATH

echo "Step 1: Clean previous builds..."
mvn clean

echo ""
echo "Step 2: Build Tailwind CSS..."
npm run build

echo ""
echo "Step 3: Building Quarkus application..."
mvn package -DskipTests -Dquarkus.profile=prod

echo ""
echo "================================"
echo "Build Complete!"
echo "================================"
echo ""
echo "Application location:"
echo "  target/quarkus-app/quarkus-run.jar"
echo ""
ls -lh target/quarkus-app/quarkus-run.jar
echo ""
echo "To test locally:"
echo "  java -Dquarkus.profile=prod -jar target/quarkus-app/quarkus-run.jar"
echo ""
echo "To deploy:"
echo "  1. Copy deployment/filesurf-v2.service to /etc/systemd/system/"
echo "  2. Run: systemctl daemon-reload"
echo "  3. Run: systemctl enable filesurf-v2"
echo "  4. Run: systemctl start filesurf-v2"
