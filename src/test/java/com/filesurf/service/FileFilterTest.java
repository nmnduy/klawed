package com.filesurf.service;

import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.io.TempDir;

import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Arrays;
import java.util.List;

import static org.junit.jupiter.api.Assertions.*;

class FileFilterTest {
    
    @TempDir
    Path tempDir;
    
    @Test
    void testEmptyFilter() {
        FileFilter filter = new FileFilter();
        assertFalse(filter.hasPatterns());
        assertEquals(0, filter.getPatternCount());
        
        Path testFile = tempDir.resolve("test.txt");
        assertFalse(filter.shouldIgnore(testFile, tempDir));
    }
    
    @Test
    void testConstructorWithPatterns() {
        List<String> patterns = Arrays.asList("*.log", "tmp/", "*.tmp");
        FileFilter filter = new FileFilter(patterns);
        
        assertTrue(filter.hasPatterns());
        assertTrue(filter.getPatternCount() > 0);
    }
    
    @Test
    void testLoadIgnorePatternsFromFile() throws IOException {
        Path ignoreFile = tempDir.resolve(".fileexplorerignore");
        Files.write(ignoreFile, Arrays.asList(
            "# Comment line",
            "*.log",
            "tmp/",
            "*.tmp",
            "",
            "  # Another comment with spaces"
        ));
        
        FileFilter filter = new FileFilter(ignoreFile);
        assertTrue(filter.hasPatterns());
    }
    
    @Test
    void testLoadIgnorePatternsFromNonExistentFile() throws IOException {
        Path ignoreFile = tempDir.resolve("nonexistent.ignore");
        FileFilter filter = new FileFilter(ignoreFile);
        assertFalse(filter.hasPatterns());
    }
    
    @Test
    void testShouldIgnoreFileByExtension() throws IOException {
        List<String> patterns = Arrays.asList("*.log", "*.tmp");
        FileFilter filter = new FileFilter(patterns);
        
        Path logFile = tempDir.resolve("debug.log");
        Path tmpFile = tempDir.resolve("temp.tmp");
        Path textFile = tempDir.resolve("readme.txt");
        
        assertTrue(filter.shouldIgnore(logFile, tempDir));
        assertTrue(filter.shouldIgnore(tmpFile, tempDir));
        assertFalse(filter.shouldIgnore(textFile, tempDir));
    }
    
    @Test
    void testShouldIgnoreDirectory() throws IOException {
        List<String> patterns = Arrays.asList("node_modules/", "target/");
        FileFilter filter = new FileFilter(patterns);
        
        Path nodeDir = tempDir.resolve("node_modules");
        Path targetDir = tempDir.resolve("target");
        Path srcDir = tempDir.resolve("src");
        
        Files.createDirectories(nodeDir);
        Files.createDirectories(targetDir);
        Files.createDirectories(srcDir);
        
        assertTrue(filter.shouldIgnore(nodeDir, tempDir));
        assertTrue(filter.shouldIgnore(targetDir, tempDir));
        assertFalse(filter.shouldIgnore(srcDir, tempDir));
    }
    
    @Test
    void testShouldIgnoreFilesInDirectory() throws IOException {
        List<String> patterns = Arrays.asList("logs/");
        FileFilter filter = new FileFilter(patterns);
        
        Path logsDir = tempDir.resolve("logs");
        Path logFile = logsDir.resolve("app.log");
        Path nestedFile = logsDir.resolve("nested/error.log");
        
        Files.createDirectories(logsDir);
        Files.createDirectories(logsDir.resolve("nested"));
        Files.writeString(logFile, "test");
        Files.writeString(nestedFile, "test");
        
        assertTrue(filter.shouldIgnore(logsDir, tempDir));
        assertTrue(filter.shouldIgnore(logFile, tempDir));
        assertTrue(filter.shouldIgnore(nestedFile, tempDir));
    }
    
    @Test
    void testShouldIgnoreWithAbsolutePattern() throws IOException {
        List<String> patterns = Arrays.asList("/logs/app.log");
        FileFilter filter = new FileFilter(patterns);
        
        Path logsDir = tempDir.resolve("logs");
        Path appLog = logsDir.resolve("app.log");
        Path otherLog = logsDir.resolve("other.log");
        
        Files.createDirectories(logsDir);
        Files.writeString(appLog, "test");
        Files.writeString(otherLog, "test");
        
        assertTrue(filter.shouldIgnore(appLog, tempDir));
        assertFalse(filter.shouldIgnore(otherLog, tempDir));
    }
    
    @Test
    void testShouldIgnoreWithRelativePattern() throws IOException {
        List<String> patterns = Arrays.asList("logs/app.log");
        FileFilter filter = new FileFilter(patterns);
        
        Path logsDir = tempDir.resolve("logs");
        Path appLog = logsDir.resolve("app.log");
        Path otherLog = logsDir.resolve("other.log");
        
        Files.createDirectories(logsDir);
        Files.writeString(appLog, "test");
        Files.writeString(otherLog, "test");
        
        assertTrue(filter.shouldIgnore(appLog, tempDir));
        assertFalse(filter.shouldIgnore(otherLog, tempDir));
    }
    
    @Test
    void testFilterPaths() throws IOException {
        List<String> patterns = Arrays.asList("*.log", "tmp/");
        FileFilter filter = new FileFilter(patterns);
        
        Path logFile = tempDir.resolve("debug.log");
        Path tmpDir = tempDir.resolve("tmp");
        Path textFile = tempDir.resolve("readme.txt");
        Path javaFile = tempDir.resolve("Main.java");
        
        Files.createDirectories(tmpDir);
        Files.writeString(logFile, "test");
        Files.writeString(textFile, "test");
        Files.writeString(javaFile, "test");
        
        List<Path> paths = Arrays.asList(logFile, tmpDir, textFile, javaFile);
        List<Path> filtered = filter.filterPaths(paths, tempDir);
        
        assertEquals(2, filtered.size());
        assertTrue(filtered.contains(textFile));
        assertTrue(filtered.contains(javaFile));
        assertFalse(filtered.contains(logFile));
        assertFalse(filtered.contains(tmpDir));
    }
    
    @Test
    void testCompilePatterns() {
        List<String> patterns = Arrays.asList("*.log", "tmp/", "", "  ");
        List<java.nio.file.PathMatcher> matchers = FileFilter.compilePatterns(patterns);
        
        // Should compile 3 patterns: *.log, tmp/, and tmp/** (for directory)
        // Empty strings are skipped
        assertEquals(3, matchers.size());
    }
    
    @Test
    void testStaticLoadIgnorePatterns() throws IOException {
        Path ignoreFile = tempDir.resolve(".testignore");
        Files.write(ignoreFile, Arrays.asList(
            "*.log",
            "tmp/",
            "# Comment",
            ""
        ));
        
        List<java.nio.file.PathMatcher> matchers = FileFilter.loadIgnorePatterns(ignoreFile);
        assertFalse(matchers.isEmpty());
    }
    
    @Test
    void testShouldNotIgnoreWhenNoBaseDirMatch() throws IOException {
        List<String> patterns = Arrays.asList("subdir/*.log");
        FileFilter filter = new FileFilter(patterns);
        
        Path logFile = tempDir.resolve("debug.log"); // Not in subdir
        Files.writeString(logFile, "test");
        
        assertFalse(filter.shouldIgnore(logFile, tempDir));
    }
    
    @Test
    void testShouldIgnoreWithBackslashesInPattern() throws IOException {
        List<String> patterns = Arrays.asList("logs\\*.log"); // Backslash in pattern
        FileFilter filter = new FileFilter(patterns);
        
        Path logFile = tempDir.resolve("logs").resolve("app.log");
        Files.createDirectories(logFile.getParent());
        Files.writeString(logFile, "test");
        
        assertTrue(filter.shouldIgnore(logFile, tempDir));
    }
    
    @Test
    void testShouldHandleEmptyPatternGracefully() {
        List<String> patterns = Arrays.asList("");
        FileFilter filter = new FileFilter(patterns);
        
        // Should not crash, just have no patterns
        assertFalse(filter.hasPatterns());
    }
    
    @Test
    void testShouldHandleWhitespaceOnlyPattern() {
        List<String> patterns = Arrays.asList("   ");
        FileFilter filter = new FileFilter(patterns);
        
        // Should not crash, just have no patterns
        assertFalse(filter.hasPatterns());
    }
    
    @Test
    void testShouldIgnoreNestedPaths() throws IOException {
        List<String> patterns = Arrays.asList("build/");
        FileFilter filter = new FileFilter(patterns);
        
        Path buildDir = tempDir.resolve("build");
        Path classesDir = buildDir.resolve("classes");
        Path nestedFile = classesDir.resolve("Main.class");
        
        Files.createDirectories(classesDir);
        Files.writeString(nestedFile, "test");
        
        assertTrue(filter.shouldIgnore(buildDir, tempDir));
        assertTrue(filter.shouldIgnore(classesDir, tempDir));
        assertTrue(filter.shouldIgnore(nestedFile, tempDir));
    }
    
    @Test
    void testShouldNotIgnoreSimilarNames() throws IOException {
        List<String> patterns = Arrays.asList("*.log");
        FileFilter filter = new FileFilter(patterns);
        
        Path logFile = tempDir.resolve("debug.log");
        Path logBackup = tempDir.resolve("debug.log.bak");
        Path logDir = tempDir.resolve("logs");
        
        Files.writeString(logFile, "test");
        Files.writeString(logBackup, "test");
        Files.createDirectories(logDir);
        
        assertTrue(filter.shouldIgnore(logFile, tempDir));
        assertFalse(filter.shouldIgnore(logBackup, tempDir)); // .log.bak doesn't match *.log
        assertFalse(filter.shouldIgnore(logDir, tempDir)); // directory doesn't match *.log
    }
}