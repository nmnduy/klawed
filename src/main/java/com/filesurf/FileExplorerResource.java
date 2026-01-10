package com.filesurf;

import com.filesurf.service.FileFilter;
import com.filesurf.service.SessionManager;
import jakarta.inject.Inject;
import jakarta.ws.rs.*;
import jakarta.ws.rs.core.MediaType;
import jakarta.ws.rs.core.Response;

import java.io.IOException;
import java.nio.file.FileSystems;
import java.nio.file.Files;
import java.nio.file.Paths;
import java.nio.file.attribute.BasicFileAttributes;
import java.nio.file.attribute.FileTime;
import java.time.Instant;
import java.time.ZoneId;
import java.time.format.DateTimeFormatter;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.logging.Logger;

/**
 * File Explorer Resource for browsing session directories and files
 */
@Path("/file-chat/explorer")
public class FileExplorerResource {

    private static final Logger LOGGER = Logger.getLogger(FileExplorerResource.class.getName());
    
    @Inject
    SessionManager sessionManager;
    
    @Inject
    com.filesurf.service.LatexCompilerService latexCompilerService;
    
    @Inject
    com.filesurf.service.MetricsService metricsService;
    
    // Maximum size for text file preview (in bytes)
    private static final long MAX_PREVIEW_SIZE = 100 * 1024; // 100KB
    
    // Date formatter for file timestamps
    private static final DateTimeFormatter DATE_FORMATTER = 
        DateTimeFormatter.ofPattern("yyyy-MM-dd HH:mm:ss").withZone(ZoneId.systemDefault());
    
    // Whitelist of file extensions to preview as text
    private static final java.util.Set<String> PREVIEWABLE_EXTENSIONS = java.util.Set.of(
        "txt", "md", "markdown", "json", "xml", "yaml", "yml", "properties", "ini", "cfg", "conf",
        "java", "js", "ts", "py", "cpp", "c", "h", "hpp", "cs", "go", "rs", "php", "rb", "swift", "kt", "scala",
        "html", "htm", "css", "scss", "less", "sass", "jsx", "tsx", "vue", "svelte",
        "sql", "sh", "bash", "zsh", "ps1", "bat", "cmd", "dockerfile",
        "csv", "tsv", "log", "out", "err",
        "gradle", "pom", "toml", "tex", "gitignore", "env", "editorconfig", "makefile"
    );

    private String resolveUserId(String headerUserId, String cookieUserId) {
        if (headerUserId != null && !headerUserId.isBlank()) {
            return headerUserId;
        }
        if (cookieUserId != null && !cookieUserId.isBlank()) {
            return cookieUserId;
        }
        return null;
    }
    
    /**
     * List directory contents
     */
    @GET
    @Path("/list")
    @Produces(MediaType.APPLICATION_JSON)
    public Response listDirectory(
            @HeaderParam("X-Session-ID") String sessionId,
            @HeaderParam("X-User-ID") String headerUserId,
            @CookieParam("filesurf_userId") String cookieUserId,
            @QueryParam("path") String relativePath) {

        LOGGER.info("Listing directory for session: " + sessionId + ", path: " + relativePath);

        if (sessionId == null || sessionId.trim().isEmpty()) {
            return Response.status(Response.Status.BAD_REQUEST)
                    .entity("{\"error\": \"No session ID provided\"}")
                    .build();
        }

        String userId = resolveUserId(headerUserId, cookieUserId);
        if (userId == null || userId.trim().isEmpty()) {
            return Response.status(Response.Status.BAD_REQUEST)
                    .entity("{\"error\": \"No user ID provided\"}")
                    .build();
        }

        try {
            // Get session directory
            java.nio.file.Path sessionDir = sessionManager.getSessionDirectory(sessionId, userId);
            
            // Resolve the requested path (relative to session directory)
            java.nio.file.Path targetPath;
            if (relativePath == null || relativePath.trim().isEmpty() || relativePath.equals("/")) {
                targetPath = sessionDir;
            } else {
                // Sanitize the path to prevent directory traversal
                String sanitizedPath = sanitizePath(relativePath);
                targetPath = sessionDir.resolve(sanitizedPath).normalize();
                
                // Ensure the path is within the session directory
                if (!targetPath.startsWith(sessionDir)) {
                    return Response.status(Response.Status.BAD_REQUEST)
                            .entity("{\"error\": \"Invalid path\"}")
                            .build();
                }
            }
            
            // Check if path exists and is a directory
            if (!Files.exists(targetPath)) {
                return Response.status(Response.Status.NOT_FOUND)
                        .entity("{\"error\": \"Directory not found\"}")
                        .build();
            }
            
            if (!Files.isDirectory(targetPath)) {
                return Response.status(Response.Status.BAD_REQUEST)
                        .entity("{\"error\": \"Path is not a directory\"}")
                        .build();
            }
            
            // Create file filter for ignore patterns
            FileFilter fileFilter = createFileFilter(sessionDir);
            LOGGER.fine("Created file filter with " + fileFilter.getPatternCount() + " patterns for session: " + sessionId);

            // List files and directories
            List<Map<String, Object>> items = new ArrayList<>();
            java.util.concurrent.atomic.AtomicInteger totalFiles = new java.util.concurrent.atomic.AtomicInteger(0);
            java.util.concurrent.atomic.AtomicInteger ignoredFiles = new java.util.concurrent.atomic.AtomicInteger(0);
            
            try (var stream = Files.list(targetPath)) {
                stream.forEach(path -> {
                    totalFiles.incrementAndGet();
                    if (shouldIgnore(path, sessionDir, fileFilter)) {
                        ignoredFiles.incrementAndGet();
                        return;
                    }
                    try {
                        Map<String, Object> item = new HashMap<>();
                        String name = path.getFileName().toString();
                        
                        // Get file attributes
                        BasicFileAttributes attrs = Files.readAttributes(path, BasicFileAttributes.class);
                        
                        item.put("name", name);
                        item.put("path", sessionDir.relativize(path).toString());
                        item.put("type", attrs.isDirectory() ? "directory" : "file");
                        item.put("size", attrs.size());
                        item.put("modified", formatFileTime(attrs.lastModifiedTime()));
                        item.put("created", formatFileTime(attrs.creationTime()));
                        
                        // Determine icon based on file type
                        item.put("icon", getFileIcon(name, attrs.isDirectory()));
                        
                        // Get file extension
                        if (!attrs.isDirectory()) {
                            int dotIndex = name.lastIndexOf('.');
                            if (dotIndex > 0) {
                                item.put("extension", name.substring(dotIndex + 1).toLowerCase());
                            }
                        }
                        
                        items.add(item);
                    } catch (IOException e) {
                        LOGGER.warning("Failed to read file attributes for: " + path + " - " + e.getMessage());
                        metricsService.incrementErrors("file_attribute_read");
                    }
                });
            }
            
            LOGGER.fine("File listing complete. Total files: " + totalFiles.get() + ", Ignored: " + ignoredFiles.get() + ", Showing: " + items.size());
            
            // Track metrics for file operation
            metricsService.incrementFileOperations();
            metricsService.trackUserActivity(userId);
            
            // Sort: directories first, then files, both alphabetically
            items.sort((a, b) -> {
                boolean aIsDir = "directory".equals(a.get("type"));
                boolean bIsDir = "directory".equals(b.get("type"));
                
                if (aIsDir && !bIsDir) return -1;
                if (!aIsDir && bIsDir) return 1;
                
                String aName = (String) a.get("name");
                String bName = (String) b.get("name");
                return aName.compareToIgnoreCase(bName);
            });
            
            // Build response
            Map<String, Object> response = new HashMap<>();
            response.put("success", true);
            response.put("items", items);
            response.put("count", items.size());
            response.put("currentPath", sessionDir.relativize(targetPath).toString());
            response.put("absolutePath", targetPath.toString());
            
            return Response.ok(response).build();
            
        } catch (IOException e) {
            LOGGER.severe("Failed to list directory: " + e.getMessage());
            metricsService.incrementErrors("directory_listing");
            return Response.status(Response.Status.INTERNAL_SERVER_ERROR)
                    .entity("{\"error\": \"Failed to list directory: " + e.getMessage() + "\"}")
                    .build();
        }
    }
    
    /**
     * Preview file contents (for text files only)
     */
    @GET
    @Path("/preview")
    @Produces(MediaType.TEXT_PLAIN)
    public Response previewFile(
            @HeaderParam("X-Session-ID") String sessionId,
            @HeaderParam("X-User-ID") String headerUserId,
            @CookieParam("filesurf_userId") String cookieUserId,
            @QueryParam("path") String filePath) {

        LOGGER.info("Previewing file for session: " + sessionId + ", path: " + filePath);

        if (sessionId == null || sessionId.trim().isEmpty()) {
            return Response.status(Response.Status.BAD_REQUEST)
                    .entity("No session ID provided")
                    .build();
        }

        String userId = resolveUserId(headerUserId, cookieUserId);
        if (userId == null || userId.trim().isEmpty()) {
            return Response.status(Response.Status.BAD_REQUEST)
                    .entity("No user ID provided")
                    .build();
        }

        if (filePath == null || filePath.trim().isEmpty()) {
            return Response.status(Response.Status.BAD_REQUEST)
                    .entity("No file path provided")
                    .build();
        }

        try {
            // Get session directory
            java.nio.file.Path sessionDir = sessionManager.getSessionDirectory(sessionId, userId);

            // Create file filter for ignore patterns
            FileFilter fileFilter = createFileFilter(sessionDir);
            
            // Sanitize and resolve the file path
            String sanitizedPath = sanitizePath(filePath);
            java.nio.file.Path targetFile = sessionDir.resolve(sanitizedPath).normalize();
            
            // Ensure the file is within the session directory
            if (!targetFile.startsWith(sessionDir)) {
                return Response.status(Response.Status.BAD_REQUEST)
                        .entity("Invalid file path")
                        .build();
            }

            if (shouldIgnore(targetFile, sessionDir, fileFilter)) {
                return Response.status(Response.Status.NOT_FOUND)
                        .entity("File not found")
                        .build();
            }
            
            // Check if file exists and is a regular file
            if (!Files.exists(targetFile)) {
                return Response.status(Response.Status.NOT_FOUND)
                        .entity("File not found")
                        .build();
            }
            
            if (!Files.isRegularFile(targetFile)) {
                return Response.status(Response.Status.BAD_REQUEST)
                        .entity("Path is not a file")
                        .build();
            }
            
            // Check file size
            long fileSize = Files.size(targetFile);
            if (fileSize > MAX_PREVIEW_SIZE) {
                return Response.ok("File too large for preview (" + formatFileSize(fileSize) + ").\n" +
                                  "Only showing first " + formatFileSize(MAX_PREVIEW_SIZE) + ".")
                        .build();
            }
            
            // Check if file extension is in whitelist
            String fileName = targetFile.getFileName().toString();
            if (!isPreviewableExtension(fileName)) {
                return Response.ok("File type not supported for preview: " + fileName + " (size: " + formatFileSize(fileSize) + ")")
                        .build();
            }
            
            // Read file content as bytes first
            byte[] fileBytes = Files.readAllBytes(targetFile);
            
            // For binary files, check if it's text
            if (!isTextFile(fileBytes)) {
                return Response.ok("Binary file (size: " + formatFileSize(fileSize) + ")")
                        .build();
            }
            
            // Convert to string if it's text
            String content = new String(fileBytes, java.nio.charset.StandardCharsets.UTF_8);
            
            // Limit preview to first 1000 characters
            int previewLength = Math.min(content.length(), 1000);
            String preview = content.substring(0, previewLength);
            
            if (content.length() > previewLength) {
                preview += "\n\n... (truncated, " + formatFileSize(fileSize) + " total)";
            }
            
            return Response.ok(preview).build();
            
        } catch (IOException e) {
            LOGGER.severe("Failed to preview file: " + e.getMessage());
            return Response.status(Response.Status.INTERNAL_SERVER_ERROR)
                    .entity("Failed to preview file: " + e.getMessage())
                    .build();
        }
    }
    
    /**
     * Compile LaTeX file to PDF
     */
    @POST
    @Path("/compile-latex")
    @Produces(MediaType.APPLICATION_JSON)
    public Response compileLatex(
            @HeaderParam("X-Session-ID") String sessionId,
            @HeaderParam("X-User-ID") String headerUserId,
            @CookieParam("filesurf_userId") String cookieUserId,
            @QueryParam("path") String filePath,
            @QueryParam("engine") String engine) {
        
        LOGGER.info("Compiling LaTeX file for session: " + sessionId + ", path: " + filePath + ", engine: " + engine);
        
        if (sessionId == null || sessionId.trim().isEmpty()) {
            return Response.status(Response.Status.BAD_REQUEST)
                    .entity("{\"success\": false, \"error\": \"No session ID provided\"}")
                    .build();
        }
        
        String userId = resolveUserId(headerUserId, cookieUserId);
        if (userId == null || userId.trim().isEmpty()) {
            return Response.status(Response.Status.BAD_REQUEST)
                    .entity("{\"success\": false, \"error\": \"No user ID provided\"}")
                    .build();
        }
        
        if (filePath == null || filePath.trim().isEmpty()) {
            return Response.status(Response.Status.BAD_REQUEST)
                    .entity("{\"success\": false, \"error\": \"No file path provided\"}")
                    .build();
        }
        
        try {
            // Get session directory
            java.nio.file.Path sessionDir = sessionManager.getSessionDirectory(sessionId, userId);
            
            // Create file filter for ignore patterns
            FileFilter fileFilter = createFileFilter(sessionDir);
            
            // Sanitize and resolve the file path
            String sanitizedPath = sanitizePath(filePath);
            java.nio.file.Path targetFile = sessionDir.resolve(sanitizedPath).normalize();
            
            // Ensure the file is within the session directory
            if (!targetFile.startsWith(sessionDir)) {
                return Response.status(Response.Status.BAD_REQUEST)
                        .entity("{\"success\": false, \"error\": \"Invalid file path\"}")
                        .build();
            }
            
            if (shouldIgnore(targetFile, sessionDir, fileFilter)) {
                return Response.status(Response.Status.NOT_FOUND)
                        .entity("{\"success\": false, \"error\": \"File not found\"}")
                        .build();
            }
            
            // Check if file exists and is a regular file
            if (!Files.exists(targetFile)) {
                return Response.status(Response.Status.NOT_FOUND)
                        .entity("{\"success\": false, \"error\": \"File not found\"}")
                        .build();
            }
            
            if (!Files.isRegularFile(targetFile)) {
                return Response.status(Response.Status.BAD_REQUEST)
                        .entity("{\"success\": false, \"error\": \"Path is not a file\"}")
                        .build();
            }
            
            // Check if it's a .tex file
            String fileName = targetFile.getFileName().toString();
            if (!fileName.toLowerCase().endsWith(".tex")) {
                return Response.status(Response.Status.BAD_REQUEST)
                        .entity("{\"success\": false, \"error\": \"File is not a LaTeX (.tex) file\"}")
                        .build();
            }
            
            // Compile LaTeX to PDF
            java.nio.file.Path pdfFile = latexCompilerService.compileToPdf(targetFile, null, engine);
            
            if (pdfFile == null) {
                return Response.status(Response.Status.INTERNAL_SERVER_ERROR)
                        .entity("{\"success\": false, \"error\": \"LaTeX compilation failed\"}")
                        .build();
            }
            
            // Get relative path for the PDF
            java.nio.file.Path relativePdfPath = sessionDir.relativize(pdfFile);
            
            // Return success with PDF path
            Map<String, Object> response = new HashMap<>();
            response.put("success", true);
            response.put("pdfPath", relativePdfPath.toString());
            response.put("pdfSize", Files.size(pdfFile));
            
            return Response.ok(response).build();
            
        } catch (IOException e) {
            LOGGER.severe("Failed to compile LaTeX: " + e.getMessage());
            return Response.status(Response.Status.INTERNAL_SERVER_ERROR)
                    .entity("{\"success\": false, \"error\": \"Failed to compile LaTeX: " + e.getMessage() + "\"}")
                    .build();
        }
    }
    
    /**
     * Open or download a file. PDFs and common previewable types are returned inline so the browser can render them.
     */
    @GET
    @Path("/open")
    public Response openFile(
            @HeaderParam("X-Session-ID") String sessionId,
            @HeaderParam("X-User-ID") String headerUserId,
            @CookieParam("filesurf_userId") String cookieUserId,
            @QueryParam("path") String filePath) {

        LOGGER.info("Opening file for session: " + sessionId + ", path: " + filePath);

        if (sessionId == null || sessionId.trim().isEmpty()) {
            return Response.status(Response.Status.BAD_REQUEST)
                    .entity("No session ID provided")
                    .build();
        }

        String userId = resolveUserId(headerUserId, cookieUserId);
        if (userId == null || userId.trim().isEmpty()) {
            return Response.status(Response.Status.BAD_REQUEST)
                    .entity("No user ID provided")
                    .build();
        }

        if (filePath == null || filePath.trim().isEmpty()) {
            return Response.status(Response.Status.BAD_REQUEST)
                    .entity("No file path provided")
                    .build();
        }

        try {
            java.nio.file.Path sessionDir = sessionManager.getSessionDirectory(sessionId, userId);
            FileFilter fileFilter = createFileFilter(sessionDir);

            String sanitizedPath = sanitizePath(filePath);
            java.nio.file.Path targetFile = sessionDir.resolve(sanitizedPath).normalize();

            if (!targetFile.startsWith(sessionDir)) {
                return Response.status(Response.Status.BAD_REQUEST)
                        .entity("Invalid file path")
                        .build();
            }

            if (shouldIgnore(targetFile, sessionDir, fileFilter)) {
                return Response.status(Response.Status.NOT_FOUND)
                        .entity("File not found")
                        .build();
            }

            if (!Files.exists(targetFile)) {
                return Response.status(Response.Status.NOT_FOUND)
                        .entity("File not found")
                        .build();
            }

            if (!Files.isRegularFile(targetFile)) {
                return Response.status(Response.Status.BAD_REQUEST)
                        .entity("Path is not a file")
                        .build();
            }

            String fileName = targetFile.getFileName().toString();
            String contentType = detectContentType(targetFile, fileName);

            Response.ResponseBuilder builder = Response.ok(targetFile.toFile());
            if (contentType != null) {
                builder.type(contentType);
            }

            String dispositionType = (contentType != null && (contentType.startsWith("image/") ||
                    contentType.startsWith("text/") || "application/pdf".equals(contentType))) ? "inline" : "attachment";
            builder.header("Content-Disposition", dispositionType + "; filename=\"" + fileName + "\"");

            return builder.build();
        } catch (IOException e) {
            LOGGER.severe("Failed to open file: " + e.getMessage());
            return Response.status(Response.Status.INTERNAL_SERVER_ERROR)
                    .entity("Failed to open file: " + e.getMessage())
                    .build();
        }
    }

    private String detectContentType(java.nio.file.Path targetFile, String fileName) {
        try {
            String contentType = Files.probeContentType(targetFile);
            if (contentType != null) {
                return contentType;
            }
        } catch (IOException e) {
            LOGGER.fine("Content type detection failed for " + fileName + ": " + e.getMessage());
        }

        String lowerName = fileName.toLowerCase();
        if (lowerName.endsWith(".pdf")) return "application/pdf";
        if (lowerName.endsWith(".png")) return "image/png";
        if (lowerName.endsWith(".jpg") || lowerName.endsWith(".jpeg")) return "image/jpeg";
        if (lowerName.endsWith(".gif")) return "image/gif";
        if (lowerName.endsWith(".webp")) return "image/webp";
        if (lowerName.endsWith(".txt")) return MediaType.TEXT_PLAIN;
        if (lowerName.endsWith(".json")) return MediaType.APPLICATION_JSON;
        if (lowerName.endsWith(".xml")) return MediaType.APPLICATION_XML;
        if (lowerName.endsWith(".html") || lowerName.endsWith(".htm")) return "text/html";
        return "application/octet-stream";
    }

    /**
     * Download file (always forces download as attachment)
     */
    @GET
    @Path("/download")
    public Response downloadFile(
            @HeaderParam("X-Session-ID") String sessionId,
            @HeaderParam("X-User-ID") String headerUserId,
            @CookieParam("filesurf_userId") String cookieUserId,
            @QueryParam("path") String filePath) {

        LOGGER.info("Downloading file for session: " + sessionId + ", path: " + filePath);

        if (sessionId == null || sessionId.trim().isEmpty()) {
            return Response.status(Response.Status.BAD_REQUEST)
                    .entity("No session ID provided")
                    .build();
        }

        String userId = resolveUserId(headerUserId, cookieUserId);
        if (userId == null || userId.trim().isEmpty()) {
            return Response.status(Response.Status.BAD_REQUEST)
                    .entity("No user ID provided")
                    .build();
        }

        if (filePath == null || filePath.trim().isEmpty()) {
            return Response.status(Response.Status.BAD_REQUEST)
                    .entity("No file path provided")
                    .build();
        }

        try {
            java.nio.file.Path sessionDir = sessionManager.getSessionDirectory(sessionId, userId);
            FileFilter fileFilter = createFileFilter(sessionDir);

            String sanitizedPath = sanitizePath(filePath);
            java.nio.file.Path targetFile = sessionDir.resolve(sanitizedPath).normalize();

            if (!targetFile.startsWith(sessionDir)) {
                return Response.status(Response.Status.BAD_REQUEST)
                        .entity("Invalid file path")
                        .build();
            }

            if (shouldIgnore(targetFile, sessionDir, fileFilter)) {
                return Response.status(Response.Status.NOT_FOUND)
                        .entity("File not found")
                        .build();
            }

            if (!Files.exists(targetFile)) {
                return Response.status(Response.Status.NOT_FOUND)
                        .entity("File not found")
                        .build();
            }

            if (!Files.isRegularFile(targetFile)) {
                return Response.status(Response.Status.BAD_REQUEST)
                        .entity("Path is not a file")
                        .build();
            }

            String fileName = targetFile.getFileName().toString();
            String contentType = detectContentType(targetFile, fileName);

            Response.ResponseBuilder builder = Response.ok(targetFile.toFile());
            if (contentType != null) {
                builder.type(contentType);
            }

            // Always force download as attachment
            builder.header("Content-Disposition", "attachment; filename=\"" + fileName + "\"");

            return builder.build();
        } catch (IOException e) {
            LOGGER.severe("Failed to download file: " + e.getMessage());
            return Response.status(Response.Status.INTERNAL_SERVER_ERROR)
                    .entity("Failed to download file: " + e.getMessage())
                    .build();
        }
    }
    
    /**
     * Get file metadata
     */
    @GET
    @Path("/metadata")
    @Produces(MediaType.APPLICATION_JSON)
    public Response getFileMetadata(
            @HeaderParam("X-Session-ID") String sessionId,
            @HeaderParam("X-User-ID") String headerUserId,
            @CookieParam("filesurf_userId") String cookieUserId,
            @QueryParam("path") String filePath) {

        LOGGER.info("Getting file metadata for session: " + sessionId + ", path: " + filePath);

        if (sessionId == null || sessionId.trim().isEmpty()) {
            return Response.status(Response.Status.BAD_REQUEST)
                    .entity("{\"error\": \"No session ID provided\"}")
                    .build();
        }

        String userId = resolveUserId(headerUserId, cookieUserId);
        if (userId == null || userId.trim().isEmpty()) {
            return Response.status(Response.Status.BAD_REQUEST)
                    .entity("{\"error\": \"No user ID provided\"}")
                    .build();
        }

        if (filePath == null || filePath.trim().isEmpty()) {
            return Response.status(Response.Status.BAD_REQUEST)
                    .entity("{\"error\": \"No file path provided\"}")
                    .build();
        }

        try {
            // Get session directory
            java.nio.file.Path sessionDir = sessionManager.getSessionDirectory(sessionId, userId);

            // Create file filter for ignore patterns
            FileFilter fileFilter = createFileFilter(sessionDir);
            
            // Sanitize and resolve the file path
            String sanitizedPath = sanitizePath(filePath);
            java.nio.file.Path targetFile = sessionDir.resolve(sanitizedPath).normalize();
            
            // Ensure the file is within the session directory
            if (!targetFile.startsWith(sessionDir)) {
                return Response.status(Response.Status.BAD_REQUEST)
                        .entity("{\"error\": \"Invalid file path\"}")
                        .build();
            }

            if (shouldIgnore(targetFile, sessionDir, fileFilter)) {
                return Response.status(Response.Status.NOT_FOUND)
                        .entity("{\"error\": \"File not found\"}")
                        .build();
            }
            
            // Check if file exists
            if (!Files.exists(targetFile)) {
                return Response.status(Response.Status.NOT_FOUND)
                        .entity("{\"error\": \"File not found\"}")
                        .build();
            }
            
            // Get file attributes
            BasicFileAttributes attrs = Files.readAttributes(targetFile, BasicFileAttributes.class);
            
            Map<String, Object> metadata = new HashMap<>();
            metadata.put("name", targetFile.getFileName().toString());
            metadata.put("path", sessionDir.relativize(targetFile).toString());
            metadata.put("type", attrs.isDirectory() ? "directory" : "file");
            metadata.put("size", attrs.size());
            metadata.put("sizeFormatted", formatFileSize(attrs.size()));
            metadata.put("modified", formatFileTime(attrs.lastModifiedTime()));
            metadata.put("created", formatFileTime(attrs.creationTime()));
            metadata.put("isDirectory", attrs.isDirectory());
            metadata.put("isRegularFile", attrs.isRegularFile());
            metadata.put("isSymbolicLink", attrs.isSymbolicLink());
            metadata.put("absolutePath", targetFile.toString());
            
            // Get file extension for files
            if (attrs.isRegularFile()) {
                String name = targetFile.getFileName().toString();
                int dotIndex = name.lastIndexOf('.');
                if (dotIndex > 0) {
                    metadata.put("extension", name.substring(dotIndex + 1).toLowerCase());
                }
            }
            
            Map<String, Object> response = new HashMap<>();
            response.put("success", true);
            response.put("metadata", metadata);
            
            return Response.ok(response).build();
            
        } catch (IOException e) {
            LOGGER.severe("Failed to get file metadata: " + e.getMessage());
            return Response.status(Response.Status.INTERNAL_SERVER_ERROR)
                    .entity("{\"error\": \"Failed to get file metadata: " + e.getMessage() + "\"}")
                    .build();
        }
    }
    
    /**
     * Sanitize path to prevent directory traversal
     */
    private String sanitizePath(String path) {
        if (path == null) return "";
        
        // Remove any leading/trailing slashes and normalize
        path = path.trim().replace("\\", "/");
        
        // Remove any attempt at directory traversal
        path = path.replace("../", "").replace("..\\", "");
        
        // Split by path separator and filter out empty parts
        String[] parts = path.split("/");
        StringBuilder sanitized = new StringBuilder();
        
        for (String part : parts) {
            if (!part.isEmpty() && !part.equals(".")) {
                if (sanitized.length() > 0) {
                    sanitized.append("/");
                }
                sanitized.append(part);
            }
        }
        
        return sanitized.toString();
    }
    
    /**
     * Format file time to readable string
     */
    private String formatFileTime(FileTime fileTime) {
        if (fileTime == null) return "Unknown";
        Instant instant = fileTime.toInstant();
        return DATE_FORMATTER.format(instant);
    }
    
    /**
     * Format file size to human readable format
     */
    private String formatFileSize(long size) {
        if (size < 1024) {
            return size + " B";
        } else if (size < 1024 * 1024) {
            return String.format("%.1f KB", size / 1024.0);
        } else if (size < 1024 * 1024 * 1024) {
            return String.format("%.1f MB", size / (1024.0 * 1024.0));
        } else {
            return String.format("%.1f GB", size / (1024.0 * 1024.0 * 1024.0));
        }
    }
    
    /**
     * Determine icon based on file name and type
     */
    private String getFileIcon(String fileName, boolean isDirectory) {
        if (isDirectory) {
            return "folder";
        }
        
        String lowerName = fileName.toLowerCase();
        
        if (lowerName.endsWith(".pdf")) {
            return "file-pdf";
        } else if (lowerName.endsWith(".png") || lowerName.endsWith(".jpg") || 
                   lowerName.endsWith(".jpeg") || lowerName.endsWith(".gif") || 
                   lowerName.endsWith(".svg") || lowerName.endsWith(".webp")) {
            return "file-image";
        } else if (lowerName.endsWith(".txt") || lowerName.endsWith(".md") || 
                   lowerName.endsWith(".json") || lowerName.endsWith(".xml") || 
                   lowerName.endsWith(".yaml") || lowerName.endsWith(".yml")) {
            return "file-text";
        } else if (lowerName.endsWith(".csv") || lowerName.endsWith(".xlsx") || 
                   lowerName.endsWith(".xls")) {
            return "file-spreadsheet";
        } else if (lowerName.endsWith(".zip") || lowerName.endsWith(".tar") || 
                   lowerName.endsWith(".gz") || lowerName.endsWith(".7z")) {
            return "file-archive";
        } else if (lowerName.endsWith(".sh") || lowerName.endsWith(".bash") || 
                   lowerName.endsWith(".zsh")) {
            return "file-script";
        } else if (lowerName.endsWith(".java") || lowerName.endsWith(".js") || 
                   lowerName.endsWith(".ts") || lowerName.endsWith(".py") || 
                   lowerName.endsWith(".cpp") || lowerName.endsWith(".c") || 
                   lowerName.endsWith(".html") || lowerName.endsWith(".css")) {
            return "file-code";
        } else if (lowerName.endsWith(".tex")) {
            return "file-latex";
        } else if (lowerName.endsWith(".db") || lowerName.endsWith(".sqlite") || 
                   lowerName.endsWith(".sql")) {
            return "file-database";
        } else {
            return "file";
        }
    }
    
    /**
     * Check if file content is text (simple heuristic)
     */
    private boolean isTextFile(byte[] content) {
        if (content == null || content.length == 0) {
            return true;
        }
        
        // Check for null bytes and control characters (except common ones like \n, \r, \t)
        for (int i = 0; i < Math.min(content.length, 1000); i++) {
            byte b = content[i];
            int unsigned = b & 0xFF; // Convert to unsigned
            
            if (unsigned == 0) {
                return false; // Null byte indicates binary file
            }
            if (unsigned < 32 && unsigned != '\n' && unsigned != '\r' && unsigned != '\t' && unsigned != '\f' && unsigned != '\b') {
                return false; // Uncommon control character
            }
        }
        
        return true;
    }
    
    /**
     * Check if string content is text (simple heuristic)
     */
    private boolean isTextFile(String content) {
        if (content == null || content.isEmpty()) {
            return true;
        }
        
        // Check for null bytes and control characters (except common ones like \n, \r, \t)
        for (int i = 0; i < Math.min(content.length(), 1000); i++) {
            char c = content.charAt(i);
            if (c == 0) {
                return false; // Null byte indicates binary file
            }
            // char is unsigned in Java, so we only need to check for control characters (0-31)
            if (c < 32 && c != '\n' && c != '\r' && c != '\t' && c != '\f' && c != '\b') {
                return false; // Uncommon control character
            }
        }
        
        return true;
    }
    
    /**
     * Check if file extension is in the whitelist for preview
     */
    private boolean isPreviewableExtension(String fileName) {
        if (fileName == null || fileName.isEmpty()) {
            return false;
        }
        
        int dotIndex = fileName.lastIndexOf('.');
        if (dotIndex <= 0 || dotIndex == fileName.length() - 1) {
            return false; // No extension or dot at the end
        }
        
        String extension = fileName.substring(dotIndex + 1).toLowerCase();
        return PREVIEWABLE_EXTENSIONS.contains(extension);
    }

    /**
     * Load ignore patterns from .fileexplorerignore in the session root.
     * Supports glob syntax understood by PathMatcher. Ignores empty/comment lines.
     * @deprecated Use {@link FileFilter} instead
     */
    @Deprecated
    protected List<java.nio.file.PathMatcher> loadIgnorePatterns(java.nio.file.Path sessionDir) {
        java.nio.file.Path ignoreFile = sessionDir.resolve(".fileexplorerignore");
        try {
            return FileFilter.loadIgnorePatterns(ignoreFile);
        } catch (IOException e) {
            LOGGER.warning("Failed to read .fileexplorerignore: " + e.getMessage());
            return new ArrayList<>();
        }
    }
    
    /**
     * Create a FileFilter instance for the session directory.
     * Loads patterns from .fileexplorerignore if present.
     */
    protected FileFilter createFileFilter(java.nio.file.Path sessionDir) {
        java.nio.file.Path ignoreFile = sessionDir.resolve(".fileexplorerignore");
        try {
            return new FileFilter(ignoreFile);
        } catch (IOException e) {
            LOGGER.warning("Failed to create FileFilter from .fileexplorerignore: " + e.getMessage());
            return new FileFilter();
        }
    }

    /**
     * Decide whether to hide a path based on ignore patterns relative to session root.
     * @deprecated Use {@link #shouldIgnore(java.nio.file.Path, java.nio.file.Path, FileFilter)} instead
     */
    @Deprecated
    protected boolean shouldIgnore(java.nio.file.Path path, java.nio.file.Path sessionDir, List<java.nio.file.PathMatcher> matchers) {
        if (matchers == null || matchers.isEmpty()) {
            LOGGER.finest("No ignore patterns to check against");
            return false;
        }
        java.nio.file.Path rel = sessionDir.relativize(path);
        // Normalize to use system separators
        java.nio.file.Path normalized = Paths.get("/" + rel.toString().replace('\\', '/'));
        String normalizedStr = normalized.toString();
        LOGGER.fine("Checking if path should be ignored: " + path.getFileName() + " (normalized: " + normalizedStr + ")");
        
        for (int i = 0; i < matchers.size(); i++) {
            java.nio.file.PathMatcher matcher = matchers.get(i);
            try {
                boolean matches = matcher.matches(normalized);
                if (matches) {
                    LOGGER.fine("  ✓ Matched pattern #" + i + " - ignoring file: " + path.getFileName());
                    return true;
                } else {
                    LOGGER.finest("  ✗ Did not match pattern #" + i);
                }
            } catch (Exception e) {
                LOGGER.warning("Error checking pattern #" + i + " against path " + normalizedStr + ": " + e.getMessage());
                // Ignore matcher errors to avoid breaking listing
            }
        }
        LOGGER.fine("  No patterns matched - showing file: " + path.getFileName());
        return false;
    }
    
    /**
     * Decide whether to hide a path based on ignore patterns using FileFilter.
     */
    protected boolean shouldIgnore(java.nio.file.Path path, java.nio.file.Path sessionDir, FileFilter filter) {
        return filter.shouldIgnore(path, sessionDir);
    }
}
