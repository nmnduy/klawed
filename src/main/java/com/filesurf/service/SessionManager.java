package com.filesurf.service;

import jakarta.enterprise.context.ApplicationScoped;
import org.eclipse.microprofile.config.inject.ConfigProperty;

import java.io.IOException;
import java.nio.file.FileSystems;
import java.nio.file.FileVisitOption;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.StandardCopyOption;
import java.nio.file.StandardOpenOption;
import java.time.Instant;
import java.time.LocalDateTime;
import java.time.format.DateTimeFormatter;
import java.util.List;
import java.util.Objects;
import java.util.Set;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.atomic.AtomicReference;
import java.util.concurrent.locks.ReentrantLock;
import java.util.logging.Logger;
import java.util.stream.Collectors;
import java.util.stream.Stream;

/**
 * Manages user workspace directories for file management and AI processing.
 * Each user has a persistent workspace that serves as their session directory.
 * A tmp/ folder is created for temporary files that get cleaned up when session ends.
 */
@ApplicationScoped
public class SessionManager {

    private static final Logger LOGGER = Logger.getLogger(SessionManager.class.getName());

    // Inject persistent root path from configuration
    @ConfigProperty(name = "filesurf.persist.root", defaultValue = "./data/persistent")
    String persistRootConfig;

    // Map session ID to session directory path
    private final ConcurrentHashMap<String, Path> sessionDirectories = new ConcurrentHashMap<>();

    // Per-user lock for session operations
    private final ConcurrentHashMap<String, ReentrantLock> userLocks = new ConcurrentHashMap<>();

    // Map user ID to set of session IDs (allowing multiple sessions per user)
    private final ConcurrentHashMap<String, Set<String>> userToSessions = new ConcurrentHashMap<>();

    // Cached persistent root path
    private Path persistRoot;

    // Date formatter for backup folders
    private static final DateTimeFormatter BACKUP_FORMATTER = DateTimeFormatter.ofPattern("yyyyMMddHHmmss");

    /**
     * Initialize a user workspace for a WebSocket session.
     * The workspace is the user's persistent directory where all files are stored.
     * Creates tmp/ folder for temporary files and ensures SKILLS folder exists.
     */
    public Path initializeSession(String sessionId, String userId) throws IOException {
        Objects.requireNonNull(sessionId, "sessionId");
        Objects.requireNonNull(userId, "userId");

        String sanitizedUser = sanitizeUserId(userId);
        ReentrantLock lock = userLocks.computeIfAbsent(sanitizedUser, u -> new ReentrantLock());
        lock.lock();
        try {
            // The workspace is the user's persistent directory
            Path workspace = userRoot(sanitizedUser);
            Files.createDirectories(workspace);
            
            LOGGER.info("[SESSION:" + sessionId + "] Initializing workspace: " + workspace);

            // Create tmp folder for temporary files
            Path tmpDir = workspace.resolve("tmp");
            Files.createDirectories(tmpDir);
            LOGGER.info("[SESSION:" + sessionId + "] Created tmp directory: " + tmpDir);

            // Ensure SKILLS folder exists in workspace
            Path skillsDir = workspace.resolve("SKILLS");
            if (!Files.exists(skillsDir) || isEmptyDirectory(skillsDir)) {
                LOGGER.info("[SESSION:" + sessionId + "] SKILLS folder missing or empty, copying default SKILLS");
                copySkillsToWorkspace(workspace);
            }

            // Copy KLAWED.md to workspace so agent gets proper instructions
            copyKlawedMdToWorkspace(workspace);

            // Copy .fileexplorerignore so explorer can hide unwanted files
            copyFileExplorerIgnoreToWorkspace(workspace);

            // Store in map (workspace is the session directory)
            sessionDirectories.put(sessionId, workspace);
            userToSessions.computeIfAbsent(sanitizedUser, k -> ConcurrentHashMap.newKeySet()).add(sessionId);

            LOGGER.info("[SESSION:" + sessionId + "] Workspace initialized successfully");
            return workspace;
        } finally {
            lock.unlock();
        }
    }
    
    /**
     * Get the session directory for a given session ID
     * Creates it if it doesn't exist. Requires userId for hydration.
     */
    public Path getSessionDirectory(String sessionId, String userId) throws IOException {
        Path sessionDir = sessionDirectories.get(sessionId);
        if (sessionDir == null || !Files.exists(sessionDir)) {
            sessionDir = initializeSession(sessionId, userId);
        }
        return sessionDir;
    }
    
    /**
     * Get the uploads directory for a given session
     * Creates the directory only when needed (on first file upload)
     */
    public Path getUploadsDirectory(String sessionId, String userId) throws IOException {
        Path sessionDir = getSessionDirectory(sessionId, userId);
        Path uploadsDir = sessionDir.resolve("uploads");

        // Only create the directory if it doesn't exist
        if (!Files.exists(uploadsDir)) {
            Files.createDirectories(uploadsDir);
            LOGGER.info("Created uploads directory on demand: " + uploadsDir);
        }

        return uploadsDir;
    }
    
    /**
     * Copy SKILLS folder to the session directory so the AI can access and use the scripts
     */
    private void copySkillsToWorkspace(Path workspace) {
        try {
            // Try to copy from filesystem first (development mode)
            // First try: src/main/resources/SKILLS (development mode)
            Path sourceSkillsDir = Path.of("src/main/resources/SKILLS");
            if (Files.exists(sourceSkillsDir)) {
                LOGGER.info("Copying SKILLS from filesystem: " + sourceSkillsDir.toAbsolutePath());
                Path targetSkillsDir = workspace.resolve("SKILLS");
                copyDirectory(sourceSkillsDir, targetSkillsDir);
                makeScriptsExecutable(targetSkillsDir);
                LOGGER.info("Copied SKILLS directory to workspace: " + targetSkillsDir);
                return;
            }
            
            // Second try: just SKILLS (alternative location)
            sourceSkillsDir = Path.of("SKILLS");
            if (Files.exists(sourceSkillsDir)) {
                LOGGER.info("Copying SKILLS from filesystem: " + sourceSkillsDir.toAbsolutePath());
                Path targetSkillsDir = workspace.resolve("SKILLS");
                copyDirectory(sourceSkillsDir, targetSkillsDir);
                makeScriptsExecutable(targetSkillsDir);
                LOGGER.info("Copied SKILLS directory to workspace: " + targetSkillsDir);
                return;
            }

            // Try to copy from classpath resources (packaged mode)
            LOGGER.info("Attempting to copy SKILLS from classpath resources");
            Path targetSkillsDir = workspace.resolve("SKILLS");
            if (copySkillsFromClasspath(targetSkillsDir)) {
                makeScriptsExecutable(targetSkillsDir);
                LOGGER.info("Copied SKILLS from classpath to workspace: " + targetSkillsDir);
                return;
            }

            LOGGER.warning("SKILLS directory not found in filesystem or classpath");

        } catch (IOException e) {
            LOGGER.warning("Failed to copy SKILLS directory to workspace: " + e.getMessage());
        }
    }
    
    private void copySkillsToSession(Path sessionDir) {
        try {
            // Try to copy from filesystem first (development mode)
            // First try: src/main/resources/SKILLS (development mode)
            Path sourceSkillsDir = Path.of("src/main/resources/SKILLS");
            if (Files.exists(sourceSkillsDir)) {
                LOGGER.info("Copying SKILLS from filesystem: " + sourceSkillsDir.toAbsolutePath());
                Path targetSkillsDir = sessionDir.resolve("SKILLS");
                copyDirectory(sourceSkillsDir, targetSkillsDir);
                makeScriptsExecutable(targetSkillsDir);
                LOGGER.info("Copied SKILLS directory to session: " + targetSkillsDir);
                return;
            }
            
            // Second try: just SKILLS (alternative location)
            sourceSkillsDir = Path.of("SKILLS");
            if (Files.exists(sourceSkillsDir)) {
                LOGGER.info("Copying SKILLS from filesystem: " + sourceSkillsDir.toAbsolutePath());
                Path targetSkillsDir = sessionDir.resolve("SKILLS");
                copyDirectory(sourceSkillsDir, targetSkillsDir);
                makeScriptsExecutable(targetSkillsDir);
                LOGGER.info("Copied SKILLS directory to session: " + targetSkillsDir);
                return;
            }

            // Try to copy from classpath resources (packaged mode)
            LOGGER.info("Attempting to copy SKILLS from classpath resources");
            Path targetSkillsDir = sessionDir.resolve("SKILLS");
            if (copySkillsFromClasspath(targetSkillsDir)) {
                makeScriptsExecutable(targetSkillsDir);
                LOGGER.info("Copied SKILLS from classpath to session: " + targetSkillsDir);
                return;
            }

            LOGGER.warning("SKILLS directory not found in filesystem or classpath");

        } catch (IOException e) {
            LOGGER.warning("Failed to copy SKILLS directory to session: " + e.getMessage());
        }
    }
    
    /**
     * Copy SKILLS from classpath resources
     */
    private boolean copySkillsFromClasspath(Path targetDir) throws IOException {
        try {
            // Get the SKILLS directory from classpath
            var classLoader = Thread.currentThread().getContextClassLoader();
            var skillsUrl = classLoader.getResource("SKILLS");
            
            if (skillsUrl == null) {
                LOGGER.warning("SKILLS not found in classpath");
                return false;
            }
            
            LOGGER.info("Found SKILLS in classpath: " + skillsUrl);
            
            // For JAR files, we need to extract resources differently
            if (skillsUrl.getProtocol().equals("jar")) {
                // Extract from JAR
                copySkillsFromJar(targetDir);
            } else {
                // Copy from regular filesystem
                Path sourcePath = Path.of(skillsUrl.toURI());
                copyDirectory(sourcePath, targetDir);
            }
            
            return true;
            
        } catch (Exception e) {
            LOGGER.warning("Failed to copy from classpath: " + e.getMessage());
            return false;
        }
    }
    
    /**
     * Extract SKILLS from JAR file
     */
    private void copySkillsFromJar(Path targetDir) throws IOException {
        Files.createDirectories(targetDir);
        
        // List of known SKILLS resources to extract
        String[] skillsResources = {
            // OCR scripts
            "SKILLS/ocr/README.md",
            "SKILLS/ocr/ocr_tesseract.sh",
            "SKILLS/ocr/ocr_pdf_text.sh",
            "SKILLS/ocr/ocr_combined.sh",
            "SKILLS/ocr/ocr_to_json.sh",
            "SKILLS/ocr/preprocess_image.sh",
            // LaTeX recipe notes (packaged mode support)
            "SKILLS/latex-recipes/minimal-invoice-skeleton.txt",
            "SKILLS/latex-recipes/branded-header-footer.txt",
            "SKILLS/latex-recipes/typography.txt",
            "SKILLS/latex-recipes/items-and-totals.txt",
            "SKILLS/latex-recipes/layout-variants.txt",
            "SKILLS/latex-recipes/localization.txt",
            "SKILLS/latex-recipes/compile-tips.txt",
            // Invoice recipe index
            "SKILLS/invoice-recipes/README.txt"
        };
        
        var classLoader = Thread.currentThread().getContextClassLoader();
        
        for (String resourcePath : skillsResources) {
            try (var inputStream = classLoader.getResourceAsStream(resourcePath)) {
                if (inputStream != null) {
                    // Create parent directories
                    Path targetPath = targetDir.resolve(resourcePath.replace("SKILLS/", ""));
                    Path normalizedTarget = targetPath.normalize();
                    if (!normalizedTarget.startsWith(targetDir)) {
                        throw new IOException("Invalid target path for resource " + resourcePath);
                    }
                    Files.createDirectories(normalizedTarget.getParent());
                    
                    // Copy the file
                    Files.copy(inputStream, normalizedTarget, StandardCopyOption.REPLACE_EXISTING);
                    LOGGER.fine("Copied from JAR: " + resourcePath);
                } else {
                    LOGGER.warning("Resource not found in JAR: " + resourcePath);
                }
            }
        }
    }
    
    /**
     * Recursively copy a directory
     */
    private void copyDirectory(Path source, Path target) throws IOException {
        LOGGER.fine("[COPY-DIRECTORY] Starting copy from " + source + " to " + target);
        if (!Files.exists(source)) {
            LOGGER.fine("[COPY-DIRECTORY] Source does not exist, skipping: " + source);
            return;
        }
        
        // Create target directory if it doesn't exist
        Files.createDirectories(target);
        
        int fileCount = 0;
        int dirCount = 0;
        
        // Walk through source directory
        try (Stream<Path> stream = Files.walk(source, FileVisitOption.FOLLOW_LINKS)) {
            stream.forEach(sourcePath -> {
                try {
                    if (Files.isSymbolicLink(sourcePath)) {
                        LOGGER.warning("Skipping symbolic link during copy: " + sourcePath);
                        return;
                    }
                    Path targetPath = target.resolve(source.relativize(sourcePath));
                    if (Files.isDirectory(sourcePath)) {
                        Files.createDirectories(targetPath);
                    } else {
                        Files.copy(sourcePath, targetPath, StandardCopyOption.REPLACE_EXISTING);
                    }
                } catch (IOException e) {
                    LOGGER.warning("Failed to copy: " + sourcePath + " - " + e.getMessage());
                }
            });
        }
        
        LOGGER.fine("[COPY-DIRECTORY] Copy completed from " + source + " to " + target);
    }
    
    /**
     * Copy .fileexplorerignore to session directory for file explorer filtering
     */
    private void copyFileExplorerIgnoreToWorkspace(Path workspace) {
        copyResourceToWorkspace(workspace, ".fileexplorerignore");
    }
    
    private void copyFileExplorerIgnoreToSession(Path sessionDir) {
        copyResourceToSession(sessionDir, ".fileexplorerignore");
    }

    /**
     * Copy KLAWED.md to workspace so the AI agent gets proper instructions
     * Appends dynamic content listing files in the workspace
     */
    private void copyKlawedMdToWorkspace(Path workspace) {
        copyResourceToWorkspace(workspace, "KLAWED.md");
    }
    
    /**
     * Copy KLAWED.md to session directory so the AI agent gets proper instructions
     * Appends dynamic content listing files in DATA and SKILLS folders
     */
    private void copyKlawedMdToSession(Path sessionDir) {
        copyResourceToSession(sessionDir, "KLAWED.md");
    }

    /**
     * Generic helper to copy a resource to the workspace (classpath first, filesystem fallback)
     */
    private void copyResourceToWorkspace(Path workspace, String resourceName) {
        try {
            Path targetPath = workspace.resolve(resourceName).normalize();
            if (!targetPath.startsWith(workspace)) {
                throw new IOException("Invalid target path for " + resourceName);
            }
            
            // Special handling for KLAWED.md - read content and append dynamic listing
            if (resourceName.equals("KLAWED.md")) {
                // Read the original KLAWED.md content
                String originalContent = "";
                
                // Try to read from classpath resources first (packaged mode)
                var classLoader = Thread.currentThread().getContextClassLoader();
                try (var inputStream = classLoader.getResourceAsStream(resourceName)) {
                    if (inputStream != null) {
                        originalContent = new String(inputStream.readAllBytes());
                        LOGGER.info("Read KLAWED.md from classpath resources");
                    }
                }
                
                // Fallback: try to read from filesystem (development mode)
                if (originalContent.isEmpty()) {
                    Path sourceKlawedMd = Path.of("src/main/resources/KLAWED.md");
                    if (Files.exists(sourceKlawedMd)) {
                        originalContent = Files.readString(sourceKlawedMd);
                        LOGGER.info("Read KLAWED.md from filesystem: " + sourceKlawedMd);
                    }
                }
                
                if (originalContent.isEmpty()) {
                    LOGGER.warning("KLAWED.md not found in classpath resources or filesystem");
                    return;
                }
                
                // Generate dynamic content listing files in workspace
                String dynamicContent = generateWorkspaceListing(workspace);
                
                // Combine original content with dynamic content
                String combinedContent = originalContent + "\n\n" + dynamicContent;
                
                // Write the combined content to workspace
                Files.writeString(targetPath, combinedContent, StandardOpenOption.CREATE, StandardOpenOption.TRUNCATE_EXISTING);
                LOGGER.info("Copied KLAWED.md with dynamic content to workspace: " + targetPath);
                return;
            }
            
            // For other resources, just copy them directly
            // Try to copy from classpath resources first (packaged mode)
            var classLoader = Thread.currentThread().getContextClassLoader();
            try (var inputStream = classLoader.getResourceAsStream(resourceName)) {
                if (inputStream != null) {
                    Files.copy(inputStream, targetPath, StandardCopyOption.REPLACE_EXISTING);
                    LOGGER.info("Copied " + resourceName + " from resources to workspace: " + targetPath);
                    return;
                }
            }

            // Fallback: try to copy from filesystem (development mode)
            Path source = Path.of("src/main/resources/" + resourceName);
            if (Files.exists(source)) {
                Files.copy(source, targetPath, StandardCopyOption.REPLACE_EXISTING);
                LOGGER.info("Copied " + resourceName + " from filesystem to workspace: " + targetPath);
                return;
            }

            LOGGER.warning(resourceName + " not found in classpath resources or filesystem");

        } catch (IOException e) {
            LOGGER.warning("Failed to copy " + resourceName + " to workspace: " + e.getMessage());
        }
    }
    
    /**
     * Generic helper to copy a resource to the session dir (classpath first, filesystem fallback)
     */
    private void copyResourceToSession(Path sessionDir, String resourceName) {
        try {
            Path targetPath = sessionDir.resolve(resourceName).normalize();
            if (!targetPath.startsWith(sessionDir)) {
                throw new IOException("Invalid target path for " + resourceName);
            }
            
            // Special handling for KLAWED.md - read content and append dynamic listing
            if (resourceName.equals("KLAWED.md")) {
                // Read the original KLAWED.md content
                String originalContent = "";
                
                // Try to read from classpath resources first (packaged mode)
                var classLoader = Thread.currentThread().getContextClassLoader();
                try (var inputStream = classLoader.getResourceAsStream(resourceName)) {
                    if (inputStream != null) {
                        originalContent = new String(inputStream.readAllBytes());
                        LOGGER.info("Read KLAWED.md from classpath resources");
                    }
                }
                
                // Fallback: try to read from filesystem (development mode)
                if (originalContent.isEmpty()) {
                    Path sourceKlawedMd = Path.of("src/main/resources/KLAWED.md");
                    if (Files.exists(sourceKlawedMd)) {
                        originalContent = Files.readString(sourceKlawedMd);
                        LOGGER.info("Read KLAWED.md from filesystem: " + sourceKlawedMd);
                    }
                }
                
                if (originalContent.isEmpty()) {
                    LOGGER.warning("KLAWED.md not found in classpath resources or filesystem");
                    return;
                }
                
                // Generate dynamic content listing files in persistent folders
                String dynamicContent = generatePersistentFoldersListing(sessionDir);
                
                // Combine original content with dynamic content
                String combinedContent = originalContent + "\n\n" + dynamicContent;
                
                // Write the combined content to session directory
                Files.writeString(targetPath, combinedContent, StandardOpenOption.CREATE, StandardOpenOption.TRUNCATE_EXISTING);
                LOGGER.info("Copied KLAWED.md with dynamic content to session: " + targetPath);
                return;
            }
            
            // For other resources, just copy them directly
            // Try to copy from classpath resources first (packaged mode)
            var classLoader = Thread.currentThread().getContextClassLoader();
            try (var inputStream = classLoader.getResourceAsStream(resourceName)) {
                if (inputStream != null) {
                    Files.copy(inputStream, targetPath, StandardCopyOption.REPLACE_EXISTING);
                    LOGGER.info("Copied " + resourceName + " from resources to session: " + targetPath);
                    return;
                }
            }

            // Fallback: try to copy from filesystem (development mode)
            Path source = Path.of("src/main/resources/" + resourceName);
            if (Files.exists(source)) {
                Files.copy(source, targetPath, StandardCopyOption.REPLACE_EXISTING);
                LOGGER.info("Copied " + resourceName + " from filesystem to session: " + targetPath);
                return;
            }

            LOGGER.warning(resourceName + " not found in classpath resources or filesystem");

        } catch (IOException e) {
            LOGGER.warning("Failed to copy " + resourceName + " to session directory: " + e.getMessage());
        }
    }
    
    /**
     * Generate a listing of files in the workspace (excluding tmp folder)
     * Limits to 100 items and adds [...] if truncated
     */
    private String generateWorkspaceListing(Path workspace) {
        StringBuilder sb = new StringBuilder();
        sb.append("## Workspace Files and Directories\n\n");
        sb.append("The following are files and directories available in your workspace:\n\n");
        
        try (Stream<Path> paths = Files.list(workspace)) {
            List<Path> items = paths
                .filter(p -> !p.getFileName().toString().equals("tmp")) // Exclude tmp folder from listing
                .sorted((p1, p2) -> {
                    boolean d1 = Files.isDirectory(p1);
                    boolean d2 = Files.isDirectory(p2);
                    if (d1 && !d2) return -1;
                    if (!d1 && d2) return 1;
                    return p1.getFileName().toString().compareToIgnoreCase(p2.getFileName().toString());
                })
                .collect(Collectors.toList());
            
            if (items.isEmpty()) {
                sb.append("- No files or directories found\n");
            } else {
                int count = 0;
                int maxItems = 100;
                boolean truncated = false;
                
                for (Path item : items) {
                    if (count >= maxItems) {
                        truncated = true;
                        break;
                    }
                    
                    String name = item.getFileName().toString();
                    if (Files.isDirectory(item)) {
                        sb.append("- `").append(name).append("/` - Directory\n");
                    } else {
                        sb.append("- `").append(name).append("` - File\n");
                    }
                    count++;
                }
                
                if (truncated) {
                    sb.append("- [...] (more items not shown)\n");
                }
            }
        } catch (IOException e) {
            sb.append("- Unable to list workspace contents: ").append(e.getMessage()).append("\n");
        }
        
        sb.append("\n**Note:** Files in the `tmp/` folder are temporary and will be cleaned up when the session ends.\n");
        sb.append("Use the `tmp/` folder for temporary files that don't need to persist between sessions.\n");
        
        return sb.toString();
    }
    
    /**
     * Generate a listing of files in DATA and SKILLS folders
     * Limits to 100 items per folder and adds [...] if truncated
     */
    private String generatePersistentFoldersListing(Path sessionDir) {
        StringBuilder sb = new StringBuilder();
        sb.append("## Session Files and Directories\n\n");
        sb.append("The following are files and directories available in your current session's persistent folders:\n\n");
        
        String[] folders = {"DATA", "SKILLS"};
        
        for (String folderName : folders) {
            Path folderPath = sessionDir.resolve(folderName);
            sb.append("### ").append(folderName).append(" Folder\n");
            
            if (!Files.exists(folderPath) || !Files.isDirectory(folderPath)) {
                sb.append("- No files or directories found\n\n");
                continue;
            }
            
            try (Stream<Path> paths = Files.list(folderPath)) {
                List<Path> items = paths
                    .sorted((p1, p2) -> {
                        boolean d1 = Files.isDirectory(p1);
                        boolean d2 = Files.isDirectory(p2);
                        if (d1 && !d2) return -1;
                        if (!d1 && d2) return 1;
                        return p1.getFileName().toString().compareToIgnoreCase(p2.getFileName().toString());
                    })
                    .collect(Collectors.toList());
                
                if (items.isEmpty()) {
                    sb.append("- No files or directories found\n");
                } else {
                    int count = 0;
                    int maxItems = 100;
                    boolean truncated = false;
                    
                    for (Path item : items) {
                        if (count >= maxItems) {
                            truncated = true;
                            break;
                        }
                        
                        String name = item.getFileName().toString();
                        if (Files.isDirectory(item)) {
                            sb.append("- `").append(name).append("/` - Directory\n");
                        } else {
                            sb.append("- `").append(name).append("` - File\n");
                        }
                        count++;
                    }
                    
                    if (truncated) {
                        sb.append("- [...] (list truncated at ").append(maxItems).append(" items)\n");
                    }
                }
                
            } catch (IOException e) {
                sb.append("- Error reading directory: ").append(e.getMessage()).append("\n");
            }
            
            sb.append("\n");
        }
        
        sb.append("_Note: These are the files available in your current session. " +
                 "You can upload additional files to the DATA folder._\n");
        
        return sb.toString();
    }
    
    /**
     * Make all shell scripts executable
     */
    private void makeScriptsExecutable(Path skillsDir) throws IOException {
        if (!Files.exists(skillsDir)) {
            return;
        }
        
        try (Stream<Path> stream = Files.walk(skillsDir)) {
            stream.filter(path -> path.toString().endsWith(".sh"))
                  .forEach(path -> {
                      try {
                          // Make executable (read + execute for owner, group, others)
                          java.util.Set<java.nio.file.attribute.PosixFilePermission> perms = 
                              new java.util.HashSet<>();
                          perms.add(java.nio.file.attribute.PosixFilePermission.OWNER_READ);
                          perms.add(java.nio.file.attribute.PosixFilePermission.OWNER_WRITE);
                          perms.add(java.nio.file.attribute.PosixFilePermission.OWNER_EXECUTE);
                          perms.add(java.nio.file.attribute.PosixFilePermission.GROUP_READ);
                          perms.add(java.nio.file.attribute.PosixFilePermission.GROUP_EXECUTE);
                          perms.add(java.nio.file.attribute.PosixFilePermission.OTHERS_READ);
                          perms.add(java.nio.file.attribute.PosixFilePermission.OTHERS_EXECUTE);
                          
                          Files.setPosixFilePermissions(path, perms);
                          LOGGER.fine("Made executable: " + path);
                      } catch (Exception e) {
                          LOGGER.warning("Failed to make executable: " + path + " - " + e.getMessage());
                      }
                  });
        }
    }
    
    /**
     * Clean up a session's tmp folder immediately (synchronous).
     * The workspace itself persists - only temporary files are removed.
     */
    public void cleanupSession(String sessionId) {
        Path workspace = sessionDirectories.remove(sessionId);
        if (workspace != null) {
            try {
                // Clean up tmp folder only (workspace itself persists)
                Path tmpDir = workspace.resolve("tmp");
                if (Files.exists(tmpDir)) {
                    deleteDirectoryIfExists(tmpDir);
                    LOGGER.info("[SESSION:" + sessionId + "] Cleaned up tmp folder: " + tmpDir);
                }
            } catch (IOException e) {
                LOGGER.warning("Failed to clean up tmp folder for session " + sessionId + ": " + e.getMessage());
            }
        }
        // remove any user mapping pointing to this session
        userToSessions.forEach((user, sessions) -> sessions.remove(sessionId));
        LOGGER.info("[SESSION:" + sessionId + "] Session cleanup completed (tmp folder cleaned)");
    }

    /**
     * Remove in-memory tracking for a session without deleting the filesystem directory.
     */
    public void releaseSessionTracking(String sessionId) {
        sessionDirectories.remove(sessionId);
        userToSessions.forEach((user, sessions) -> sessions.remove(sessionId));
        LOGGER.info("[SESSION:" + sessionId + "] Released session tracking (directory not deleted)");
    }
    
    /**
     * Get the workspace path for a session if it's currently tracked.
     * Returns null if the session is not currently tracked.
     * This is useful for cleanup operations that don't have the userId available.
     */
    public Path getWorkspaceForSession(String sessionId) {
        return sessionDirectories.get(sessionId);
    }

    /**
     * Resolve the session directory path if it is tracked in memory.
     * Returns null if the session is not currently tracked.
     * @deprecated Use getWorkspaceForSession instead
     */
    @Deprecated
    public Path resolveSessionPath(String sessionId) {
        return sessionDirectories.get(sessionId);
    }

    /**
     * Delete a specific directory on disk (no in-memory tracking changes).
     */
    public void deleteDirectory(Path dir) throws IOException {
        if (dir == null) {
            LOGGER.warning("deleteDirectory called with null path");
            return;
        }
        if (!Files.exists(dir)) {
            LOGGER.info("Directory already absent: " + dir);
            return;
        }
        deleteDirectoryRecursive(dir);
        LOGGER.info("Deleted directory: " + dir);
    }

    // ---------- Persistent storage helpers ----------

    private Path resolvePersistRoot() {
        if (persistRoot != null && Files.exists(persistRoot)) {
            return persistRoot;
        }
        // Use the injected config property instead of System.getProperty
        persistRoot = Path.of(persistRootConfig).toAbsolutePath().normalize();
        LOGGER.info("Resolved persistent root: " + persistRoot);
        return persistRoot;
    }

    private Path userRoot(String userId) throws IOException {
        Path root = resolvePersistRoot().resolve(sanitizeUserId(userId));
        Files.createDirectories(root);
        return root;
    }

    private String sanitizeUserId(String userId) {
        // Remove path separators and restrict to safe chars
        return userId.replaceAll("[^a-zA-Z0-9._-]", "_");
    }

    public void persistSession(String sessionId, String userId) throws IOException {
        Objects.requireNonNull(sessionId, "sessionId");
        if (userId == null || userId.isBlank()) {
            LOGGER.severe("[SESSION:" + sessionId + "] persistSession called without userId; skipping");
            return;
        }
        Objects.requireNonNull(userId, "userId");
        String sanitizedUser = sanitizeUserId(userId);
        LOGGER.info("[SESSION:" + sessionId + "] Persist session for user: " + sanitizedUser);
        
        ReentrantLock lock = userLocks.computeIfAbsent(sanitizedUser, u -> new ReentrantLock());
        lock.lock();
        try {
            Path workspace = getSessionDirectory(sessionId, userId);
            LOGGER.info("[SESSION:" + sessionId + "] Workspace: " + workspace);

            // With the new workspace model, files are already persistent
            // We just need to ensure the workspace exists and clean tmp if needed
            Files.createDirectories(workspace);
            
            // Clean tmp folder during persistence (optional)
            Path tmpDir = workspace.resolve("tmp");
            if (Files.exists(tmpDir)) {
                deleteDirectoryIfExists(tmpDir);
                Files.createDirectories(tmpDir);
                LOGGER.info("[SESSION:" + sessionId + "] Cleaned tmp folder during persistence");
            }
            
            LOGGER.info("[SESSION:" + sessionId + "] Persistence completed (workspace is already persistent)");
        } finally {
            lock.unlock();
        }
    }

    private void copyIfExists(Path source, Path target, boolean deleteFirst) throws IOException {
        LOGGER.fine("[COPY] Checking source: " + source + " exists: " + Files.exists(source));
        if (!Files.exists(source)) {
            LOGGER.fine("[COPY] Source does not exist, skipping: " + source);
            return;
        }
        if (deleteFirst) {
            LOGGER.fine("[COPY] Deleting target before copy: " + target);
            deleteDirectoryIfExists(target);
        }
        LOGGER.info("[COPY] Copying directory from " + source + " to " + target);
        copyDirectory(source, target);
        LOGGER.fine("[COPY] Copy completed: " + source + " -> " + target);
    }

    private void copyWithDelete(Path source, Path target) throws IOException {
        LOGGER.info("[COPY-WITH-DELETE] Source: " + source + " exists: " + Files.exists(source));
        LOGGER.info("[COPY-WITH-DELETE] Target: " + target);
        
        if (Files.exists(source)) {
            try {
                long fileCount = Files.list(source).count();
                LOGGER.info("[COPY-WITH-DELETE] Source contains " + fileCount + " files/directories");
            } catch (IOException e) {
                LOGGER.warning("[COPY-WITH-DELETE] Failed to count files in source: " + e.getMessage());
            }
        }
        
        deleteDirectoryIfExists(target);
        LOGGER.info("[COPY-WITH-DELETE] Copying from " + source + " to " + target);
        copyDirectory(source, target);
        LOGGER.info("[COPY-WITH-DELETE] Copy completed");
    }

    private void safeCopyIfExists(Path source, Path target) {
        try {
            copyIfExists(source, target, true);
        } catch (IOException e) {
            LOGGER.warning("Backup copy failed from " + source + " to " + target + ": " + e.getMessage());
        }
    }

    private void logDirectoryStats(String label, Path dir) {
        try {
            boolean exists = Files.exists(dir);
            long count = exists ? Files.list(dir).count() : 0;
            long size = exists ? Files.walk(dir).filter(Files::isRegularFile).mapToLong(p -> {
                try {
                    return Files.size(p);
                } catch (IOException e) {
                    return 0L;
                }
            }).sum() : 0;
            LOGGER.info(label + ": path=" + dir + ", exists=" + exists + ", files=" + count + ", bytes=" + size);
        } catch (IOException e) {
            LOGGER.warning("Failed to log stats for " + label + " at " + dir + ": " + e.getMessage());
        }
    }

    private void deleteDirectoryIfExists(Path path) throws IOException {
        if (path != null && Files.exists(path)) {
            deleteDirectoryRecursive(path);
        }
    }

    // ---------- End persistent storage helpers ----------
    
    /**
     * Recursively delete a directory
     */
    private void deleteDirectoryRecursive(Path directory) throws IOException {
        if (directory == null || !Files.exists(directory)) {
            return;
        }

        try (Stream<Path> stream = Files.walk(directory, FileVisitOption.FOLLOW_LINKS)) {
            stream.sorted(java.util.Comparator.reverseOrder())
                  .forEach(path -> {
                      try {
                          if (Files.isSymbolicLink(path)) {
                              Files.delete(path);
                              return;
                          }
                          Files.delete(path);
                      } catch (IOException e) {
                          LOGGER.warning("Failed to delete: " + path + " - " + e.getMessage());
                      }
                  });
        }
    }

    private void ensureUnderSession(Path sessionDir, Path... paths) throws IOException {
        for (Path p : paths) {
            if (!p.normalize().startsWith(sessionDir.normalize())) {
                throw new IOException("Path escapes session directory: " + p);
            }
        }
    }
    
    /**
     * Check if a directory is empty
     */
    private boolean isEmptyDirectory(Path dir) throws IOException {
        if (!Files.exists(dir) || !Files.isDirectory(dir)) {
            return true;
        }
        try (var stream = Files.list(dir)) {
            return !stream.findAny().isPresent();
        }
    }
    
    /**
     * Check if a session exists
     */
    public boolean sessionExists(String sessionId) {
        Path sessionDir = sessionDirectories.get(sessionId);
        return sessionDir != null && Files.exists(sessionDir);
    }
    
    /**
     * Get all session IDs for a user
     * @param userId The user ID
     * @return Set of session IDs for the user, or empty set if none
     */
    public Set<String> getSessionsForUser(String userId) {
        if (userId == null || userId.isBlank()) {
            return Set.of();
        }
        String sanitizedUser = sanitizeUserId(userId);
        Set<String> sessions = userToSessions.get(sanitizedUser);
        return sessions != null ? Set.copyOf(sessions) : Set.of();
    }
}
