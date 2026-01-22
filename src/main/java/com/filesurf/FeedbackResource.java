package com.filesurf;

import com.filesurf.model.FeedbackRecord;
import com.filesurf.model.UserRecord;
import com.filesurf.repository.UserRepository;
import com.filesurf.service.FeedbackService;
import com.fasterxml.jackson.databind.ObjectMapper;
import io.quarkus.runtime.annotations.RegisterForReflection;
import jakarta.inject.Inject;
import jakarta.ws.rs.*;
import jakarta.ws.rs.core.MediaType;
import jakarta.ws.rs.core.Response;
import org.jboss.logging.Logger;

import java.util.UUID;

/**
 * REST endpoint for handling user feedback, bug reports, and suggestions.
 *
 * Feedback is stored in SQLite database for later review by administrators.
 */
@jakarta.ws.rs.Path("/app/http/feedback")
public class FeedbackResource {

    private static final Logger LOGGER = Logger.getLogger(FeedbackResource.class.getName());

    @Inject
    UserRepository userRepository;

    @Inject
    FeedbackService feedbackService;

    private final ObjectMapper objectMapper = new ObjectMapper();

    /**
     * Feedback submission request body
     */
    @RegisterForReflection
    public static class FeedbackRequest {
        public String type;        // "bug", "suggestion", "praise"
        public String description; // Required: the feedback content
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
    public Response submitFeedback(
            FeedbackRequest request,
            @CookieParam("filesurf_userId") String userId) {
        LOGGER.info("Received feedback submission: type=" + request.type + " from userId=" + userId);

        // Validate userId from cookie
        if (userId == null || userId.isBlank()) {
            return Response.status(Response.Status.UNAUTHORIZED)
                    .entity(new FeedbackResponse(false, "User not authenticated", null))
                    .build();
        }

        // Lookup user by userId to get email
        UserRecord user = userRepository.findByUserId(userId);
        if (user == null) {
            LOGGER.warn("Feedback submission from unknown userId: " + userId);
            return Response.status(Response.Status.UNAUTHORIZED)
                    .entity(new FeedbackResponse(false, "User not found", null))
                    .build();
        }

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

        try {
            // Serialize environment info to JSON string
            String environmentJson = null;
            if (request.environment != null) {
                environmentJson = objectMapper.writeValueAsString(request.environment);
            }

            // Save feedback to database
            FeedbackRecord record = feedbackService.createFeedback(
                    feedbackId,
                    request.type,
                    request.description,
                    userId,
                    user.getEmail(),
                    request.errorDetails,
                    environmentJson
            );

            if (record != null) {
                LOGGER.info("Feedback saved successfully: " + feedbackId);
                return Response.ok(new FeedbackResponse(true, "Feedback received successfully", feedbackId))
                        .build();
            } else {
                LOGGER.error("Failed to save feedback: record is null");
                return Response.status(Response.Status.INTERNAL_SERVER_ERROR)
                        .entity(new FeedbackResponse(false, "Failed to save feedback", feedbackId))
                        .build();
            }

        } catch (Exception e) {
            LOGGER.error("Failed to save feedback: " + e.getMessage(), e);
            return Response.status(Response.Status.INTERNAL_SERVER_ERROR)
                    .entity(new FeedbackResponse(false, "Failed to save feedback: " + e.getMessage(), feedbackId))
                    .build();
        }
    }
}
