package com.filesurf;

import com.filesurf.model.UserRecord;
import com.filesurf.service.UserService;
import io.quarkus.qute.Template;
import io.quarkus.qute.TemplateInstance;
import jakarta.inject.Inject;
import jakarta.ws.rs.*;
import jakarta.ws.rs.core.*;
import org.eclipse.microprofile.config.inject.ConfigProperty;

import java.net.URI;
import java.time.Duration;
import java.util.Optional;
import java.util.logging.Logger;

/**
 * REST endpoint for user authentication.
 * Handles login by email - creates new users or returns existing ones.
 */
@Path("/auth")
public class UserAuthResource {

    private static final Logger LOGGER = Logger.getLogger(UserAuthResource.class.getName());
    private static final String USER_COOKIE_NAME = "filesurf_userId";

    @Inject
    UserService userService;

    @Inject
    Template login;

    @ConfigProperty(name = "cookie.secure", defaultValue = "false")
    Optional<Boolean> cookieSecure;

    /**
     * Login or register endpoint.
     * Takes an email, finds or creates the user, and sets the userId cookie.
     */
    @POST
    @Path("/login")
    @Consumes(MediaType.APPLICATION_FORM_URLENCODED)
    @Produces(MediaType.APPLICATION_JSON)
    public Response login(@FormParam("email") String email,
                          @FormParam("redirect") String redirect) {
        LOGGER.info("Login attempt with email: " + (email != null ? email : "null"));

        // Validate email
        if (email == null || email.trim().isEmpty()) {
            return Response.status(Response.Status.BAD_REQUEST)
                    .entity("{\"error\": \"Email is required\"}")
                    .build();
        }

        if (!userService.isValidEmail(email)) {
            return Response.status(Response.Status.BAD_REQUEST)
                    .entity("{\"error\": \"Invalid email format\"}")
                    .build();
        }

        try {
            // Check if user exists (invite-only - no auto-registration)
            UserRecord user = userService.getUserByEmail(email);

            if (user == null) {
                LOGGER.warning("Login attempt with non-invited email: " + email);
                return Response.status(Response.Status.FORBIDDEN)
                        .entity("{\"error\": \"Access denied. This application is invite-only. Please contact the administrator.\"}")
                        .build();
            }

            if (!user.isActive()) {
                LOGGER.warning("Login attempt with deactivated account: " + email);
                return Response.status(Response.Status.FORBIDDEN)
                        .entity("{\"error\": \"Your account has been deactivated. Please contact the administrator.\"}")
                        .build();
            }

            // Update last login timestamp
            userService.updateLastLogin(user.getUserId());

            LOGGER.info("Login successful for email: " + email + " with userId: " + user.getUserId());

            // Create cookie with 365 day expiration
            boolean secure = cookieSecure.orElse(false);
            NewCookie userCookie = new NewCookie(
                    USER_COOKIE_NAME,
                    user.getUserId(),
                    "/",
                    null,
                    "User identifier",
                    (int) Duration.ofDays(365).getSeconds(),
                    secure,  // secure: configurable, defaults to false for HTTP in development
                    true     // httpOnly: true for security
            );

            // If redirect param is provided, redirect there
            if (redirect != null && !redirect.isEmpty()) {
                LOGGER.info("Redirecting to: " + redirect);
                // Ensure redirect is a relative path starting with /
                String redirectPath = redirect.startsWith("/") ? redirect : "/" + redirect;
                return Response.seeOther(URI.create(redirectPath))
                        .cookie(userCookie)
                        .build();
            }

            // Return JSON response
            return Response.ok()
                    .cookie(userCookie)
                    .entity("{\"success\": true, \"userId\": \"" + user.getUserId() + "\", \"email\": \"" + user.getEmail() + "\"}")
                    .build();

        } catch (Exception e) {
            LOGGER.severe("Login failed: " + e.getMessage());
            return Response.status(Response.Status.INTERNAL_SERVER_ERROR)
                    .entity("{\"error\": \"Login failed: " + e.getMessage() + "\"}")
                    .build();
        }
    }

    /**
     * JSON login endpoint for AJAX requests.
     */
    @POST
    @Path("/login")
    @Consumes(MediaType.APPLICATION_JSON)
    @Produces(MediaType.APPLICATION_JSON)
    public Response loginJson(LoginRequest request) {
        return login(request != null ? request.email : null, null);
    }

    /**
     * Check if current user is authenticated (has email linked to userId)
     */
    @GET
    @Path("/status")
    @Produces(MediaType.APPLICATION_JSON)
    public Response getAuthStatus(@Context HttpHeaders headers) {
        String userId = extractUserIdFromCookies(headers);

        if (userId == null || userId.isBlank()) {
            return Response.ok()
                    .entity("{\"authenticated\": false, \"reason\": \"no_cookie\"}")
                    .build();
        }

        UserRecord user = userService.getUserByUserId(userId);
        if (user == null) {
            return Response.ok()
                    .entity("{\"authenticated\": false, \"reason\": \"no_user\", \"userId\": \"" + userId + "\"}")
                    .build();
        }

        return Response.ok()
                .entity("{\"authenticated\": true, \"userId\": \"" + user.getUserId() + "\", \"email\": \"" + user.getEmail() + "\"}")
                .build();
    }

    /**
     * Logout endpoint - clears the userId cookie
     */
    @POST
    @Path("/logout")
    @Produces(MediaType.APPLICATION_JSON)
    public Response logout(@QueryParam("redirect") String redirect) {
        LOGGER.info("Logout request received");

        // Create an expired cookie to clear the userId
        boolean secure = cookieSecure.orElse(false);
        NewCookie expiredCookie = new NewCookie(
                USER_COOKIE_NAME,
                "",
                "/",
                null,
                "User identifier",
                0,  // maxAge = 0 to delete cookie
                secure,
                true
        );

        // If redirect param is provided, redirect there
        if (redirect != null && !redirect.isEmpty()) {
            return Response.seeOther(URI.create(redirect))
                    .cookie(expiredCookie)
                    .build();
        }

        return Response.ok()
                .cookie(expiredCookie)
                .entity("{\"success\": true, \"message\": \"Logged out successfully\"}")
                .build();
    }

    /**
     * Get login page
     */
    @GET
    @Path("/login")
    @Produces(MediaType.TEXT_HTML)
    public TemplateInstance getLoginPage(@Context HttpHeaders headers,
                                         @QueryParam("redirect") String redirect) {
        // Check if user is already authenticated
        String userId = extractUserIdFromCookies(headers);
        if (userId != null && !userId.isBlank()) {
            UserRecord user = userService.getUserByUserId(userId);
            if (user != null) {
                // Already authenticated, redirect to main app
                String redirectUrl = (redirect != null && !redirect.isEmpty()) ? redirect : "/file-chat";
                return login.data("authenticated", true).data("redirectUrl", redirectUrl);
            }
        }

        // Return login page template with default redirect if none provided
        String redirectPath = (redirect != null && !redirect.isEmpty()) ? redirect : "/file-chat";
        return login.data("authenticated", false).data("redirect", redirectPath);
    }

    private String extractUserIdFromCookies(HttpHeaders headers) {
        if (headers == null) return null;
        var cookies = headers.getCookies();
        Cookie cookie = cookies != null ? cookies.get(USER_COOKIE_NAME) : null;
        if (cookie != null && cookie.getValue() != null && !cookie.getValue().isBlank()) {
            return cookie.getValue();
        }
        return null;
    }

    /**
     * DTO for JSON login requests
     */
    public static class LoginRequest {
        public String email;
    }
}
