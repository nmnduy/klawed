package com.filesurf;

import com.filesurf.service.SessionManager;
import com.fasterxml.jackson.databind.ObjectMapper;
import io.quarkus.runtime.annotations.RegisterForReflection;
import jakarta.inject.Inject;
import jakarta.ws.rs.*;
import jakarta.ws.rs.core.MediaType;
import jakarta.ws.rs.core.Response;
import org.jboss.logging.Logger;

import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.StandardOpenOption;
import java.time.LocalDateTime;
import java.time.format.DateTimeFormatter;
import java.util.UUID;

/**
 * REST endpoint for handling user feedback, bug reports, and suggestions.
 *
 * Feedback is stored in a JSON file for later review by administrators.
 */
@jakarta.ws.rs.Path("/file-chat/http/feedback")
public class FeedbackResource {

    private static final Logger LOGGER = Logger.getLogger(FeedbackResource.class.getName());
    private static final String FEEDBACK_DIR = "data/feedback";

    @Inject
    SessionManager sessionManager;

    private final ObjectMapper objectMapper = new ObjectMapper();

    /**
     * Feedback submission request body
     */
    @RegisterForReflection
    public static class FeedbackRequest {
        public String type;        // "bug", "suggestion", "praise"
        public String description; // Required: the feedback content
        public String email;       // Optional: user's email for response
        public String errorDetails; // Optional: error logs, stack traces
        public EnvironmentInfo environment; // Optional: browser/OS info

        public FeedbackRequest() {
        }
    }

    @RegisterForReflection
    public static class EnvironmentInfo {
        public String userAgent;
        public String language;
        public String platform;
        public String screenSize;
        public String viewportSize;
        public String timestamp;
        public String url;
        public String sessionId;

        public EnvironmentInfo() {
        }
    }

    /**
     * Feedback submission response
     */
    @RegisterForReflection
    public static class FeedbackResponse {
        public boolean success;
        public String message;
        public String feedbackId;

        public FeedbackResponse() {
        }

        public FeedbackResponse(boolean success, String message, String feedbackId) {
            this.success = success;
            this.message = message;
            this.feedbackId = feedbackId;
        }
    }

    @POST
    @Consumes(MediaType.APPLICATION_JSON)
    @Produces(MediaType.APPLICATION_JSON)
    public Response submitFeedback(FeedbackRequest request) {
        LOGGER.info("Received feedback submission: type=" + request.type);

        // Validate request
        if (request.type == null || request.type.isBlank()) {
            return Response.status(Response.Status.BAD_REQUEST)
                    .entity(new FeedbackResponse(false, "Feedback type is required", null))
                    .build();
        }

        if (!request.type.equals("bug") && !request.type.equals("suggestion") && !request.type.equals("praise")) {
            return Response.status(Response.Status.BAD_REQUEST)
                    .entity(new FeedbackResponse(false, "Invalid feedback type. Must be: bug, suggestion, or praise", null))
                    .build();
        }

        if (request.description == null || request.description.isBlank()) {
            return Response.status(Response.Status.BAD_REQUEST)
                    .entity(new FeedbackResponse(false, "Description is required", null))
                    .build();
        }

        // Generate feedback ID
        String feedbackId = UUID.randomUUID().toString();

        // Create feedback record
        FeedbackRecord record = new FeedbackRecord();
        record.id = feedbackId;
        record.type = request.type;
        record.description = request.description;
        record.email = request.email != null && !request.email.isBlank() ? request.email : null;
        record.errorDetails = request.errorDetails != null && !request.errorDetails.isBlank() ? request.errorDetails : null;
        record.environment = request.environment;
        record.timestamp = LocalDateTime.now().format(DateTimeFormatter.ISO_LOCAL_DATE_TIME);

        try {
            // Save feedback to file
            saveFeedback(record);
            LOGGER.info("Feedback saved successfully: " + feedbackId);

            return Response.ok(new FeedbackResponse(true, "Feedback received successfully", feedbackId))
                    .build();

        } catch (IOException e) {
            LOGGER.error("Failed to save feedback: " + e.getMessage(), e);
            return Response.status(Response.Status.INTERNAL_SERVER_ERROR)
                    .entity(new FeedbackResponse(false, "Failed to save feedback: " + e.getMessage(), feedbackId))
                    .build();
        }
    }

    /**
     * Save feedback record to JSON file
     */
    private void saveFeedback(FeedbackRecord record) throws IOException {
        Path feedbackPath = Path.of(FEEDBACK_DIR);

        // Create directory if it doesn't exist
        if (!Files.exists(feedbackPath)) {
            Files.createDirectories(feedbackPath);
        }

        // Create individual feedback file
        Path filePath = feedbackPath.resolve(record.id + ".json");
        String json = objectMapper.writerWithDefaultPrettyPrinter().writeValueAsString(record);
        Files.writeString(filePath, json, StandardOpenOption.CREATE, StandardOpenOption.TRUNCATE_EXISTING);

        // Also append to a daily log for easy review
        String dateSuffix = LocalDateTime.now().format(DateTimeFormatter.ofPattern("yyyy-MM-dd"));
        Path dailyLogPath = feedbackPath.resolve("feedback-" + dateSuffix + ".log");
        String logEntry = objectMapper.writeValueAsString(record) + "\n---\n";
        Files.writeString(dailyLogPath, logEntry, StandardOpenOption.CREATE, StandardOpenOption.APPEND);
    }

    /**
     * Feedback record data structure
     */
    @RegisterForReflection
    public static class FeedbackRecord {
        public String id;
        public String type;
        public String description;
        public String email;
        public String errorDetails;
        public EnvironmentInfo environment;
        public String timestamp;
    }
}
