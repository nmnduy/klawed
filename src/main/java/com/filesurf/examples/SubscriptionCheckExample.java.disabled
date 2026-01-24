package com.filesurf.examples;

import com.filesurf.stripe.SubscriptionService;
import jakarta.inject.Inject;
import jakarta.ws.rs.*;
import jakarta.ws.rs.core.MediaType;
import jakarta.ws.rs.core.Response;
import org.jboss.logging.Logger;

import java.util.Map;

/**
 * Example showing how to integrate subscription checking into your AI request handlers.
 * This is a reference implementation - adapt it to your actual endpoints.
 */
@Path("/example")
@Produces(MediaType.APPLICATION_JSON)
@Consumes(MediaType.APPLICATION_JSON)
public class SubscriptionCheckExample {

    private static final Logger LOG = Logger.getLogger(SubscriptionCheckExample.class);

    @Inject
    SubscriptionService subscriptionService;

    /**
     * Example: Heavy AI model request with subscription checking
     */
    @POST
    @Path("/heavy-model-request")
    public Response heavyModelRequest(
            Map<String, String> request,
            @CookieParam("filesurf_userId") String userId) {

        if (userId == null) {
            return Response.status(Response.Status.UNAUTHORIZED)
                    .entity(Map.of("error", "Authentication required"))
                    .build();
        }

        // Check if user can make a heavy model request
        boolean canUseHeavyModel = subscriptionService.checkLimit(
                userId,
                "heavy_model_limit",
                1
        );

        if (!canUseHeavyModel) {
            // User has exceeded their heavy model limit
            LOG.infof("User %s exceeded heavy model limit, suggesting upgrade", userId);

            // Option 1: Return error with upgrade prompt
            return Response.status(429)  // Too Many Requests
                    .entity(Map.of(
                            "error", "Heavy model limit exceeded",
                            "message", "You've reached your monthly limit for heavy AI models",
                            "suggestion", "Upgrade to Professional for higher limits",
                            "upgradeUrl", "/pricing",
                            "fallbackAvailable", true
                    ))
                    .build();

            // Option 2: Automatically fallback to Cerebras (faster, lighter model)
            // return handleCerebrasRequest(request, userId);
        }

        // Proceed with heavy model request
        try {
            String response = processHeavyModelRequest(request);

            // Increment usage counter after successful request
            subscriptionService.incrementUsage(userId, "heavy_model_requests", 1);

            LOG.infof("Heavy model request completed for user %s", userId);

            return Response.ok(Map.of(
                    "response", response,
                    "model", "heavy",
                    "remainingRequests", getRemainingRequests(userId)
            )).build();

        } catch (Exception e) {
            LOG.error("Error processing heavy model request", e);
            return Response.status(Response.Status.INTERNAL_SERVER_ERROR)
                    .entity(Map.of("error", "Failed to process request"))
                    .build();
        }
    }

    /**
     * Example: Cerebras request (faster fallback, no limit checking needed)
     */
    @POST
    @Path("/cerebras-request")
    public Response cerebrasRequest(
            Map<String, String> request,
            @CookieParam("filesurf_userId") String userId) {

        if (userId == null) {
            return Response.status(Response.Status.UNAUTHORIZED)
                    .entity(Map.of("error", "Authentication required"))
                    .build();
        }

        try {
            String response = processCerebrasRequest(request);

            // Track Cerebras usage (optional, for analytics)
            subscriptionService.incrementUsage(userId, "cerebras_requests", 1);

            return Response.ok(Map.of(
                    "response", response,
                    "model", "cerebras"
            )).build();

        } catch (Exception e) {
            LOG.error("Error processing Cerebras request", e);
            return Response.status(Response.Status.INTERNAL_SERVER_ERROR)
                    .entity(Map.of("error", "Failed to process request"))
                    .build();
        }
    }

    /**
     * Example: File upload with storage limit checking
     */
    @POST
    @Path("/upload-file")
    public Response uploadFile(
            @FormParam("file") byte[] fileData,
            @CookieParam("filesurf_userId") String userId) {

        if (userId == null) {
            return Response.status(Response.Status.UNAUTHORIZED)
                    .entity(Map.of("error", "Authentication required"))
                    .build();
        }

        long fileSize = fileData.length;

        // Check storage limit
        boolean canUpload = subscriptionService.checkLimit(
                userId,
                "storage_bytes",
                (int) fileSize
        );

        if (!canUpload) {
            // Get current usage
            Map<String, Integer> usage = subscriptionService.getCurrentUsage(userId);
            int currentStorageBytes = usage.getOrDefault("storage_bytes", 0);
            double currentStorageGB = currentStorageBytes / 1_073_741_824.0;

            // Get plan limits
            Map<String, Object> limits = subscriptionService.getPlanLimits(userId);
            int storageLimitGB = (int) limits.getOrDefault("storage_gb", 1);

            LOG.infof("User %s exceeded storage limit: %.2f GB / %d GB",
                    userId, currentStorageGB, storageLimitGB);

            return Response.status(413)  // Payload Too Large
                    .entity(Map.of(
                            "error", "Storage limit exceeded",
                            "currentUsageGB", String.format("%.2f", currentStorageGB),
                            "limitGB", storageLimitGB,
                            "message", "You've reached your storage limit",
                            "suggestion", "Upgrade your plan for more storage",
                            "upgradeUrl", "/pricing"
                    ))
                    .build();
        }

        // Proceed with upload
        try {
            String fileId = saveFile(fileData);

            // Increment storage usage
            subscriptionService.incrementUsage(userId, "storage_bytes", (int) fileSize);

            return Response.ok(Map.of(
                    "fileId", fileId,
                    "size", fileSize,
                    "remainingStorageGB", getRemainingStorage(userId)
            )).build();

        } catch (Exception e) {
            LOG.error("Error uploading file", e);
            return Response.status(Response.Status.INTERNAL_SERVER_ERROR)
                    .entity(Map.of("error", "Failed to upload file"))
                    .build();
        }
    }

    /**
     * Example: Get user's subscription status and usage
     */
    @GET
    @Path("/subscription-status")
    public Response getSubscriptionStatus(@CookieParam("filesurf_userId") String userId) {

        if (userId == null) {
            return Response.status(Response.Status.UNAUTHORIZED)
                    .entity(Map.of("error", "Authentication required"))
                    .build();
        }

        // Get plan
        String planCode = subscriptionService.getUserPlan(userId);
        String planName = planCode != null ? planCode : "free";

        // Get limits
        Map<String, Object> limits = subscriptionService.getPlanLimits(userId);

        // Get current usage
        Map<String, Integer> usage = subscriptionService.getCurrentUsage(userId);

        // Calculate remaining
        int heavyModelLimit = (int) limits.getOrDefault("heavy_model_limit", 10);
        int heavyModelUsed = usage.getOrDefault("heavy_model_requests", 0);
        int heavyModelRemaining = Math.max(0, heavyModelLimit - heavyModelUsed);

        int storageLimit = (int) limits.getOrDefault("storage_gb", 1);
        int storageBytesUsed = usage.getOrDefault("storage_bytes", 0);
        double storageGBUsed = storageBytesUsed / 1_073_741_824.0;
        double storageGBRemaining = Math.max(0, storageLimit - storageGBUsed);

        return Response.ok(Map.of(
                "plan", planName,
                "heavyModels", Map.of(
                        "limit", heavyModelLimit,
                        "used", heavyModelUsed,
                        "remaining", heavyModelRemaining
                ),
                "storage", Map.of(
                        "limitGB", storageLimit,
                        "usedGB", String.format("%.2f", storageGBUsed),
                        "remainingGB", String.format("%.2f", storageGBRemaining)
                ),
                "features", limits
        )).build();
    }

    // Placeholder methods - replace with actual implementation

    private String processHeavyModelRequest(Map<String, String> request) {
        // TODO: Implement actual heavy model processing
        return "Heavy model response";
    }

    private String processCerebrasRequest(Map<String, String> request) {
        // TODO: Implement actual Cerebras processing
        return "Cerebras response";
    }

    private String saveFile(byte[] fileData) {
        // TODO: Implement actual file saving
        return "file-" + System.currentTimeMillis();
    }

    private int getRemainingRequests(String userId) {
        Map<String, Object> limits = subscriptionService.getPlanLimits(userId);
        Map<String, Integer> usage = subscriptionService.getCurrentUsage(userId);

        int limit = (int) limits.getOrDefault("heavy_model_limit", 10);
        int used = usage.getOrDefault("heavy_model_requests", 0);

        return Math.max(0, limit - used);
    }

    private String getRemainingStorage(String userId) {
        Map<String, Object> limits = subscriptionService.getPlanLimits(userId);
        Map<String, Integer> usage = subscriptionService.getCurrentUsage(userId);

        int limitGB = (int) limits.getOrDefault("storage_gb", 1);
        int usedBytes = usage.getOrDefault("storage_bytes", 0);
        double usedGB = usedBytes / 1_073_741_824.0;

        double remainingGB = Math.max(0, limitGB - usedGB);
        return String.format("%.2f GB", remainingGB);
    }
}
