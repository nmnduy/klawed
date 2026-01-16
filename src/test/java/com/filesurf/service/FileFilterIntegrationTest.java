package com.filesurf.service;

import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.io.TempDir;

import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Arrays;
import java.util.List;

import static org.junit.jupiter.api.Assertions.*;

/**
 * Integration test to demonstrate FileFilter usage and address the PDF visibility issue.
 * This test shows how the .fileexplorerignore file is parsed and used.
 */
class FileFilterIntegrationTest {

    @TempDir
    Path tempDir;

    @Test
    void testPdfFileVisibility() throws IOException {
        // Create a .fileexplorerignore file similar to the real one
        Path ignoreFile = tempDir.resolve(".fileexplorerignore");
        Files.write(ignoreFile, Arrays.asList(
            "# Patterns hidden from the File Explorer",
            "# Shown to users so they know what is excluded",
            "node_modules/",
            "target/",
            "build/",
            "logs/",
            ".git/",
            ".DS_Store",
            "*.log",
            "*.tmp",
            "*.sqlite",
            "*.db",
            "*.db-*",
            "*.class",
            "*.jar",
            ".klawed/",
            "klawed.pid",
            "KLAWED.md",
            "SKILLS/",
            ".fileexplorerignore"
        ));

        // Create various files in the directory
        Files.createDirectories(tempDir.resolve("node_modules"));
        Files.createDirectories(tempDir.resolve("target"));
        Files.createDirectories(tempDir.resolve("build"));
        Files.createDirectories(tempDir.resolve("logs"));

        // Create files that should be ignored
        Files.writeString(tempDir.resolve("debug.log"), "log content");
        Files.writeString(tempDir.resolve("temp.tmp"), "tmp content");
        Files.writeString(tempDir.resolve("test.db"), "db content");
        Files.writeString(tempDir.resolve("Main.class"), "class content");

        // Create files that should NOT be ignored (including PDFs)
        Files.writeString(tempDir.resolve("invoice.pdf"), "pdf content");
        Files.writeString(tempDir.resolve("document.pdf"), "pdf content");
        Files.writeString(tempDir.resolve("report.pdf"), "pdf content");
        Files.writeString(tempDir.resolve("README.md"), "readme content");
        Files.writeString(tempDir.resolve("Main.java"), "java code");
        Files.writeString(tempDir.resolve("data.json"), "json data");

        // Create a LaTeX file and its compiled PDF
        Files.writeString(tempDir.resolve("invoice.tex"), "\\documentclass{article}\\begin{document}Invoice\\end{document}");
        Files.writeString(tempDir.resolve("invoice.pdf"), "compiled pdf content");

        // Create FileFilter from the ignore file
        FileFilter filter = new FileFilter(ignoreFile);

        // Test that ignored files are correctly filtered
        assertTrue(filter.shouldIgnore(tempDir.resolve("debug.log"), tempDir));
        assertTrue(filter.shouldIgnore(tempDir.resolve("temp.tmp"), tempDir));
        assertTrue(filter.shouldIgnore(tempDir.resolve("test.db"), tempDir));
        assertTrue(filter.shouldIgnore(tempDir.resolve("Main.class"), tempDir));
        assertTrue(filter.shouldIgnore(tempDir.resolve("node_modules"), tempDir));
        assertTrue(filter.shouldIgnore(tempDir.resolve("target"), tempDir));
        assertTrue(filter.shouldIgnore(tempDir.resolve("build"), tempDir));
        assertTrue(filter.shouldIgnore(tempDir.resolve("logs"), tempDir));

        // Test that PDF files and other non-ignored files are NOT filtered
        assertFalse(filter.shouldIgnore(tempDir.resolve("invoice.pdf"), tempDir));
        assertFalse(filter.shouldIgnore(tempDir.resolve("document.pdf"), tempDir));
        assertFalse(filter.shouldIgnore(tempDir.resolve("report.pdf"), tempDir));
        assertFalse(filter.shouldIgnore(tempDir.resolve("README.md"), tempDir));
        assertFalse(filter.shouldIgnore(tempDir.resolve("Main.java"), tempDir));
        assertFalse(filter.shouldIgnore(tempDir.resolve("data.json"), tempDir));
        assertFalse(filter.shouldIgnore(tempDir.resolve("invoice.tex"), tempDir));

        // Test filterPaths method
        List<Path> allFiles = Arrays.asList(
            tempDir.resolve("debug.log"),
            tempDir.resolve("temp.tmp"),
            tempDir.resolve("invoice.pdf"),
            tempDir.resolve("README.md"),
            tempDir.resolve("Main.java"),
            tempDir.resolve("node_modules"),
            tempDir.resolve("invoice.tex")
        );

        List<Path> filtered = filter.filterPaths(allFiles, tempDir);

        // Should only keep non-ignored files
        assertEquals(4, filtered.size());
        assertTrue(filtered.contains(tempDir.resolve("invoice.pdf")));
        assertTrue(filtered.contains(tempDir.resolve("README.md")));
        assertTrue(filtered.contains(tempDir.resolve("Main.java")));
        assertTrue(filtered.contains(tempDir.resolve("invoice.tex")));
        assertFalse(filtered.contains(tempDir.resolve("debug.log")));
        assertFalse(filtered.contains(tempDir.resolve("temp.tmp")));
        assertFalse(filtered.contains(tempDir.resolve("node_modules")));
    }

    @Test
    void testCustomIgnorePatterns() throws IOException {
        // Test with custom ignore patterns that might hide PDFs
        Path ignoreFile = tempDir.resolve(".customignore");
        Files.write(ignoreFile, Arrays.asList(
            "*.pdf",  // This would hide PDF files
            "*.tmp",
            "temp/"
        ));

        // Create files
        Files.writeString(tempDir.resolve("document.pdf"), "pdf content");
        Files.writeString(tempDir.resolve("data.tmp"), "tmp content");
        Files.createDirectories(tempDir.resolve("temp"));
        Files.writeString(tempDir.resolve("README.md"), "readme content");

        FileFilter filter = new FileFilter(ignoreFile);

        // With this ignore file, PDFs would be hidden
        assertTrue(filter.shouldIgnore(tempDir.resolve("document.pdf"), tempDir));
        assertTrue(filter.shouldIgnore(tempDir.resolve("data.tmp"), tempDir));
        assertTrue(filter.shouldIgnore(tempDir.resolve("temp"), tempDir));
        assertFalse(filter.shouldIgnore(tempDir.resolve("README.md"), tempDir));
    }

    @Test
    void testNoIgnoreFile() throws IOException {
        // When no ignore file exists, no files should be filtered
        FileFilter filter = new FileFilter();

        Files.writeString(tempDir.resolve("invoice.pdf"), "pdf content");
        Files.writeString(tempDir.resolve("debug.log"), "log content");
        Files.createDirectories(tempDir.resolve("node_modules"));

        assertFalse(filter.shouldIgnore(tempDir.resolve("invoice.pdf"), tempDir));
        assertFalse(filter.shouldIgnore(tempDir.resolve("debug.log"), tempDir));
        assertFalse(filter.shouldIgnore(tempDir.resolve("node_modules"), tempDir));
    }

    @Test
    void testEmptyIgnoreFile() throws IOException {
        // Empty ignore file should not filter anything
        Path ignoreFile = tempDir.resolve(".emptyignore");
        Files.write(ignoreFile, Arrays.asList("# Comment only", "", "  "));

        FileFilter filter = new FileFilter(ignoreFile);

        Files.writeString(tempDir.resolve("invoice.pdf"), "pdf content");
        Files.writeString(tempDir.resolve("debug.log"), "log content");

        assertFalse(filter.shouldIgnore(tempDir.resolve("invoice.pdf"), tempDir));
        assertFalse(filter.shouldIgnore(tempDir.resolve("debug.log"), tempDir));
    }

    @Test
    void testPatternWithSpaces() throws IOException {
        // Test patterns with leading/trailing spaces
        Path ignoreFile = tempDir.resolve(".spacesignore");
        Files.write(ignoreFile, Arrays.asList(
            "  *.log  ",  // Pattern with spaces
            "  tmp/   "   // Directory pattern with spaces
        ));

        FileFilter filter = new FileFilter(ignoreFile);

        Files.writeString(tempDir.resolve("debug.log"), "log content");
        Files.createDirectories(tempDir.resolve("tmp"));
        Files.writeString(tempDir.resolve("README.md"), "readme content");

        // Spaces should be trimmed, so patterns should still work
        assertTrue(filter.shouldIgnore(tempDir.resolve("debug.log"), tempDir));
        assertTrue(filter.shouldIgnore(tempDir.resolve("tmp"), tempDir));
        assertFalse(filter.shouldIgnore(tempDir.resolve("README.md"), tempDir));
    }
}