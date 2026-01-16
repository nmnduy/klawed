package com.filesurf.service;

import io.quarkus.runtime.Startup;
import jakarta.annotation.PostConstruct;
import jakarta.enterprise.context.ApplicationScoped;

import java.io.BufferedReader;
import java.io.IOException;
import java.io.InputStreamReader;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.attribute.BasicFileAttributes;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.concurrent.TimeUnit;
import java.util.logging.Logger;

/**
 * Service for searching files in session directories.
 * Uses fast native tools (fd, rg) when available, falls back to find or Java.
 *
 * Tool priority:
 * 1. fd - Fastest, respects .gitignore by default
 * 2. rg --files - Also fast, respects .gitignore
 * 3. find - Universal fallback, always available on Unix
 * 4. Java Files.walkFileTree - Ultimate fallback, works everywhere
 */
@ApplicationScoped
@Startup
public class FileSearchService {

    private static final Logger LOGGER = Logger.getLogger(FileSearchService.class.getName());

    private static final int SEARCH_TIMEOUT_SECONDS = 10;

    public enum SearchTool {
        FD("fd"),
        RG("rg"),
        FIND("find"),
        JAVA("java");

        private final String name;

        SearchTool(String name) {
            this.name = name;
        }

        public String getName() {
            return name;
        }
    }

    private SearchTool activeTool = SearchTool.JAVA;
    private String fdPath = null;
    private String rgPath = null;
    private String findPath = null;

    @PostConstruct
    void init() {
        detectTools();
    }

    /**
     * Detect available search tools and log warnings if fast tools are missing.
     */
    private void detectTools() {
        // Check for fd (or fdfind on Debian/Ubuntu)
        fdPath = findExecutable("fd");
        if (fdPath == null) {
            // On Debian/Ubuntu, fd is installed as 'fdfind' due to naming conflict
            fdPath = findExecutable("fdfind");
        }
        if (fdPath != null) {
            activeTool = SearchTool.FD;
            LOGGER.info("FileSearchService: Using 'fd' for file search (" + fdPath + ")");
            return;
        }

        // Check for rg (ripgrep)
        rgPath = findExecutable("rg");
        if (rgPath != null) {
            activeTool = SearchTool.RG;
            LOGGER.info("FileSearchService: Using 'rg' (ripgrep) for file search (" + rgPath + ")");
            LOGGER.warning("FileSearchService: 'fd' not found. Install fd-find for better performance: apt install fd-find");
            return;
        }

        // Check for find (should always exist on Unix)
        findPath = findExecutable("find");
        if (findPath != null) {
            activeTool = SearchTool.FIND;
            LOGGER.warning("FileSearchService: Fast search tools (fd, rg) not found. Using 'find' as fallback.");
            LOGGER.warning("FileSearchService: Install fd-find or ripgrep for better search performance:");
            LOGGER.warning("FileSearchService:   Debian/Ubuntu: apt install fd-find ripgrep");
            LOGGER.warning("FileSearchService:   macOS: brew install fd ripgrep");
            return;
        }

        // Ultimate fallback: Java
        activeTool = SearchTool.JAVA;
        LOGGER.warning("FileSearchService: No native search tools found. Using Java fallback (slowest).");
        LOGGER.warning("FileSearchService: Install fd-find or ripgrep for much better search performance.");
    }

    /**
     * Find an executable in PATH.
     */
    private String findExecutable(String name) {
        try {
            ProcessBuilder pb = new ProcessBuilder("which", name);
            pb.redirectErrorStream(true);
            Process process = pb.start();

            if (process.waitFor(5, TimeUnit.SECONDS) && process.exitValue() == 0) {
                try (BufferedReader reader = new BufferedReader(new InputStreamReader(process.getInputStream()))) {
                    String path = reader.readLine();
                    if (path != null && !path.trim().isEmpty()) {
                        return path.trim();
                    }
                }
            }
        } catch (Exception e) {
            LOGGER.fine("Failed to find executable '" + name + "': " + e.getMessage());
        }
        return null;
    }

    /**
     * Get the currently active search tool.
     */
    public SearchTool getActiveTool() {
        return activeTool;
    }

    /**
     * Search for files matching the given query in the specified directory.
     *
     * @param directory The directory to search in
     * @param query Search query (space-separated terms, all must match)
     * @param limit Maximum number of results
     * @param fileFilter Optional file filter for ignore patterns
     * @return Search results with items and hasMore flag
     */
    public SearchResult search(Path directory, String query, int limit, FileFilter fileFilter) {
        if (query == null || query.trim().isEmpty()) {
            return new SearchResult(List.of(), false);
        }

        String[] terms = query.trim().toLowerCase().split("\\s+");

        switch (activeTool) {
            case FD:
                return searchWithFd(directory, terms, limit, fileFilter);
            case RG:
                return searchWithRg(directory, terms, limit, fileFilter);
            case FIND:
                return searchWithFind(directory, terms, limit, fileFilter);
            default:
                return searchWithJava(directory, terms, limit, fileFilter);
        }
    }

    /**
     * Search using fd (fd-find).
     * fd is very fast and respects .gitignore by default.
     */
    private SearchResult searchWithFd(Path directory, String[] terms, int limit, FileFilter fileFilter) {
        try {
            // Build fd command
            // fd --no-ignore-vcs <pattern> <directory>
            // For multi-term search, we use a regex pattern
            List<String> cmd = new ArrayList<>();
            cmd.add(fdPath);
            // No --type flag means search both files and directories
            cmd.add("--no-ignore-vcs"); // Don't use .gitignore since we have our own .filesurfignore
            cmd.add("--max-results");
            cmd.add(String.valueOf(limit + 1)); // +1 to detect if there are more

            // Build pattern: match files containing all terms (case insensitive)
            // fd uses regex, so we need to match files where all terms appear
            // For simplicity, we'll get all files and filter in Java
            cmd.add("."); // Match all
            cmd.add(directory.toString());

            List<String> files = runCommand(cmd, directory);
            return filterAndBuildResults(directory, files, terms, limit, fileFilter);

        } catch (Exception e) {
            LOGGER.warning("fd search failed, falling back to Java: " + e.getMessage());
            return searchWithJava(directory, terms, limit, fileFilter);
        }
    }

    /**
     * Search using rg (ripgrep) with --files flag.
     * Note: rg --files only lists files, not directories.
     * We fall back to find for directory listing and merge results.
     */
    private SearchResult searchWithRg(Path directory, String[] terms, int limit, FileFilter fileFilter) {
        try {
            // rg --files --no-ignore-vcs <directory>
            List<String> cmd = new ArrayList<>();
            cmd.add(rgPath);
            cmd.add("--files");
            cmd.add("--no-ignore-vcs");
            cmd.add(directory.toString());

            List<String> files = runCommand(cmd, directory);
            
            // rg --files only lists files, so we need to also get directories
            // Use find for directories if available
            if (findPath != null) {
                List<String> dirCmd = new ArrayList<>();
                dirCmd.add(findPath);
                dirCmd.add(directory.toString());
                dirCmd.add("-type");
                dirCmd.add("d");
                dirCmd.add("-not");
                dirCmd.add("-path");
                dirCmd.add(directory.toString()); // Exclude the root directory itself
                
                try {
                    List<String> dirs = runCommand(dirCmd, directory);
                    files.addAll(dirs);
                } catch (Exception e) {
                    LOGGER.fine("Failed to get directories with find: " + e.getMessage());
                }
            }
            
            return filterAndBuildResults(directory, files, terms, limit, fileFilter);

        } catch (Exception e) {
            LOGGER.warning("rg search failed, falling back to Java: " + e.getMessage());
            return searchWithJava(directory, terms, limit, fileFilter);
        }
    }

    /**
     * Search using find (universal Unix tool).
     */
    private SearchResult searchWithFind(Path directory, String[] terms, int limit, FileFilter fileFilter) {
        try {
            // find <directory> - no type restriction to include both files and directories
            // Exclude the root directory itself with -mindepth 1
            List<String> cmd = new ArrayList<>();
            cmd.add(findPath);
            cmd.add(directory.toString());
            cmd.add("-mindepth");
            cmd.add("1");

            List<String> files = runCommand(cmd, directory);
            return filterAndBuildResults(directory, files, terms, limit, fileFilter);

        } catch (Exception e) {
            LOGGER.warning("find search failed, falling back to Java: " + e.getMessage());
            return searchWithJava(directory, terms, limit, fileFilter);
        }
    }

    /**
     * Run a command and return output lines.
     */
    private List<String> runCommand(List<String> cmd, Path workDir) throws IOException, InterruptedException {
        ProcessBuilder pb = new ProcessBuilder(cmd);
        pb.directory(workDir.toFile());
        pb.redirectErrorStream(true);

        Process process = pb.start();
        List<String> lines = new ArrayList<>();

        try (BufferedReader reader = new BufferedReader(new InputStreamReader(process.getInputStream()))) {
            String line;
            while ((line = reader.readLine()) != null) {
                lines.add(line);
            }
        }

        boolean completed = process.waitFor(SEARCH_TIMEOUT_SECONDS, TimeUnit.SECONDS);
        if (!completed) {
            process.destroyForcibly();
            throw new IOException("Search command timed out after " + SEARCH_TIMEOUT_SECONDS + " seconds");
        }

        return lines;
    }

    /**
     * Filter file paths and build results.
     */
    private SearchResult filterAndBuildResults(Path directory, List<String> filePaths, String[] terms, int limit, FileFilter fileFilter) {
        List<Map<String, Object>> results = new ArrayList<>();
        boolean hasMore = false;

        for (String filePath : filePaths) {
            if (results.size() >= limit) {
                hasMore = true;
                break;
            }

            Path path = Path.of(filePath);

            // Make path relative if it's absolute
            Path relativePath;
            if (path.isAbsolute()) {
                if (!path.startsWith(directory)) {
                    continue;
                }
                relativePath = directory.relativize(path);
            } else {
                relativePath = path;
            }

            String name = path.getFileName().toString();
            String relativePathStr = relativePath.toString();
            String searchableName = name.toLowerCase();
            String searchablePath = relativePathStr.toLowerCase();

            // Check ignore patterns (need full path for FileFilter)
            Path fullPath = directory.resolve(relativePath);
            if (fileFilter != null && fileFilter.shouldIgnore(fullPath, directory)) {
                continue;
            }

            // Check if all terms match
            boolean matches = true;
            for (String term : terms) {
                if (!searchableName.contains(term) && !searchablePath.contains(term)) {
                    matches = false;
                    break;
                }
            }

            if (matches) {
                try {
                    if (!Files.exists(fullPath)) {
                        continue;
                    }

                    BasicFileAttributes attrs = Files.readAttributes(fullPath, BasicFileAttributes.class);
                    boolean isDirectory = attrs.isDirectory();

                    Map<String, Object> item = new HashMap<>();
                    item.put("name", name);
                    item.put("path", relativePathStr);
                    item.put("type", isDirectory ? "directory" : "file");
                    item.put("size", isDirectory ? 0L : attrs.size());
                    item.put("modified", formatFileTime(attrs.lastModifiedTime()));
                    item.put("icon", isDirectory ? "folder" : getFileIcon(name));

                    // Include parent directory for display
                    Path parent = relativePath.getParent();
                    String parentStr = parent != null ? parent.toString() : "";
                    item.put("directory", parentStr.isEmpty() ? "/" : "/" + parentStr);

                    results.add(item);
                } catch (Exception e) {
                    // Skip files we can't read
                }
            }
        }

        return new SearchResult(results, hasMore);
    }

    /**
     * Search using Java Files.walkFileTree (ultimate fallback).
     */
    private SearchResult searchWithJava(Path directory, String[] terms, int limit, FileFilter fileFilter) {
        List<Map<String, Object>> results = new ArrayList<>();
        boolean[] hasMore = {false};

        try {
            Files.walkFileTree(directory, new java.nio.file.SimpleFileVisitor<Path>() {
                @Override
                public java.nio.file.FileVisitResult preVisitDirectory(Path dir, BasicFileAttributes attrs) {
                    if (results.size() >= limit) {
                        hasMore[0] = true;
                        return java.nio.file.FileVisitResult.TERMINATE;
                    }

                    // Skip ignored directories but still check if the directory itself matches
                    if (!dir.equals(directory)) {
                        if (fileFilter != null && fileFilter.shouldIgnore(dir, directory)) {
                            return java.nio.file.FileVisitResult.SKIP_SUBTREE;
                        }
                        
                        // Check if directory matches search terms
                        String name = dir.getFileName().toString();
                        String relativePath = directory.relativize(dir).toString();
                        String searchableName = name.toLowerCase();
                        String searchablePath = relativePath.toLowerCase();

                        boolean matches = true;
                        for (String term : terms) {
                            if (!searchableName.contains(term) && !searchablePath.contains(term)) {
                                matches = false;
                                break;
                            }
                        }

                        if (matches && results.size() < limit) {
                            Map<String, Object> item = new HashMap<>();
                            item.put("name", name);
                            item.put("path", relativePath);
                            item.put("type", "directory");
                            item.put("size", 0L);
                            item.put("modified", formatFileTime(attrs.lastModifiedTime()));
                            item.put("icon", "folder");

                            Path parent = directory.relativize(dir).getParent();
                            String parentStr = parent != null ? parent.toString() : "";
                            item.put("directory", parentStr.isEmpty() ? "/" : "/" + parentStr);

                            results.add(item);
                        }
                    }
                    return java.nio.file.FileVisitResult.CONTINUE;
                }

                @Override
                public java.nio.file.FileVisitResult visitFile(Path file, BasicFileAttributes attrs) {
                    if (results.size() >= limit) {
                        hasMore[0] = true;
                        return java.nio.file.FileVisitResult.TERMINATE;
                    }

                    String name = file.getFileName().toString();
                    String relativePath = directory.relativize(file).toString();

                    // Check ignore patterns
                    if (fileFilter != null && fileFilter.shouldIgnore(file, directory)) {
                        return java.nio.file.FileVisitResult.CONTINUE;
                    }

                    String searchableName = name.toLowerCase();
                    String searchablePath = relativePath.toLowerCase();

                    // Check if all terms match
                    boolean matches = true;
                    for (String term : terms) {
                        if (!searchableName.contains(term) && !searchablePath.contains(term)) {
                            matches = false;
                            break;
                        }
                    }

                    if (matches) {
                        Map<String, Object> item = new HashMap<>();
                        item.put("name", name);
                        item.put("path", relativePath);
                        item.put("type", "file");
                        item.put("size", attrs.size());
                        item.put("modified", formatFileTime(attrs.lastModifiedTime()));
                        item.put("icon", getFileIcon(name));

                        Path parent = directory.relativize(file.getParent());
                        String parentStr = parent.toString();
                        item.put("directory", parentStr.isEmpty() ? "/" : "/" + parentStr);

                        results.add(item);
                    }

                    return java.nio.file.FileVisitResult.CONTINUE;
                }

                @Override
                public java.nio.file.FileVisitResult visitFileFailed(Path file, IOException exc) {
                    return java.nio.file.FileVisitResult.CONTINUE;
                }
            });
        } catch (IOException e) {
            LOGGER.warning("Java file search failed: " + e.getMessage());
        }

        return new SearchResult(results, hasMore[0]);
    }

    private String formatFileTime(java.nio.file.attribute.FileTime fileTime) {
        return java.time.format.DateTimeFormatter.ofPattern("yyyy-MM-dd HH:mm:ss")
                .withZone(java.time.ZoneId.systemDefault())
                .format(fileTime.toInstant());
    }

    private String getFileIcon(String filename) {
        String lower = filename.toLowerCase();
        if (lower.endsWith(".pdf")) return "file-pdf";
        if (lower.endsWith(".jpg") || lower.endsWith(".jpeg") || lower.endsWith(".png") ||
            lower.endsWith(".gif") || lower.endsWith(".webp") || lower.endsWith(".svg")) return "file-image";
        if (lower.endsWith(".doc") || lower.endsWith(".docx")) return "file-word";
        if (lower.endsWith(".xls") || lower.endsWith(".xlsx") || lower.endsWith(".csv")) return "file-spreadsheet";
        if (lower.endsWith(".ppt") || lower.endsWith(".pptx")) return "file-presentation";
        if (lower.endsWith(".zip") || lower.endsWith(".tar") || lower.endsWith(".gz") ||
            lower.endsWith(".rar") || lower.endsWith(".7z")) return "file-archive";
        if (lower.endsWith(".mp3") || lower.endsWith(".wav") || lower.endsWith(".ogg") ||
            lower.endsWith(".flac")) return "file-audio";
        if (lower.endsWith(".mp4") || lower.endsWith(".mkv") || lower.endsWith(".avi") ||
            lower.endsWith(".mov") || lower.endsWith(".webm")) return "file-video";
        if (lower.endsWith(".html") || lower.endsWith(".htm")) return "file-html";
        if (lower.endsWith(".txt") || lower.endsWith(".md") || lower.endsWith(".json") ||
            lower.endsWith(".xml") || lower.endsWith(".yaml") || lower.endsWith(".yml") ||
            lower.endsWith(".java") || lower.endsWith(".js") || lower.endsWith(".py") ||
            lower.endsWith(".c") || lower.endsWith(".cpp") || lower.endsWith(".h") ||
            lower.endsWith(".css") || lower.endsWith(".scss") || lower.endsWith(".ts") ||
            lower.endsWith(".sh") || lower.endsWith(".sql") || lower.endsWith(".tex")) return "file-text";
        return "file";
    }

    /**
     * Result of a search operation.
     */
    public static class SearchResult {
        private final List<Map<String, Object>> items;
        private final boolean hasMore;

        public SearchResult(List<Map<String, Object>> items, boolean hasMore) {
            this.items = items;
            this.hasMore = hasMore;
        }

        public List<Map<String, Object>> getItems() {
            return items;
        }

        public boolean hasMore() {
            return hasMore;
        }
    }
}
