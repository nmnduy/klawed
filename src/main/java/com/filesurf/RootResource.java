package com.filesurf;

import com.filesurf.model.UserRecord;
import com.filesurf.service.UserService;
import com.filesurf.util.CssVersionProvider;
import io.quarkus.qute.Location;
import io.quarkus.qute.Template;
import io.quarkus.qute.TemplateInstance;
import jakarta.inject.Inject;
import jakarta.ws.rs.GET;
import jakarta.ws.rs.Path;
import jakarta.ws.rs.Produces;
import jakarta.ws.rs.core.Context;
import jakarta.ws.rs.core.Cookie;
import jakarta.ws.rs.core.HttpHeaders;
import jakarta.ws.rs.core.MediaType;
import jakarta.ws.rs.core.Response;

import java.net.URI;
import java.util.logging.Logger;

/**
 * Root endpoint - serves landing page at /
 * Redirects authenticated users to /app
 */
@Path("/")
public class RootResource {

    private static final Logger LOGGER = Logger.getLogger(RootResource.class.getName());
    private static final String USER_COOKIE_NAME = "filesurf_userId";

    @Inject
    UserService userService;

    @Inject
    @Location("index.html")
    Template landing;

    @Inject
    CssVersionProvider cssVersionProvider;

    @GET
    @Produces(MediaType.TEXT_HTML)
    public Response root(@Context HttpHeaders headers) {
        // Check if user is already authenticated
        String userId = extractUserIdFromCookies(headers);

        if (userId != null && !userId.isBlank()) {
            UserRecord user = userService.getUserByUserId(userId);
            if (user != null && user.isActive()) {
                // User is authenticated, redirect to app
                LOGGER.info("Root accessed by authenticated user: " + user.getEmail() + ", redirecting to /app");
                return Response.seeOther(URI.create("/app")).build();
            }
        }

        // User is not authenticated, show landing page
        LOGGER.info("Root accessed, serving landing page");
        return Response.ok(landing.data("cssPath", cssVersionProvider.getCssPath())).build();
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
}
