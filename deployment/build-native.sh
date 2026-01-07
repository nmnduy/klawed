#!/bin/bash
set -e

echo "================================"
echo "FileSurf v2 Native Build Script"
echo "================================"
echo ""
echo "This will build a native executable optimized for low memory usage."
echo "Build time: 5-10 minutes on the VPS"
echo ""

# Check if GraalVM is installed
if ! command -v native-image &> /dev/null; then
    echo "ERROR: GraalVM native-image not found!"
    echo ""
    echo "Please install GraalVM first:"
    echo "  1. Download GraalVM for Java 21:"
    echo "     wget https://github.com/graalvm/graalvm-ce-builds/releases/download/jdk-21.0.2/graalvm-community-jdk-21.0.2_linux-x64_bin.tar.gz"
    echo ""
    echo "  2. Extract and install:"
    echo "     tar -xzf graalvm-community-jdk-21.0.2_linux-x64_bin.tar.gz"
    echo "     sudo mv graalvm-community-openjdk-21.0.2+13.1 /usr/lib/jvm/graalvm-21"
    echo ""
    echo "  3. Install native-image:"
    echo "     /usr/lib/jvm/graalvm-21/bin/gu install native-image"
    echo ""
    echo "  4. Set JAVA_HOME:"
    echo "     export JAVA_HOME=/usr/lib/jvm/graalvm-21"
    echo "     export PATH=\$JAVA_HOME/bin:\$PATH"
    echo ""
    exit 1
fi

echo "Step 1: Clean previous builds..."
mvn clean

echo ""
echo "Step 2: Build Tailwind CSS..."
npm run build

echo ""
echo "Step 3: Building native executable..."
echo "This will take several minutes..."
mvn package -Pnative -DskipTests -Dquarkus.profile=prod

echo ""
echo "================================"
echo "Build Complete!"
echo "================================"
echo ""
echo "Native executable location:"
echo "  target/filesurf-1.0.0-SNAPSHOT-runner"
echo ""
ls -lh target/filesurf-1.0.0-SNAPSHOT-runner
echo ""
echo "Next steps:"
echo "  1. Run ./deployment/deploy.sh to install"
echo "  2. Start the service with: systemctl start filesurf-v2"
