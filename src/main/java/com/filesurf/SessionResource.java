package com.filesurf;

import jakarta.inject.Inject;
import jakarta.ws.rs.GET;
import jakarta.ws.rs.Path;
import jakarta.ws.rs.Produces;
import jakarta.ws.rs.core.Context;
import jakarta.ws.rs.core.Cookie;
import jakarta.ws.rs.core.HttpHeaders;
import jakarta.ws.rs.core.MediaType;
import jakarta.ws.rs.core.NewCookie;
import jakarta.ws.rs.core.Response;
import org.eclipse.microprofile.config.inject.ConfigProperty;

import java.time.Duration;
import java.util.Optional;
import java.util.UUID;
import java.util.concurrent.ConcurrentHashMap;
import java.util.logging.Logger;

/**
 * REST endpoint for session management
 * Generates session IDs for WebSocket connections
 */
@Path("/session")
public class SessionResource {

    private static final Logger LOGGER = Logger.getLogger(SessionResource.class.getName());
    
    @ConfigProperty(name = "cookie.secure", defaultValue = "false")
    Optional<Boolean> cookieSecure;
    
    // In-memory session store (maps session ID to client identity)
    private static final ConcurrentHashMap<String, String> sessionStore = new ConcurrentHashMap<>();
    
    /**
     * Generate a new session ID and ensure a user ID cookie is set
     * @return JSON response with session ID
     */
    @GET
    @Path("/generate")
    @Produces(MediaType.APPLICATION_JSON)
    public Response generateSession(@Context HttpHeaders headers) {
        String sessionId = UUID.randomUUID().toString();

        // Ensure userId cookie exists or generate one
        String userId = extractUserIdFromCookies(headers);
        if (userId == null || userId.isBlank()) {
            userId = "user-" + UUID.randomUUID();
            LOGGER.info("Generated new userId cookie: " + userId);
        }

        // Store the session ID with a placeholder identity
        sessionStore.put(sessionId, "client-" + System.currentTimeMillis());

        LOGGER.info("Generated new session ID: " + sessionId + " for user: " + userId);

        boolean secure = cookieSecure.orElse(false);
        NewCookie userCookie = new NewCookie(
                "filesurf_userId",
                userId,
                "/",
                null,
                "User identifier",
                (int) Duration.ofDays(365).getSeconds(),
                secure,  // secure: configurable, defaults to false for HTTP in development
                true     // httpOnly: true for security
        );

        // Return session ID as JSON and set cookie
        return Response.ok()
                .cookie(userCookie)
                .entity("{\"sessionId\": \"" + sessionId + "\" , \"userId\": \"" + userId + "\"}")
                .build();
    }
    
    /**
     * Extract userId from cookies in headers (helper)
     */
    private String extractUserIdFromCookies(HttpHeaders headers) {
        if (headers == null) return null;
        var cookies = headers.getCookies();
        Cookie cookie = cookies != null ? cookies.get("filesurf_userId") : null;
        if (cookie != null && cookie.getValue() != null && !cookie.getValue().isBlank()) {
            return cookie.getValue();
        }
        return null;
    }

    /**
     * Validate a session ID
     * @param sessionId The session ID to validate
     * @return true if session exists, false otherwise
     */
    public static boolean validateSession(String sessionId) {
        return sessionStore.containsKey(sessionId);
    }
    
    /**
     * Get client identity for a session
     * @param sessionId The session ID
     * @return Client identity or null if session doesn't exist
     */
    public static String getClientIdentity(String sessionId) {
        return sessionStore.get(sessionId);
    }
    
    /**
     * Remove a session (e.g., when WebSocket closes)
     * @param sessionId The session ID to remove
     */
    public static void removeSession(String sessionId) {
        sessionStore.remove(sessionId);
        LOGGER.info("Removed session: " + sessionId);
    }
    
    /**
     * Get all active sessions (for debugging/monitoring)
     * @return Number of active sessions
     */
    @GET
    @Path("/count")
    @Produces(MediaType.APPLICATION_JSON)
    public Response getActiveSessionCount() {
        return Response.ok()
                .entity("{\"activeSessions\": " + sessionStore.size() + "}")
                .build();
    }
}