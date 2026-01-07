package com.filesurf;

import com.filesurf.model.UserRecord;
import com.filesurf.service.UserService;
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
                          @QueryParam("redirect") String redirect) {
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
                return Response.seeOther(URI.create(redirect))
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
    public Response getLoginPage(@Context HttpHeaders headers,
                                 @QueryParam("redirect") String redirect) {
        // Check if user is already authenticated
        String userId = extractUserIdFromCookies(headers);
        if (userId != null && !userId.isBlank()) {
            UserRecord user = userService.getUserByUserId(userId);
            if (user != null) {
                // Already authenticated, redirect to main app
                String redirectUrl = (redirect != null && !redirect.isEmpty()) ? redirect : "/file-chat";
                return Response.seeOther(URI.create(redirectUrl)).build();
            }
        }

        // Return login page - will be rendered by Qute
        return Response.ok()
                .entity(getLoginHtml(redirect))
                .build();
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

    private String getLoginHtml(String redirect) {
        String redirectInput = redirect != null && !redirect.isEmpty() 
            ? "<input type=\"hidden\" name=\"redirect\" value=\"" + escapeHtml(redirect) + "\" />"
            : "<input type=\"hidden\" name=\"redirect\" value=\"/file-chat\" />";

        return """
            <!DOCTYPE html>
            <html lang="en">
            <head>
                <meta charset="UTF-8">
                <meta name="viewport" content="width=device-width, initial-scale=1.0">
                <title>FileSurf - Login</title>
                <link rel="stylesheet" href="/assets/main.css">
                <style>
                    .login-gradient {
                        background: linear-gradient(135deg, 
                            hsl(var(--background)) 0%%, 
                            hsl(var(--muted)) 50%%, 
                            hsl(var(--background)) 100%%);
                    }
                    .login-card {
                        backdrop-filter: blur(10px);
                        box-shadow: 0 25px 50px -12px rgba(0, 0, 0, 0.25),
                                    0 0 0 1px rgba(255, 255, 255, 0.05);
                    }
                    .login-icon {
                        background: linear-gradient(135deg, hsl(var(--primary)) 0%%, hsl(var(--primary)/0.8) 100%%);
                        box-shadow: 0 10px 40px -10px hsl(var(--primary)/0.5);
                    }
                    .input-focus-glow:focus {
                        box-shadow: 0 0 0 3px hsl(var(--primary)/0.2);
                    }
                    .btn-glow {
                        box-shadow: 0 4px 14px 0 hsl(var(--primary)/0.4);
                    }
                    .btn-glow:hover {
                        box-shadow: 0 6px 20px 0 hsl(var(--primary)/0.5);
                        transform: translateY(-1px);
                    }
                    @keyframes float {
                        0%%, 100%% { transform: translateY(0px); }
                        50%% { transform: translateY(-10px); }
                    }
                    .float-animation {
                        animation: float 6s ease-in-out infinite;
                    }
                </style>
            </head>
            <body class="login-gradient min-h-screen flex items-center justify-center p-4 sm:p-6 md:p-8">
                <div class="w-full max-w-md">
                    <div class="login-card bg-[hsl(var(--card)/0.95)] rounded-2xl p-6 sm:p-8 md:p-10 border border-[hsl(var(--border)/0.5)]">
                        <!-- Icon -->
                        <div class="flex justify-center mb-6">
                            <div class="login-icon float-animation w-16 h-16 sm:w-20 sm:h-20 rounded-2xl flex items-center justify-center">
                                <svg class="w-8 h-8 sm:w-10 sm:h-10 text-[hsl(var(--primary-foreground))]" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                                    <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M5 19a2 2 0 01-2-2V7a2 2 0 012-2h4l2 2h4a2 2 0 012 2v1M5 19h14a2 2 0 002-2v-5a2 2 0 00-2-2H9a2 2 0 00-2 2v5a2 2 0 01-2 2z" />
                                </svg>
                            </div>
                        </div>
                        
                        <!-- Header -->
                        <div class="text-center mb-8">
                            <h1 class="text-2xl sm:text-3xl font-bold text-[hsl(var(--foreground))] mb-2">Welcome to FileSurf</h1>
                            <p class="text-[hsl(var(--muted-foreground))] text-sm sm:text-base">Enter your email to continue</p>
                        </div>
                        
                        <!-- Form -->
                        <form action="/auth/login" method="POST" id="login-form" class="space-y-6">
                            %s
                            <div>
                                <label for="email" class="block text-sm font-medium text-[hsl(var(--foreground))] mb-2">
                                    Email Address
                                </label>
                                <input 
                                    type="email" 
                                    id="email" 
                                    name="email" 
                                    required
                                    autofocus
                                    placeholder="you@example.com"
                                    class="input-focus-glow w-full px-4 py-3 sm:py-3.5 rounded-xl border border-[hsl(var(--border))] bg-[hsl(var(--background))] text-[hsl(var(--foreground))] placeholder-[hsl(var(--muted-foreground))] focus:outline-none focus:border-[hsl(var(--primary))] transition-all duration-200"
                                />
                            </div>
                            
                            <button 
                                type="submit"
                                class="btn-glow w-full py-3 sm:py-3.5 px-4 bg-[hsl(var(--primary))] hover:bg-[hsl(var(--primary)/0.9)] text-[hsl(var(--primary-foreground))] font-semibold rounded-xl transition-all duration-200 focus:outline-none focus:ring-2 focus:ring-[hsl(var(--primary))] focus:ring-offset-2 focus:ring-offset-[hsl(var(--card))]"
                            >
                                Continue with Email
                            </button>
                        </form>
                        
                        <!-- Error Message -->
                        <div id="error-message" class="hidden mt-6 p-4 rounded-xl bg-red-500/10 border border-red-500/20 text-red-400 text-sm text-center"></div>
                        
                        <!-- Footer -->
                        <div class="mt-8 pt-6 border-t border-[hsl(var(--border)/0.5)]">
                            <p class="text-center text-xs sm:text-sm text-[hsl(var(--muted-foreground))]">
                                <span class="inline-flex items-center gap-1">
                                    <svg class="w-4 h-4" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                                        <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M12 15v2m-6 4h12a2 2 0 002-2v-6a2 2 0 00-2-2H6a2 2 0 00-2 2v6a2 2 0 002 2zm10-10V7a4 4 0 00-8 0v4h8z" />
                                    </svg>
                                    Invite-only access
                                </span>
                            </p>
                        </div>
                    </div>
                    
                    <!-- Branding -->
                    <p class="text-center text-xs text-[hsl(var(--muted-foreground)/0.6)] mt-6">
                        FileSurf &copy; 2026
                    </p>
                </div>
                
                <script>
                    document.getElementById('login-form').addEventListener('submit', function(e) {
                        const email = document.getElementById('email').value.trim();
                        const errorDiv = document.getElementById('error-message');
                        
                        if (!email) {
                            e.preventDefault();
                            errorDiv.textContent = 'Please enter your email address';
                            errorDiv.classList.remove('hidden');
                            return;
                        }
                        
                        if (!email.includes('@') || !email.includes('.')) {
                            e.preventDefault();
                            errorDiv.textContent = 'Please enter a valid email address';
                            errorDiv.classList.remove('hidden');
                            return;
                        }
                        
                        errorDiv.classList.add('hidden');
                    });
                </script>
            </body>
            </html>
            """.formatted(redirectInput);
    }

    private String escapeHtml(String input) {
        if (input == null) return "";
        return input.replace("&", "&amp;")
                    .replace("<", "&lt;")
                    .replace(">", "&gt;")
                    .replace("\"", "&quot;")
                    .replace("'", "&#x27;");
    }

    /**
     * DTO for JSON login requests
     */
    public static class LoginRequest {
        public String email;
    }
}
