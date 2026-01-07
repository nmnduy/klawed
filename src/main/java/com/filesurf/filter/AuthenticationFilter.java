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

    private boolean shouldSkipAuthentication(String path) {
        // Skip auth endpoints
        if (path.startsWith("auth/") || path.equals("auth")) {
            return true;
        }

        // Skip static assets
        if (path.startsWith("assets/") || 
            path.startsWith("js/") || 
            path.startsWith("css/") ||
            path.endsWith(".css") ||
            path.endsWith(".js") ||
            path.endsWith(".png") ||
            path.endsWith(".jpg") ||
            path.endsWith(".jpeg") ||
            path.endsWith(".gif") ||
            path.endsWith(".svg") ||
            path.endsWith(".ico") ||
            path.endsWith(".woff") ||
            path.endsWith(".woff2")) {
            return true;
        }

        // Skip health/metrics endpoints
        if (path.equals("health") || 
            path.equals("health/live") || 
            path.equals("health/ready") ||
            path.equals("metrics") ||
            path.startsWith("q/")) {  // Quarkus dev endpoints
            return true;
        }

        return false;
    }

    private void handleUnauthenticated(ContainerRequestContext requestContext, String path) {
        // For API calls (JSON requests), return 401
        String accept = requestContext.getHeaderString("Accept");
        String contentType = requestContext.getHeaderString("Content-Type");
        
        boolean isApiRequest = (accept != null && accept.contains("application/json")) ||
                               (contentType != null && contentType.contains("application/json")) ||
                               path.startsWith("file-chat/http/") ||
                               path.startsWith("session/") ||
                               path.startsWith("file-chat/upload") ||
                               path.startsWith("file-chat/explorer/");

        if (isApiRequest) {
            requestContext.abortWith(
                Response.status(Response.Status.UNAUTHORIZED)
                    .entity("{\"error\": \"Authentication required\", \"redirect\": \"/auth/login\"}")
                    .type("application/json")
                    .build()
            );
        } else {
            // For browser requests, redirect to login page
            String redirectUrl = "/auth/login?redirect=" + encodeRedirect("/" + path);
            requestContext.abortWith(
                Response.seeOther(URI.create(redirectUrl))
                    .build()
            );
        }
    }

    private String encodeRedirect(String url) {
        try {
            return java.net.URLEncoder.encode(url, "UTF-8");
        } catch (Exception e) {
            return url;
        }
    }
}
