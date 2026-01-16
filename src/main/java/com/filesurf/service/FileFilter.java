package com.filesurf.service;

import java.io.IOException;
import java.nio.file.FileSystems;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.PathMatcher;
import java.util.ArrayList;
import java.util.List;
import java.util.logging.Logger;

/**
 * FileFilter provides functionality to filter files based on ignore patterns
 * similar to .gitignore syntax. It loads patterns from a file and can determine
 * whether a given path should be ignored.
 */
public class FileFilter {

    private static final Logger LOGGER = Logger.getLogger(FileFilter.class.getName());

    private final List<PathMatcher> ignorePatterns;

    /**
     * Creates a FileFilter with no ignore patterns.
     */
    public FileFilter() {
        this.ignorePatterns = new ArrayList<>();
    }

    /**
     * Creates a FileFilter and loads ignore patterns from the specified file.
     *
     * @param ignoreFile Path to the ignore file (e.g., .fileexplorerignore)
     * @throws IOException if there's an error reading the ignore file
     */
    public FileFilter(Path ignoreFile) throws IOException {
        this.ignorePatterns = loadIgnorePatterns(ignoreFile);
    }

    /**
     * Creates a FileFilter with the given ignore patterns.
     *
     * @param ignorePatterns List of ignore patterns in glob syntax
     */
    public FileFilter(List<String> ignorePatterns) {
        this.ignorePatterns = compilePatterns(ignorePatterns);
    }

    /**
     * Loads ignore patterns from a file.
     *
     * @param ignoreFile Path to the ignore file
     * @return List of compiled PathMatcher objects
     * @throws IOException if there's an error reading the file
     */
    public static List<PathMatcher> loadIgnorePatterns(Path ignoreFile) throws IOException {
        List<PathMatcher> matchers = new ArrayList<>();

        if (!Files.exists(ignoreFile)) {
            LOGGER.fine("Ignore file not found at: " + ignoreFile);
            return matchers;
        }

        List<String> lines = Files.readAllLines(ignoreFile);
        LOGGER.fine("Read " + lines.size() + " lines from ignore file: " + ignoreFile);

        List<String> patterns = new ArrayList<>();
        for (String raw : lines) {
            String line = raw.strip();
            if (line.isEmpty() || line.startsWith("#")) {
                LOGGER.finest("Skipping comment/empty line: " + line);
                continue;
            }
            patterns.add(line);
        }

        return compilePatterns(patterns);
    }

    /**
     * Compiles a list of pattern strings into PathMatcher objects.
     *
     * @param patterns List of pattern strings
     * @return List of compiled PathMatcher objects
     */
    public static List<PathMatcher> compilePatterns(List<String> patterns) {
        List<PathMatcher> matchers = new ArrayList<>();

        for (String rawPattern : patterns) {
            String pattern = rawPattern.strip();
            if (pattern.isEmpty()) {
                LOGGER.warning("Empty pattern found, skipping");
                continue;
            }

            LOGGER.fine("Processing ignore pattern: '" + pattern + "'");

            // Normalize to forward slashes
            pattern = pattern.replace('\\', '/');
            boolean dirPattern = pattern.endsWith("/");

            if (dirPattern) {
                pattern = pattern.substring(0, pattern.length() - 1);
                LOGGER.finest("  Detected directory pattern, trimmed to: '" + pattern + "'");
            }

            if (pattern.isEmpty()) {
                LOGGER.warning("Empty pattern after trimming");
                continue;
            }

            // Create glob pattern
            String glob = pattern.startsWith("/") ? pattern : "/" + pattern;
            String basePattern = "glob:**" + glob;
            LOGGER.finest("  Created glob pattern: '" + basePattern + "'");

            matchers.add(FileSystems.getDefault().getPathMatcher(basePattern));

            if (dirPattern) {
                // Match anything under the directory
                String dirPatternFull = basePattern + "/**";
                LOGGER.finest("  Adding directory recursive pattern: '" + dirPatternFull + "'");
                matchers.add(FileSystems.getDefault().getPathMatcher(dirPatternFull));
            }
        }

        LOGGER.fine("Compiled " + matchers.size() + " path matchers from " + patterns.size() + " patterns");
        return matchers;
    }

    /**
     * Determines whether a path should be ignored based on the loaded patterns.
     *
     * @param path The path to check
     * @param baseDir The base directory against which relative paths are resolved
     * @return true if the path should be ignored, false otherwise
     */
    public boolean shouldIgnore(Path path, Path baseDir) {
        if (ignorePatterns.isEmpty()) {
            LOGGER.finest("No ignore patterns to check against");
            return false;
        }

        // Get relative path from base directory
        Path rel = baseDir.relativize(path);
        // Normalize to use forward slashes and add leading slash
        Path normalized = Path.of("/" + rel.toString().replace('\\', '/'));
        String normalizedStr = normalized.toString();

        LOGGER.fine("Checking if path should be ignored: " + path.getFileName() +
                   " (relative: " + rel + ", normalized: " + normalizedStr + ")");

        for (int i = 0; i < ignorePatterns.size(); i++) {
            PathMatcher matcher = ignorePatterns.get(i);
            try {
                boolean matches = matcher.matches(normalized);
                if (matches) {
                    LOGGER.fine("  ✓ Matched pattern #" + i + " - ignoring: " + path.getFileName());
                    return true;
                }
            } catch (Exception e) {
                LOGGER.warning("Error matching pattern #" + i + " against path " + normalizedStr + ": " + e.getMessage());
            }
        }

        LOGGER.finest("  No matches - not ignoring: " + path.getFileName());
        return false;
    }

    /**
     * Filters a list of paths, returning only those that should not be ignored.
     *
     * @param paths List of paths to filter
     * @param baseDir The base directory against which relative paths are resolved
     * @return Filtered list of paths
     */
    public List<Path> filterPaths(List<Path> paths, Path baseDir) {
        List<Path> filtered = new ArrayList<>();
        for (Path path : paths) {
            if (!shouldIgnore(path, baseDir)) {
                filtered.add(path);
            }
        }
        return filtered;
    }

    /**
     * Gets the number of ignore patterns loaded.
     *
     * @return Number of ignore patterns
     */
    public int getPatternCount() {
        return ignorePatterns.size();
    }

    /**
     * Checks if any ignore patterns are loaded.
     *
     * @return true if ignore patterns are loaded, false otherwise
     */
    public boolean hasPatterns() {
        return !ignorePatterns.isEmpty();
    }
}