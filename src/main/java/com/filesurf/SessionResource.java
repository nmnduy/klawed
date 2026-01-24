package com.filesurf;

import com.filesurf.model.UserRecord;
import com.filesurf.service.KlawedSandboxService;
import com.filesurf.service.UserService;
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

    @Inject
    UserService userService;

    @Inject
    KlawedSandboxService klawedSandboxService;

    @ConfigProperty(name = "cookie.secure", defaultValue = "false")
    Optional<Boolean> cookieSecure;

    // In-memory session cache (maps session ID to client identity)
    // This is a read-through cache; the database is the source of truth
    private static final ConcurrentHashMap<String, String> sessionCache = new ConcurrentHashMap<>();

    /**
     * Generate a new session ID for authenticated users.
     * Requires the user to have a valid userId cookie linked to an email.
     * @return JSON response with session ID and user info
     */
    @GET
    @Path("/generate")
    @Produces(MediaType.APPLICATION_JSON)
    public Response generateSession(@Context HttpHeaders headers) {
        // Get userId from cookie - user must already be authenticated
        String userId = extractUserIdFromCookies(headers);
        if (userId == null || userId.isBlank()) {
            LOGGER.warning("Session generate request without userId cookie");
            return Response.status(Response.Status.UNAUTHORIZED)
                    .entity("{\"error\": \"Authentication required\", \"redirect\": \"/auth/login\"}")
                    .build();
        }

        // Verify user exists in database (has email)
        UserRecord user = userService.getUserByUserId(userId);
        if (user == null) {
            LOGGER.warning("Session generate request with unregistered userId: " + userId);
            return Response.status(Response.Status.UNAUTHORIZED)
                    .entity("{\"error\": \"Please log in with your email\", \"redirect\": \"/auth/login\"}")
                    .build();
        }

        String sessionId = UUID.randomUUID().toString();

        // Store the session ID with user identity in cache
        sessionCache.put(sessionId, user.getEmail());

        // Register session in sessions.db (source of truth) with email
        klawedSandboxService.registerSession(sessionId, userId, user.getEmail());

        LOGGER.info("Generated new session ID: " + sessionId + " for user: " + user.getEmail() + " (userId: " + userId + ")");

        // Refresh the cookie with 365 day expiration
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

        // Return session ID as JSON and refresh cookie
        return Response.ok()
                .cookie(userCookie)
                .entity("{\"sessionId\": \"" + sessionId + "\", \"userId\": \"" + userId + "\", \"email\": \"" + user.getEmail() + "\"}")
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
     * Validate a session ID using database as source of truth
     * @param sessionId The session ID to validate
     * @param klawedSandboxService Service instance to query database
     * @return true if session exists and is active, false otherwise
     */
    public static boolean validateSession(String sessionId, KlawedSandboxService klawedSandboxService) {
        // Database is the source of truth - check if session is active (not disconnected)
        return klawedSandboxService.isSessionActive(sessionId);
    }

    /**
     * Validate a session ID (legacy static method for backward compatibility)
     * NOTE: This method is deprecated and will be removed in a future version.
     * Use validateSession(sessionId, klawedSandboxService) instead.
     * @param sessionId The session ID to validate
     * @return true if session exists in cache, false otherwise
     */
    @Deprecated
    public static boolean validateSession(String sessionId) {
        // Legacy: only check cache
        // This is kept for backward compatibility but should not be used
        return sessionCache.containsKey(sessionId);
    }

    /**
     * Get client identity for a session with read-through cache
     * @param sessionId The session ID
     * @param klawedSandboxService Service instance to query database
     * @return Client identity (email) or null if session doesn't exist
     */
    public static String getClientIdentity(String sessionId, KlawedSandboxService klawedSandboxService) {
        // Try cache first
        String cached = sessionCache.get(sessionId);
        if (cached != null) {
            return cached;
        }
        
        // Cache miss - query database
        String email = klawedSandboxService.getSessionEmail(sessionId);
        if (email != null) {
            // Populate cache for future requests
            sessionCache.put(sessionId, email);
            LOGGER.info("Loaded session " + sessionId + " from database into cache");
        }
        return email;
    }

    /**
     * Get client identity for a session (legacy static method)
     * NOTE: This method is deprecated and will be removed in a future version.
     * Use getClientIdentity(sessionId, klawedSandboxService) instead.
     * @param sessionId The session ID
     * @return Client identity or null if session doesn't exist in cache
     */
    @Deprecated
    public static String getClientIdentity(String sessionId) {
        return sessionCache.get(sessionId);
    }

    /**
     * Remove a session from cache (e.g., when WebSocket closes)
     * Note: This only clears the cache. The database record is updated separately
     * by KlawedSandboxService.unregisterSession()
     * @param sessionId The session ID to remove from cache
     */
    public static void removeSession(String sessionId) {
        sessionCache.remove(sessionId);
        LOGGER.info("Removed session from cache: " + sessionId);
    }

    /**
     * Get all active sessions (for debugging/monitoring)
     * @return Number of sessions in cache (note: database is the source of truth)
     */
    @GET
    @Path("/count")
    @Produces(MediaType.APPLICATION_JSON)
    public Response getActiveSessionCount() {
        return Response.ok()
                .entity("{\"cachedSessions\": " + sessionCache.size() + "}")
                .build();
    }
}