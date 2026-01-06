package com.filesurf;

import com.filesurf.service.SessionManager;
import io.quarkus.test.junit.QuarkusTest;
import jakarta.inject.Inject;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;

import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.List;

import static org.junit.jupiter.api.Assertions.*;

@QuarkusTest
class FileExplorerResourceTest {

    @Inject
    SessionManager sessionManager;

    @Inject
    FileExplorerResource resource;

    private String sessionId;
    private String userId;
    private Path sessionDir;

    @BeforeEach
    void setUp() throws IOException {
        sessionId = "test-session" + System.nanoTime();
        userId = "test-user" + System.nanoTime();
        sessionDir = sessionManager.initializeSession(sessionId, userId);
        // ensure clean
        Files.createDirectories(sessionDir);
    }

    @Test
    void shouldLoadIgnorePatterns() throws IOException {
        Path ignoreFile = sessionDir.resolve(".fileexplorerignore");
        Files.write(ignoreFile, List.of("# comment", "node_modules/", "*.log", "tmp"));

        var patterns = invokeLoadIgnorePatterns();
        assertFalse(patterns.isEmpty(), "Patterns should load");
    }

    @Test
    void shouldIgnoreMatchedFiles() throws IOException {
        Files.write(sessionDir.resolve(".fileexplorerignore"), List.of("node_modules/", "*.log"));
        Path nodeDir = sessionDir.resolve("node_modules");
        Path logFile = sessionDir.resolve("debug.log");
        Files.createDirectories(nodeDir);
        Files.writeString(logFile, "test");

        var patterns = invokeLoadIgnorePatterns();
        assertTrue(invokeShouldIgnore(nodeDir, patterns));
        assertTrue(invokeShouldIgnore(logFile, patterns));
    }

    @Test
    void shouldNotIgnoreOtherFiles() throws IOException {
        Files.write(sessionDir.resolve(".fileexplorerignore"), List.of("node_modules/", "*.log"));
        Path srcDir = sessionDir.resolve("src");
        Path readme = sessionDir.resolve("README.md");
        Files.createDirectories(srcDir);
        Files.writeString(readme, "hi");

        var patterns = invokeLoadIgnorePatterns();
        assertFalse(invokeShouldIgnore(srcDir, patterns));
        assertFalse(invokeShouldIgnore(readme, patterns));
    }

    private List<java.nio.file.PathMatcher> invokeLoadIgnorePatterns() {
        return resourceTestAccess().loadIgnorePatterns(sessionDir);
    }

    private boolean invokeShouldIgnore(Path path, List<java.nio.file.PathMatcher> patterns) {
        return resourceTestAccess().shouldIgnore(path, sessionDir, patterns);
    }

    /**
     * Provide access to protected helper methods via an inner subclass for testing.
     */
    private FileExplorerResourceTestProbe resourceTestAccess() {
        return new FileExplorerResourceTestProbe();
    }

    private class FileExplorerResourceTestProbe extends FileExplorerResource {
        @Override
        protected List<java.nio.file.PathMatcher> loadIgnorePatterns(Path sessionDir) {
            return super.loadIgnorePatterns(sessionDir);
        }

        @Override
        protected boolean shouldIgnore(Path path, Path sessionDir, List<java.nio.file.PathMatcher> matchers) {
            return super.shouldIgnore(path, sessionDir, matchers);
        }
    }
}
