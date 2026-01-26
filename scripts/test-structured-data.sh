#!/bin/bash

# Test script to verify JSON-LD structured data is valid

echo "Testing JSON-LD Structured Data Generation..."
echo ""

# Create a temporary Java test that generates JSON
cat > /tmp/GenerateTestJsonLD.java << 'JAVA_CODE'
public class GenerateTestJsonLD {
    private static String escapeJson(String str) {
        if (str == null) return "";
        return str.replace("\\", "\\\\")
                  .replace("\"", "\\\"")
                  .replace("\n", "\\n")
                  .replace("\r", "\\r")
                  .replace("\t", "\\t");
    }
    
    private static String generateStructuredData(
            String title, String authorName, String authorAvatar,
            String featuredImage, String datePublished, String dateModified,
            String description, String canonicalUrl, String siteName) {
        StringBuilder sb = new StringBuilder();
        sb.append("{");
        sb.append("\"@context\": \"https://schema.org\",");
        sb.append("\"@type\": \"BlogPosting\",");
        sb.append("\"headline\": \"").append(escapeJson(title)).append("\",");
        
        if (authorName != null) {
            sb.append("\"author\": {");
            sb.append("\"@type\": \"Person\",");
            sb.append("\"name\": \"").append(escapeJson(authorName)).append("\"");
            if (authorAvatar != null) {
                sb.append(",\"url\": \"").append(escapeJson(authorAvatar)).append("\"");
            }
            sb.append("},");
        }
        
        if (featuredImage != null) {
            sb.append("\"image\": \"").append(escapeJson(featuredImage)).append("\",");
        }
        
        if (datePublished != null) {
            sb.append("\"datePublished\": \"").append(datePublished).append("\",");
        }
        if (dateModified != null) {
            sb.append("\"dateModified\": \"").append(dateModified).append("\",");
        }
        
        sb.append("\"description\": \"").append(escapeJson(description)).append("\",");
        sb.append("\"mainEntityOfPage\": {");
        sb.append("\"@type\": \"WebPage\",");
        sb.append("\"@id\": \"").append(escapeJson(canonicalUrl)).append("\"");
        sb.append("},");
        sb.append("\"publisher\": {");
        sb.append("\"@type\": \"Organization\",");
        sb.append("\"name\": \"").append(escapeJson(siteName)).append("\"");
        if (featuredImage != null) {
            sb.append(",\"logo\": {\"@type\": \"ImageObject\",\"url\": \"").append(escapeJson(featuredImage)).append("\"}");
        }
        sb.append("}");
        sb.append("}");
        
        return sb.toString();
    }
    
    public static void main(String[] args) {
        String testCase = args.length > 0 ? args[0] : "1";
        
        if (testCase.equals("1")) {
            // Test Case 1: All fields present
            System.out.println(generateStructuredData(
                "Welcome to FileSurf Blog",
                "John Doe",
                "https://example.com/avatar.png",
                "https://example.com/featured.png",
                "2026-01-26T12:00:00",
                "2026-01-26T13:00:00",
                "Learn about FileSurf's powerful file management features",
                "https://filesurf.io/blog/welcome",
                "FileSurf"
            ));
        } else if (testCase.equals("2")) {
            // Test Case 2: No featured image (no logo)
            System.out.println(generateStructuredData(
                "Another Post",
                "Jane Smith",
                null,
                null,
                "2026-01-26T14:00:00",
                null,
                "A post without images",
                "https://filesurf.io/blog/another-post",
                "FileSurf"
            ));
        } else if (testCase.equals("3")) {
            // Test Case 3: With special characters
            System.out.println(generateStructuredData(
                "How to use \"quotes\" and 'apostrophes' in titles",
                "Test Author",
                null,
                "https://example.com/img.png",
                "2026-01-26T15:00:00",
                "2026-01-26T16:00:00",
                "Testing special characters: & < > \" '",
                "https://filesurf.io/blog/special-chars",
                "FileSurf"
            ));
        }
    }
}
JAVA_CODE

# Compile the test
cd /tmp
javac GenerateTestJsonLD.java

if [ $? -ne 0 ]; then
    echo "❌ Compilation failed!"
    exit 1
fi

echo "✅ Test compiled successfully"
echo ""

# Test Case 1: All fields present
echo "=== Test Case 1: All fields present ==="
json1=$(java GenerateTestJsonLD 1)
echo "$json1"
echo ""
echo "Validating JSON..."
echo "$json1" | python3 -m json.tool > /dev/null 2>&1
if [ $? -eq 0 ]; then
    echo "✅ Test Case 1 - Valid JSON"
    echo "$json1" | python3 -c "import sys, json; d = json.load(sys.stdin); print('   @type:', d['@type']); print('   headline:', d['headline']); print('   publisher.name:', d['publisher']['name']); print('   publisher.logo.url:', d['publisher']['logo']['url'])"
else
    echo "❌ Test Case 1 - INVALID JSON"
    exit 1
fi

echo ""
echo "=== Test Case 2: No featured image (no logo) ==="
json2=$(java GenerateTestJsonLD 2)
echo "$json2"
echo ""
echo "Validating JSON..."
echo "$json2" | python3 -m json.tool > /dev/null 2>&1
if [ $? -eq 0 ]; then
    echo "✅ Test Case 2 - Valid JSON"
    echo "$json2" | python3 -c "import sys, json; d = json.load(sys.stdin); print('   @type:', d['@type']); print('   headline:', d['headline']); print('   publisher.name:', d['publisher']['name']); print('   publisher has logo:', 'logo' in d['publisher'])"
else
    echo "❌ Test Case 2 - INVALID JSON"
    exit 1
fi

echo ""
echo "=== Test Case 3: Special characters ==="
json3=$(java GenerateTestJsonLD 3)
echo "$json3"
echo ""
echo "Validating JSON..."
echo "$json3" | python3 -m json.tool > /dev/null 2>&1
if [ $? -eq 0 ]; then
    echo "✅ Test Case 3 - Valid JSON"
    echo "$json3" | python3 -c "import sys, json; d = json.load(sys.stdin); print('   @type:', d['@type']); print('   headline:', d['headline']); print('   description:', d['description'])"
else
    echo "❌ Test Case 3 - INVALID JSON"
    exit 1
fi

echo ""
echo "✅ All tests passed! JSON-LD structured data is valid."
