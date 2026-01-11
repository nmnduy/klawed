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
import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.StandardCopyOption;
import java.util.ArrayList;
import java.util.List;
import java.util.logging.Logger;

@jakarta.ws.rs.Path("/file-chat/upload")
public class FileUploadResource {

    private static final Logger LOGGER = Logger.getLogger(FileUploadResource.class.getName());
    private static final long MAX_FILE_SIZE = 100 * 1024 * 1024; // 100 MB

    @Inject
    SessionManager sessionManager;

    @Inject
    KlawedAgentManager agentManager;

    private String resolveUserId(String headerUserId, String cookieUserId) {
        if (headerUserId != null && !headerUserId.isBlank()) {
            return headerUserId;
        }
        if (cookieUserId != null && !cookieUserId.isBlank()) {
            return cookieUserId;
        }
        return null;
    }
    
    public static class UploadForm {
        @FormParam("files")
        public List<FileUpload> files;
    }
    
    public static class UploadResponse {
        public int count;
        public List<String> files;
        public String message;
        
        public UploadResponse(int count, List<String> files, String message) {
            this.count = count;
            this.files = files;
            this.message = message;
        }
    }

    @POST
    @Consumes(MediaType.MULTIPART_FORM_DATA)
    @Produces(MediaType.APPLICATION_JSON)
    public Response uploadFiles(@MultipartForm UploadForm form,
                                @HeaderParam("X-Session-ID") String sessionId,
                                @HeaderParam("X-User-ID") String headerUserId,
                                @CookieParam("filesurf_userId") String cookieUserId) {
        LOGGER.info("Received file upload request for session: " + sessionId);

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

        if (form.files == null || form.files.isEmpty()) {
            return Response.status(Response.Status.BAD_REQUEST)
                    .entity("{\"error\": \"No files provided\"}")
                    .build();
        }

        // Get session-specific upload directory
        java.nio.file.Path uploadPath;
        try {
            uploadPath = sessionManager.getUploadsDirectory(sessionId, userId);
            LOGGER.info("Using upload directory: " + uploadPath);
        } catch (IOException e) {
            LOGGER.severe("Failed to get session upload directory: " + e.getMessage());
            return Response.status(Response.Status.INTERNAL_SERVER_ERROR)
                    .entity("{\"error\": \"Failed to access upload directory: " + e.getMessage() + "\"}")
                    .build();
        }

        List<String> uploadedFiles = new ArrayList<>();
        List<String> errors = new ArrayList<>();

        for (FileUpload fileUpload : form.files) {
            try {
                // Validate file size
                if (fileUpload.size() > MAX_FILE_SIZE) {
                    errors.add(fileUpload.fileName() + " exceeds maximum size of 10 MB");
                    continue;
                }

                // Generate safe file name to prevent path traversal
                String fileName = fileUpload.fileName();
                String safeFileName = sanitizeFileName(fileName, uploadPath);
                java.nio.file.Path targetPath = uploadPath.resolve(safeFileName);

                // Copy file to upload directory
                Files.copy(fileUpload.filePath(), targetPath, StandardCopyOption.REPLACE_EXISTING);
                
                uploadedFiles.add(safeFileName);
                LOGGER.info("Successfully uploaded file: " + safeFileName + " (" + fileUpload.size() + " bytes)");
                
            } catch (IOException e) {
                LOGGER.severe("Failed to upload file " + fileUpload.fileName() + ": " + e.getMessage());
                errors.add(fileUpload.fileName() + ": " + e.getMessage());
            }
        }

        if (uploadedFiles.isEmpty()) {
            String errorMessage = errors.isEmpty() ? "No files were uploaded" : String.join(", ", errors);
            return Response.status(Response.Status.BAD_REQUEST)
                    .entity("{\"error\": \"" + errorMessage + "\"}")
                    .build();
        }

        String message = uploadedFiles.size() == form.files.size() 
                ? "All files uploaded successfully" 
                : "Some files uploaded successfully. Errors: " + String.join(", ", errors);

        UploadResponse response = new UploadResponse(uploadedFiles.size(), uploadedFiles, message);
        
        // Notify klawed agent about the uploaded files
        notifyKlawedAboutUpload(sessionId, uploadedFiles);
        
        return Response.ok(response).build();
    }

    private String sanitizeFileName(String fileName, java.nio.file.Path uploadDir) {
        // Remove path separators and other dangerous characters
        String sanitized = fileName.replaceAll("[^a-zA-Z0-9._-]", "_");
        
        // Ensure unique filename by adding timestamp if needed
        String baseName = sanitized.substring(0, sanitized.lastIndexOf('.') >= 0 ? sanitized.lastIndexOf('.') : sanitized.length());
        String extension = sanitized.lastIndexOf('.') >= 0 ? sanitized.substring(sanitized.lastIndexOf('.')) : "";
        
        java.nio.file.Path targetPath = uploadDir.resolve(sanitized);
        if (Files.exists(targetPath)) {
            sanitized = baseName + "_" + System.currentTimeMillis() + extension;
        }
        
        return sanitized;
    }

    @GET
    @jakarta.ws.rs.Path("/list")
    @Produces(MediaType.APPLICATION_JSON)
    public Response listUploadedFiles(@HeaderParam("X-Session-ID") String sessionId,
                                      @HeaderParam("X-User-ID") String headerUserId,
                                      @CookieParam("filesurf_userId") String cookieUserId) {
        LOGGER.info("Listing uploaded files for session: " + sessionId);

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
            java.nio.file.Path uploadPath = sessionManager.getUploadsDirectory(sessionId, userId);

            if (!Files.exists(uploadPath)) {
                return Response.ok("{\"files\": [], \"count\": 0}").build();
            }

            List<String> files = Files.list(uploadPath)
                    .filter(Files::isRegularFile)
                    .map(path -> path.getFileName().toString())
                    .toList();
            
            return Response.ok()
                    .entity("{\"files\": [\"" + String.join("\",\"", files) + "\"], \"count\": " + files.size() + "}")
                    .build();
        } catch (IOException e) {
            LOGGER.severe("Failed to list files: " + e.getMessage());
            return Response.status(Response.Status.INTERNAL_SERVER_ERROR)
                    .entity("{\"error\": \"Failed to list files\"}")
                    .build();
        }
    }

    /**
     * Notify klawed agent about uploaded files
     */
    private void notifyKlawedAboutUpload(String sessionId, List<String> uploadedFiles) {
        if (uploadedFiles == null || uploadedFiles.isEmpty()) {
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
            String fileList = String.join(", ", uploadedFiles);
            String notificationMessage;
            
            if (uploadedFiles.size() == 1) {
                notificationMessage = "User has uploaded a file: " + uploadedFiles.get(0);
            } else {
                notificationMessage = "User has uploaded " + uploadedFiles.size() + " files: " + fileList;
            }

            LOGGER.info("[SESSION:" + sessionId + "] Notifying klawed about uploaded files: " + fileList);
            
            // Send the notification asynchronously to avoid blocking the upload response
            agent.sendMessageAsync(notificationMessage);
            
            LOGGER.info("[SESSION:" + sessionId + "] File upload notification sent to klawed");
            
        } catch (Exception e) {
            // Log but don't fail the upload if notification fails
            LOGGER.warning("[SESSION:" + sessionId + "] Failed to notify klawed about file upload: " + e.getMessage());
        }
    }
}
