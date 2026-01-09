package com.filesurf.filter;

import io.vertx.core.http.HttpServerRequest;
import jakarta.annotation.Priority;
import jakarta.inject.Inject;
import jakarta.ws.rs.Priorities;
import jakarta.ws.rs.container.ContainerRequestContext;
import jakarta.ws.rs.container.ContainerRequestFilter;
import jakarta.ws.rs.core.Context;
import jakarta.ws.rs.core.Response;
import jakarta.ws.rs.ext.Provider;

import java.io.IOException;
import java.util.logging.Logger;

/**
 * Filter that restricts access to the /metrics endpoint to only Tailscale network (100.x.x.x).
 * This filter runs before authentication to prevent public access to metrics.
 */
@Provider
@Priority(Priorities.AUTHENTICATION - 1)  // Run before AuthenticationFilter
public class MetricsAccessFilter implements ContainerRequestFilter {

    private static final Logger LOGGER = Logger.getLogger(MetricsAccessFilter.class.getName());
    private static final String TAILSCALE_NETWORK_PREFIX = "100.";

    @Context
    HttpServerRequest httpServerRequest;

    @Override
    public void filter(ContainerRequestContext requestContext) throws IOException {
        String path = requestContext.getUriInfo().getPath();
        
        // Only apply this filter to /metrics endpoint
        if (!isMetricsEndpoint(path)) {
            return;
        }

        String clientIp = getClientIp();
        
        // Check if IP is from Tailscale network
        if (!isTailscaleIp(clientIp)) {
            LOGGER.warning(String.format("Access denied to /metrics from non-Tailscale IP: %s", clientIp));
            requestContext.abortWith(
                Response.status(Response.Status.FORBIDDEN)
                    .entity("Access denied: Metrics endpoint is only accessible from Tailscale network")
                    .type("text/plain")
                    .build()
            );
            return;
        }

        LOGGER.fine(String.format("Allowing /metrics access from Tailscale IP: %s", clientIp));
    }

    /**
     * Checks if the given path is the metrics endpoint.
     * Public for testing purposes.
     */
    public boolean isMetricsEndpoint(String path) {
        if (path == null) {
            return false;
        }
        
        String normalizedPath = path.startsWith("/") ? path.substring(1) : path;
        return normalizedPath.equals("metrics") || normalizedPath.equals("q/metrics");
    }

    /**
     * Checks if the IP address is from the Tailscale network (100.x.x.x).
     * Public for testing purposes.
     */
    public boolean isTailscaleIp(String ip) {
        if (ip == null || ip.isEmpty() || ip.equals("Unknown")) {
            return false;
        }
        
        // Extract IP address if it includes port
        String ipOnly = ip.split(":")[0];
        
        // Check if it starts with 100.
        return ipOnly.startsWith(TAILSCALE_NETWORK_PREFIX);
    }

    /**
     * Gets the client IP address, checking multiple headers in order:
     * 1. CF-Connecting-IP (Cloudflare's real IP header - most reliable behind Cloudflare)
     * 2. X-Forwarded-For (standard proxy header)
     * 3. Direct connection IP
     * 
     * Public for testing purposes.
     */
    public String getClientIp() {
        if (httpServerRequest == null) {
            return "Unknown";
        }
        
        // Priority 1: CF-Connecting-IP (Cloudflare's real IP header)
        String cfConnectingIp = httpServerRequest.getHeader("CF-Connecting-IP");
        if (cfConnectingIp != null && !cfConnectingIp.isEmpty()) {
            LOGGER.fine(String.format("Using CF-Connecting-IP: %s", cfConnectingIp));
            return cfConnectingIp.trim();
        }
        
        // Priority 2: X-Forwarded-For (standard proxy header)
        String xForwardedFor = httpServerRequest.getHeader("X-Forwarded-For");
        if (xForwardedFor != null && !xForwardedFor.isEmpty()) {
            // X-Forwarded-For can contain multiple IPs (client, proxy1, proxy2, ...)
            // Take the first one (original client IP)
            String[] ips = xForwardedFor.split(",");
            String firstIp = ips[0].trim();
            LOGGER.fine(String.format("Using X-Forwarded-For: %s (from: %s)", firstIp, xForwardedFor));
            return firstIp;
        }
        
        // Priority 3: Direct connection IP
        String remoteAddress = httpServerRequest.remoteAddress() != null 
            ? httpServerRequest.remoteAddress().toString() 
            : "Unknown";
        LOGGER.fine(String.format("Using remote address: %s", remoteAddress));
        return remoteAddress;
    }
}
