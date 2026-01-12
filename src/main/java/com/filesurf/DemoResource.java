package com.filesurf;

import io.quarkus.qute.Template;
import io.quarkus.qute.TemplateInstance;
import jakarta.inject.Inject;
import jakarta.ws.rs.*;
import jakarta.ws.rs.core.*;
import org.eclipse.microprofile.config.inject.ConfigProperty;

import java.io.IOException;
import java.io.InputStream;
import java.io.RandomAccessFile;
import java.nio.file.Files;
import java.nio.file.Paths;
import java.util.*;
import java.util.logging.Logger;
import java.util.regex.Pattern;

/**
 * REST endpoint for serving demo MP4 videos.
 * 
 * Security considerations:
 * - Protected by AuthenticationFilter (requires valid user session)
 * - Only serves .mp4 files (whitelist approach)
 * - Path traversal protection via strict validation
 * - Supports HTTP Range requests for efficient video streaming
 * - All access is logged for audit purposes
 */
@Path("/demo")
public class DemoResource {

    private static final Logger LOGGER = Logger.getLogger(DemoResource.class.getName());
    
    // Only allow alphanumeric, hyphen, underscore in filenames (no dots except for extension)
    private static final Pattern SAFE_FILENAME_PATTERN = Pattern.compile("^[a-zA-Z0-9_-]+$");
    
    // Content type for MP4 videos
    private static final String MP4_CONTENT_TYPE = "video/mp4";
    
    // Maximum file size to serve (1 GB)
    private static final long MAX_FILE_SIZE = 1L * 1024 * 1024 * 1024;

    @ConfigProperty(name = "demo.videos.directory", defaultValue = "data/demos")
    String demosDirectory;
    
    @Inject
    com.filesurf.service.MetricsService metricsService;
    
    @Inject
    Template demos;

    /**
     * Serve the demo videos page.
     */
    @GET
    @Produces(MediaType.TEXT_HTML)
    public TemplateInstance getDemosPage() {
        LOGGER.info("Serving demo videos page");
        return demos.instance();
    }

    /**
     * List available demo videos.
     * Returns a JSON array of available demo video names (without extension).
     */
    @GET
    @Path("/list")
    @Produces(MediaType.APPLICATION_JSON)
    public Response listDemos() {
        LOGGER.info("Listing demo videos");
        
        try {
            java.nio.file.Path demoDir = Paths.get(demosDirectory);
            
            if (!Files.exists(demoDir) || !Files.isDirectory(demoDir)) {
                LOGGER.warning("Demo directory does not exist: " + demosDirectory);
                return Response.ok()
                        .entity("{\"demos\": [], \"count\": 0}")
                        .build();
            }
            
            List<Map<String, Object>> demos = new ArrayList<>();
            
            try (var stream = Files.list(demoDir)) {
                stream.filter(path -> {
                    String filename = path.getFileName().toString().toLowerCase();
                    return filename.endsWith(".mp4") && Files.isRegularFile(path);
                }).forEach(path -> {
                    try {
                        String filename = path.getFileName().toString();
                        String name = filename.substring(0, filename.length() - 4); // Remove .mp4
                        
                        Map<String, Object> demo = new HashMap<>();
                        demo.put("name", name);
                        demo.put("filename", filename);
                        demo.put("size", Files.size(path));
                        demo.put("sizeFormatted", formatFileSize(Files.size(path)));
                        demos.add(demo);
                    } catch (IOException e) {
                        LOGGER.warning("Failed to read demo file attributes: " + path + " - " + e.getMessage());
                    }
                });
            }
            
            // Sort by name
            demos.sort((a, b) -> ((String) a.get("name")).compareToIgnoreCase((String) b.get("name")));
            
            Map<String, Object> response = new HashMap<>();
            response.put("demos", demos);
            response.put("count", demos.size());
            
            return Response.ok(response).build();
            
        } catch (IOException e) {
            LOGGER.severe("Failed to list demo videos: " + e.getMessage());
            return Response.status(Response.Status.INTERNAL_SERVER_ERROR)
                    .entity("{\"error\": \"Failed to list demo videos\"}")
                    .build();
        }
    }

    /**
     * Stream a demo video by name.
     * Supports HTTP Range requests for efficient video streaming.
     * 
     * @param name The demo video name (without .mp4 extension)
     * @param rangeHeader Optional Range header for partial content requests
     */
    @GET
    @Path("/{name}")
    @Produces(MP4_CONTENT_TYPE)
    public Response streamDemo(
            @PathParam("name") String name,
            @HeaderParam("Range") String rangeHeader,
            @CookieParam("filesurf_userId") String userId) {
        
        LOGGER.info("Demo video request: " + name + " by user: " + (userId != null ? userId : "unknown") + 
                   (rangeHeader != null ? " (Range: " + rangeHeader + ")" : ""));
        
        // Validate filename - strict whitelist approach
        if (name == null || name.isEmpty()) {
            LOGGER.warning("Demo request with empty name");
            return Response.status(Response.Status.BAD_REQUEST)
                    .entity("Demo name is required")
                    .type(MediaType.TEXT_PLAIN)
                    .build();
        }
        
        // Security: Only allow safe characters in filename
        if (!SAFE_FILENAME_PATTERN.matcher(name).matches()) {
            LOGGER.warning("Demo request with invalid name pattern: " + name);
            return Response.status(Response.Status.BAD_REQUEST)
                    .entity("Invalid demo name")
                    .type(MediaType.TEXT_PLAIN)
                    .build();
        }
        
        // Security: Limit name length to prevent buffer issues
        if (name.length() > 100) {
            LOGGER.warning("Demo request with name too long: " + name.length() + " chars");
            return Response.status(Response.Status.BAD_REQUEST)
                    .entity("Demo name too long")
                    .type(MediaType.TEXT_PLAIN)
                    .build();
        }
        
        try {
            // Build the path - only look for .mp4 files
            java.nio.file.Path demoDir = Paths.get(demosDirectory).toAbsolutePath().normalize();
            java.nio.file.Path videoPath = demoDir.resolve(name + ".mp4").normalize();
            
            // Security: Ensure the resolved path is within the demo directory
            if (!videoPath.startsWith(demoDir)) {
                LOGGER.severe("Path traversal attempt detected: " + name);
                return Response.status(Response.Status.FORBIDDEN)
                        .entity("Access denied")
                        .type(MediaType.TEXT_PLAIN)
                        .build();
            }
            
            // Check if file exists
            if (!Files.exists(videoPath)) {
                LOGGER.info("Demo video not found: " + name);
                return Response.status(Response.Status.NOT_FOUND)
                        .entity("Demo video not found")
                        .type(MediaType.TEXT_PLAIN)
                        .build();
            }
            
            // Verify it's a regular file (not a symlink, directory, etc.)
            if (!Files.isRegularFile(videoPath)) {
                LOGGER.warning("Demo path is not a regular file: " + name);
                return Response.status(Response.Status.NOT_FOUND)
                        .entity("Demo video not found")
                        .type(MediaType.TEXT_PLAIN)
                        .build();
            }
            
            // Get file size
            long fileSize = Files.size(videoPath);
            
            // Security: Check file size limit
            if (fileSize > MAX_FILE_SIZE) {
                LOGGER.warning("Demo video exceeds size limit: " + name + " (" + fileSize + " bytes)");
                return Response.status(Response.Status.FORBIDDEN)
                        .entity("Video file too large")
                        .type(MediaType.TEXT_PLAIN)
                        .build();
            }
            
            // Track metrics
            if (metricsService != null) {
                metricsService.incrementFileOperations();
                if (userId != null) {
                    metricsService.trackUserActivity(userId);
                }
            }
            
            // Handle Range request for video streaming
            if (rangeHeader != null && rangeHeader.startsWith("bytes=")) {
                return handleRangeRequest(videoPath, fileSize, rangeHeader, name);
            }
            
            // Full file response
            LOGGER.info("Serving full demo video: " + name + " (" + formatFileSize(fileSize) + ")");
            
            return Response.ok(videoPath.toFile())
                    .type(MP4_CONTENT_TYPE)
                    .header("Content-Length", fileSize)
                    .header("Accept-Ranges", "bytes")
                    .header("Content-Disposition", "inline; filename=\"" + name + ".mp4\"")
                    .header("Cache-Control", "public, max-age=86400") // Cache for 24 hours
                    .build();
            
        } catch (IOException e) {
            LOGGER.severe("Failed to serve demo video " + name + ": " + e.getMessage());
            return Response.status(Response.Status.INTERNAL_SERVER_ERROR)
                    .entity("Failed to serve demo video")
                    .type(MediaType.TEXT_PLAIN)
                    .build();
        }
    }

    /**
     * Handle HTTP Range requests for partial content (video seeking).
     */
    private Response handleRangeRequest(java.nio.file.Path videoPath, long fileSize, 
                                        String rangeHeader, String name) throws IOException {
        
        // Parse range header: "bytes=start-end" or "bytes=start-"
        String rangeValue = rangeHeader.substring(6); // Remove "bytes="
        String[] ranges = rangeValue.split("-");
        
        long start = 0;
        long end = fileSize - 1;
        
        try {
            if (ranges.length > 0 && !ranges[0].isEmpty()) {
                start = Long.parseLong(ranges[0]);
            }
            if (ranges.length > 1 && !ranges[1].isEmpty()) {
                end = Long.parseLong(ranges[1]);
            }
        } catch (NumberFormatException e) {
            LOGGER.warning("Invalid range header format: " + rangeHeader);
            return Response.status(Response.Status.REQUESTED_RANGE_NOT_SATISFIABLE)
                    .header("Content-Range", "bytes */" + fileSize)
                    .build();
        }
        
        // Validate range
        if (start < 0 || end >= fileSize || start > end) {
            LOGGER.warning("Invalid range requested: " + start + "-" + end + " for file size " + fileSize);
            return Response.status(Response.Status.REQUESTED_RANGE_NOT_SATISFIABLE)
                    .header("Content-Range", "bytes */" + fileSize)
                    .build();
        }
        
        long contentLength = end - start + 1;
        
        LOGGER.fine("Serving partial content for " + name + ": bytes " + start + "-" + end + "/" + fileSize);
        
        // Create input stream for the range
        RandomAccessFile raf = new RandomAccessFile(videoPath.toFile(), "r");
        raf.seek(start);
        
        // Create a limited input stream
        InputStream rangeStream = new RangeInputStream(raf, contentLength);
        
        return Response.status(Response.Status.PARTIAL_CONTENT)
                .entity(rangeStream)
                .type(MP4_CONTENT_TYPE)
                .header("Content-Length", contentLength)
                .header("Content-Range", "bytes " + start + "-" + end + "/" + fileSize)
                .header("Accept-Ranges", "bytes")
                .header("Content-Disposition", "inline; filename=\"" + name + ".mp4\"")
                .header("Cache-Control", "public, max-age=86400")
                .build();
    }

    /**
     * Format file size to human readable format.
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
     * Input stream that reads a specific range from a RandomAccessFile.
     */
    private static class RangeInputStream extends InputStream {
        private final RandomAccessFile raf;
        private long remaining;

        public RangeInputStream(RandomAccessFile raf, long length) {
            this.raf = raf;
            this.remaining = length;
        }

        @Override
        public int read() throws IOException {
            if (remaining <= 0) {
                return -1;
            }
            remaining--;
            return raf.read();
        }

        @Override
        public int read(byte[] b, int off, int len) throws IOException {
            if (remaining <= 0) {
                return -1;
            }
            int toRead = (int) Math.min(len, remaining);
            int bytesRead = raf.read(b, off, toRead);
            if (bytesRead > 0) {
                remaining -= bytesRead;
            }
            return bytesRead;
        }

        @Override
        public void close() throws IOException {
            raf.close();
        }
    }
}
