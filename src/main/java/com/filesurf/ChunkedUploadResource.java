package com.filesurf;

import com.filesurf.service.KlawedAgentManager;
import com.filesurf.service.SessionManager;
import jakarta.inject.Inject;
import jakarta.ws.rs.*;
import jakarta.ws.rs.core.Context;
import jakarta.ws.rs.core.Cookie;
import jakarta.ws.rs.core.HttpHeaders;
import jakarta.ws.rs.core.MediaType;
import jakarta.ws.rs.core.Response;
import org.jboss.resteasy.reactive.MultipartForm;
import org.jboss.resteasy.reactive.multipart.FileUpload;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.StandardCopyOption;
import java.security.MessageDigest;
import java.util.Map;
import java.util.UUID;
import java.util.concurrent.ConcurrentHashMap;
import java.util.logging.Logger;

/**
 * Resource for handling large file uploads via chunking.
 * Supports resumable uploads with chunk-based transfer.
 */
@Path("/file-chat/upload/chunked")
@Produces(MediaType.APPLICATION_JSON)
public class ChunkedUploadResource {

    private static final Logger LOGGER = Logger.getLogger(ChunkedUploadResource.class.getName());
    private static final long MAX_FILE_SIZE = 1024L * 1024 * 1024; // 1 GB
    private static final long CHUNK_SIZE = 5 * 1024 * 1024; // 5 MB chunks
    
    @Inject
    SessionManager sessionManager;

    @Inject
    KlawedAgentManager agentManager;

    // Track upload sessions: uploadId -> UploadSession
    private static final Map<String, UploadSession> uploadSessions = new ConcurrentHashMap<>();

    private static class UploadSession {
        String uploadId;
        String sessionId;
        String userId;
        String fileName;
        long totalSize;
        long uploadedSize;
        java.nio.file.Path tempFile;
        MessageDigest md5;
        long lastActivity;

        UploadSession(String uploadId, String sessionId, String userId, String fileName, long totalSize) throws Exception {
            this.uploadId = uploadId;
            this.sessionId = sessionId;
            this.userId = userId;
            this.fileName = fileName;
            this.totalSize = totalSize;
            this.uploadedSize = 0;
            this.tempFile = Files.createTempFile("chunked-upload-", ".tmp");
            this.md5 = MessageDigest.getInstance("MD5");
            this.lastActivity = System.currentTimeMillis();
        }

        void appendChunk(byte[] data) throws IOException {
            try (FileOutputStream fos = new FileOutputStream(tempFile.toFile(), true)) {
                fos.write(data);
            }
            md5.update(data);
            uploadedSize += data.length;
            lastActivity = System.currentTimeMillis();
        }

        boolean isComplete() {
            return uploadedSize >= totalSize;
        }

        String getMd5Hash() {
            byte[] digest = md5.digest();
            StringBuilder sb = new StringBuilder();
            for (byte b : digest) {
                sb.append(String.format("%02x", b));
            }
            return sb.toString();
        }

        void cleanup() {
            try {
                Files.deleteIfExists(tempFile);
            } catch (IOException e) {
                LOGGER.warning("Failed to cleanup temp file: " + e.getMessage());
            }
        }
    }

    public static class InitUploadRequest {
        public String sessionId;
        public String fileName;
        public long totalSize;
    }

    public static class ChunkUploadForm {
        @FormParam("uploadId")
        public String uploadId;
        
        @FormParam("chunkIndex")
        public int chunkIndex;
        
        @FormParam("chunk")
        public FileUpload chunk;
    }

    /**
     * Initialize a chunked upload session
     */
    @POST
    @Path("/init")
    @Consumes(MediaType.APPLICATION_JSON)
    public Response initUpload(InitUploadRequest request, 
                              @Context HttpHeaders headers) {
        try {
            // Extract userId from cookie
            Cookie userCookie = headers.getCookies().get("filesurf_userId");
            if (userCookie == null) {
                return Response.status(Response.Status.UNAUTHORIZED)
                    .entity("{\"error\": \"Missing authentication cookie\"}")
                    .build();
            }
            String userId = userCookie.getValue();

            // Validate request
            if (request.fileName == null || request.fileName.isBlank()) {
                return Response.status(Response.Status.BAD_REQUEST)
                    .entity("{\"error\": \"fileName is required\"}")
                    .build();
            }

            if (request.totalSize <= 0 || request.totalSize > MAX_FILE_SIZE) {
                return Response.status(Response.Status.BAD_REQUEST)
                    .entity("{\"error\": \"Invalid file size. Max: " + MAX_FILE_SIZE + " bytes\"}")
                    .build();
            }

            // Create upload session
            String uploadId = UUID.randomUUID().toString();
            UploadSession session = new UploadSession(
                uploadId, 
                request.sessionId, 
                userId, 
                request.fileName, 
                request.totalSize
            );
            
            uploadSessions.put(uploadId, session);

            LOGGER.info("Initialized chunked upload: " + uploadId + 
                       ", file=" + request.fileName + 
                       ", size=" + request.totalSize);

            return Response.ok()
                .entity("{\"uploadId\": \"" + uploadId + "\", " +
                       "\"chunkSize\": " + CHUNK_SIZE + ", " +
                       "\"totalChunks\": " + ((request.totalSize + CHUNK_SIZE - 1) / CHUNK_SIZE) + "}")
                .build();

        } catch (Exception e) {
            LOGGER.severe("Failed to initialize upload: " + e.getMessage());
            return Response.status(Response.Status.INTERNAL_SERVER_ERROR)
                .entity("{\"error\": \"" + e.getMessage() + "\"}")
                .build();
        }
    }

    /**
     * Upload a chunk
     */
    @POST
    @Path("/chunk")
    @Consumes(MediaType.MULTIPART_FORM_DATA)
    public Response uploadChunk(@MultipartForm ChunkUploadForm form,
                               @Context HttpHeaders headers) {
        try {
            // Validate upload session
            UploadSession session = uploadSessions.get(form.uploadId);
            if (session == null) {
                return Response.status(Response.Status.NOT_FOUND)
                    .entity("{\"error\": \"Upload session not found\"}")
                    .build();
            }

            // Validate user
            Cookie userCookie = headers.getCookies().get("filesurf_userId");
            if (userCookie == null || !userCookie.getValue().equals(session.userId)) {
                return Response.status(Response.Status.UNAUTHORIZED)
                    .entity("{\"error\": \"Unauthorized\"}")
                    .build();
            }

            // Read and append chunk
            byte[] chunkData = Files.readAllBytes(form.chunk.filePath());
            session.appendChunk(chunkData);

            LOGGER.info("Uploaded chunk " + form.chunkIndex + 
                       " for " + form.uploadId + 
                       " (" + session.uploadedSize + "/" + session.totalSize + " bytes)");

            // Check if upload is complete
            if (session.isComplete()) {
                return finalizeUpload(session);
            }

            return Response.ok()
                .entity("{\"uploadedSize\": " + session.uploadedSize + ", " +
                       "\"totalSize\": " + session.totalSize + ", " +
                       "\"complete\": false}")
                .build();

        } catch (Exception e) {
            LOGGER.severe("Failed to upload chunk: " + e.getMessage());
            return Response.status(Response.Status.INTERNAL_SERVER_ERROR)
                .entity("{\"error\": \"" + e.getMessage() + "\"}")
                .build();
        }
    }

    /**
     * Get upload status
     */
    @GET
    @Path("/status/{uploadId}")
    public Response getStatus(@PathParam("uploadId") String uploadId,
                             @Context HttpHeaders headers) {
        UploadSession session = uploadSessions.get(uploadId);
        if (session == null) {
            return Response.status(Response.Status.NOT_FOUND)
                .entity("{\"error\": \"Upload session not found\"}")
                .build();
        }

        // Validate user
        Cookie userCookie = headers.getCookies().get("filesurf_userId");
        if (userCookie == null || !userCookie.getValue().equals(session.userId)) {
            return Response.status(Response.Status.UNAUTHORIZED)
                .entity("{\"error\": \"Unauthorized\"}")
                .build();
        }

        return Response.ok()
            .entity("{\"uploadId\": \"" + uploadId + "\", " +
                   "\"uploadedSize\": " + session.uploadedSize + ", " +
                   "\"totalSize\": " + session.totalSize + ", " +
                   "\"complete\": " + session.isComplete() + "}")
            .build();
    }

    /**
     * Cancel an upload
     */
    @DELETE
    @Path("/{uploadId}")
    public Response cancelUpload(@PathParam("uploadId") String uploadId,
                                @Context HttpHeaders headers) {
        UploadSession session = uploadSessions.remove(uploadId);
        if (session == null) {
            return Response.status(Response.Status.NOT_FOUND)
                .entity("{\"error\": \"Upload session not found\"}")
                .build();
        }

        // Validate user
        Cookie userCookie = headers.getCookies().get("filesurf_userId");
        if (userCookie == null || !userCookie.getValue().equals(session.userId)) {
            return Response.status(Response.Status.UNAUTHORIZED)
                .entity("{\"error\": \"Unauthorized\"}")
                .build();
        }

        session.cleanup();
        LOGGER.info("Cancelled upload: " + uploadId);

        return Response.ok()
            .entity("{\"message\": \"Upload cancelled\"}")
            .build();
    }

    /**
     * Finalize the upload and move file to session directory
     */
    private Response finalizeUpload(UploadSession session) {
        try {
            // Get session directory
            java.nio.file.Path sessionDir = sessionManager.getSessionDirectory(session.sessionId, session.userId);
            if (sessionDir == null || !Files.exists(sessionDir)) {
                throw new IOException("Session directory not found");
            }

            // Sanitize filename
            String safeFileName = session.fileName.replaceAll("[^a-zA-Z0-9._-]", "_");
            java.nio.file.Path targetFile = sessionDir.resolve(safeFileName);

            // Move file to session directory
            Files.move(session.tempFile, targetFile, StandardCopyOption.REPLACE_EXISTING);

            String md5Hash = session.getMd5Hash();
            
            LOGGER.info("Completed upload: " + session.uploadId + 
                       ", file=" + safeFileName + 
                       ", size=" + session.uploadedSize + 
                       ", md5=" + md5Hash);

            // Cleanup session
            uploadSessions.remove(session.uploadId);

            // Notify klawed agent about the uploaded file
            notifyKlawedAboutUpload(session.sessionId, safeFileName);

            return Response.ok()
                .entity("{\"uploadedSize\": " + session.uploadedSize + ", " +
                       "\"totalSize\": " + session.totalSize + ", " +
                       "\"complete\": true, " +
                       "\"fileName\": \"" + safeFileName + "\", " +
                       "\"md5\": \"" + md5Hash + "\"}")
                .build();

        } catch (Exception e) {
            LOGGER.severe("Failed to finalize upload: " + e.getMessage());
            session.cleanup();
            uploadSessions.remove(session.uploadId);
            return Response.status(Response.Status.INTERNAL_SERVER_ERROR)
                .entity("{\"error\": \"" + e.getMessage() + "\"}")
                .build();
        }
    }

    /**
     * Cleanup old upload sessions (called periodically)
     */
    public static void cleanupStaleUploads() {
        long now = System.currentTimeMillis();
        long timeout = 24 * 60 * 60 * 1000; // 24 hours

        uploadSessions.entrySet().removeIf(entry -> {
            UploadSession session = entry.getValue();
            if (now - session.lastActivity > timeout) {
                LOGGER.info("Cleaning up stale upload: " + session.uploadId);
                session.cleanup();
                return true;
            }
            return false;
        });
    }

    /**
     * Notify klawed agent about uploaded file
     */
    private void notifyKlawedAboutUpload(String sessionId, String fileName) {
        if (fileName == null || fileName.isEmpty()) {
            return;
        }

        try {
            // Get the agent instance for this session
            KlawedAgentManager.KlawedAgentInstance agent = agentManager.getAgentForSession(sessionId);
            
            if (agent == null) {
                LOGGER.info("[SESSION:" + sessionId + "] No klawed agent found to notify about file upload");
                return;
            }

            // Create a notification message
            String notificationMessage = "User has uploaded a file: " + fileName;

            LOGGER.info("[SESSION:" + sessionId + "] Notifying klawed about uploaded file: " + fileName);
            
            // Send the notification asynchronously to avoid blocking the upload response
            agent.sendMessageAsync(notificationMessage);
            
            LOGGER.info("[SESSION:" + sessionId + "] File upload notification sent to klawed");
            
        } catch (Exception e) {
            // Log but don't fail the upload if notification fails
            LOGGER.warning("[SESSION:" + sessionId + "] Failed to notify klawed about file upload: " + e.getMessage());
        }
    }
}
