package com.filesurf.filter;

import com.filesurf.model.UserRecord;
import com.filesurf.service.UserService;
import jakarta.annotation.Priority;
import jakarta.inject.Inject;
import jakarta.ws.rs.Priorities;
import jakarta.ws.rs.container.ContainerRequestContext;
import jakarta.ws.rs.container.ContainerRequestFilter;
import jakarta.ws.rs.core.Cookie;
import jakarta.ws.rs.core.Response;
import jakarta.ws.rs.ext.Provider;

import java.io.IOException;
import java.net.URI;
import java.net.URLEncoder;
import java.nio.charset.StandardCharsets;
import java.util.logging.Logger;

/**
 * Authentication filter that ensures users have a valid email-linked userId.
 * Protects all endpoints except auth endpoints, static assets, and health checks.
 */
@Provider
@Priority(Priorities.AUTHENTICATION)
public class AuthenticationFilter implements ContainerRequestFilter {

    private static final Logger LOGGER = Logger.getLogger(AuthenticationFilter.class.getName());
    private static final String USER_COOKIE_NAME = "filesurf_userId";
    private static final String DEFAULT_REDIRECT_PATH = "/file-chat";

    @Inject
    UserService userService;

    @Override
    public void filter(ContainerRequestContext requestContext) throws IOException {
        String path = requestContext.getUriInfo().getPath();
        String method = requestContext.getMethod();

        // Skip authentication for certain paths
        if (shouldSkipAuthentication(path)) {
            return;
        }

        // Get userId from cookie
        Cookie userCookie = requestContext.getCookies().get(USER_COOKIE_NAME);
        String userId = userCookie != null ? userCookie.getValue() : null;

        if (userId == null || userId.isBlank()) {
            LOGGER.info("No userId cookie found, redirecting to login for path: " + path);
            handleUnauthenticated(requestContext, path);
            return;
        }

        // Check if userId is linked to an email
        UserRecord user = userService.getUserByUserId(userId);
        if (user == null) {
            LOGGER.info("UserId not found in database: " + userId + ", redirecting to login for path: " + path);
            handleUnauthenticated(requestContext, path);
            return;
        }

        // User is authenticated
        LOGGER.fine("Authenticated user: " + user.getEmail() + " accessing: " + path);
    }

    /**
     * Determines if authentication should be skipped for the given path.
     * Public for testing purposes.
     */
    public boolean shouldSkipAuthentication(String path) {
        if (path == null) {
            return false;
        }

        // Normalize path by removing leading slash for comparison
        String normalizedPath = path.startsWith("/") ? path.substring(1) : path;

        // Skip auth endpoints
        if (normalizedPath.startsWith("auth/") || normalizedPath.equals("auth")) {
            return true;
        }

        // Skip static assets
        if (normalizedPath.startsWith("assets/") || 
            normalizedPath.startsWith("js/") || 
            normalizedPath.startsWith("css/")) {
            return true;
        }

        // Skip files by extension
        if (path.endsWith(".css") || path.endsWith(".js") ||
            path.endsWith(".png") || path.endsWith(".jpg") || path.endsWith(".jpeg") ||
            path.endsWith(".gif") || path.endsWith(".svg") || path.endsWith(".ico") ||
            path.endsWith(".woff") || path.endsWith(".woff2")) {
            return true;
        }

        // Skip health/metrics endpoints
        if (normalizedPath.equals("health") || 
            normalizedPath.equals("health/live") || 
            normalizedPath.equals("health/ready") ||
            normalizedPath.equals("metrics") ||
            normalizedPath.startsWith("q/")) {  // Quarkus dev endpoints
            return true;
        }

        return false;
    }

    private void handleUnauthenticated(ContainerRequestContext requestContext, String path) {
        if (isApiRequest(requestContext, path)) {
            requestContext.abortWith(
                Response.status(Response.Status.UNAUTHORIZED)
                    .entity("{\"error\": \"Authentication required\", \"redirect\": \"/auth/login\"}")
                    .type("application/json")
                    .build()
            );
        } else {
            // For browser requests, redirect to login page
            String targetPath = normalizeRedirectPath(path);
            String redirectUrl = buildLoginRedirectUrl(targetPath);
            requestContext.abortWith(
                Response.seeOther(URI.create(redirectUrl))
                    .build()
            );
        }
    }

    /**
     * Determines if the request is an API request (should get JSON 401 response).
     * Public for testing purposes.
     */
    public boolean isApiRequest(ContainerRequestContext requestContext, String path) {
        String accept = requestContext.getHeaderString("Accept");
        String contentType = requestContext.getHeaderString("Content-Type");
        
        // Check headers
        if ((accept != null && accept.contains("application/json")) ||
            (contentType != null && contentType.contains("application/json"))) {
            return true;
        }

        // Check path patterns
        if (path == null) {
            return false;
        }
        
        String normalizedPath = path.startsWith("/") ? path.substring(1) : path;
        return normalizedPath.startsWith("file-chat/http/") ||
               normalizedPath.startsWith("session/") ||
               normalizedPath.startsWith("file-chat/upload") ||
               normalizedPath.startsWith("file-chat/explorer/");
    }

    /**
     * Normalizes the redirect path to ensure it's a valid target.
     * Handles empty paths, root paths, and paths with/without leading slashes.
     * Public for testing purposes.
     */
    public String normalizeRedirectPath(String path) {
        if (path == null || path.isEmpty() || path.equals("/")) {
            return DEFAULT_REDIRECT_PATH;
        }

        // Ensure path starts with a single slash
        if (path.startsWith("/")) {
            return path;
        }
        
        return "/" + path;
    }

    /**
     * Builds the login redirect URL with properly encoded redirect parameter.
     * Public for testing purposes.
     */
    public String buildLoginRedirectUrl(String targetPath) {
        String encodedPath = encodeRedirect(targetPath);
        return "/auth/login?redirect=" + encodedPath;
    }

    /**
     * URL-encodes the redirect path.
     * Public for testing purposes.
     */
    public String encodeRedirect(String url) {
        if (url == null) {
            return "";
        }
        try {
            return URLEncoder.encode(url, StandardCharsets.UTF_8);
        } catch (Exception e) {
            LOGGER.warning("Failed to encode redirect URL: " + url + ", error: " + e.getMessage());
            return url;
        }
    }
}
