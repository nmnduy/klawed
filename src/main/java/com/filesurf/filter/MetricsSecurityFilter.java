package com.filesurf.filter;

import io.quarkus.vertx.http.runtime.filters.Filters;
import io.vertx.core.Handler;
import io.vertx.ext.web.RoutingContext;
import jakarta.enterprise.context.ApplicationScoped;
import jakarta.enterprise.event.Observes;

import java.util.logging.Logger;

/**
 * Vert.x-level filter that restricts access to the /metrics endpoint to only Tailscale network (100.x.x.x).
 * This filter runs at the Vert.x HTTP layer, before JAX-RS processing, ensuring it applies to all endpoints
 * including those registered directly with the HTTP router (like /metrics from Micrometer).
 */
@ApplicationScoped
public class MetricsSecurityFilter {

    private static final Logger LOGGER = Logger.getLogger(MetricsSecurityFilter.class.getName());
    private static final String TAILSCALE_NETWORK_PREFIX = "100.";

    public void registerFilter(@Observes Filters filters) {
        filters.register(new Handler<RoutingContext>() {
            @Override
            public void handle(RoutingContext rc) {
                String path = rc.request().path();
                
                // Only check /metrics and /q/metrics endpoints
                if (!isMetricsEndpoint(path)) {
                    rc.next();
                    return;
                }

                String clientIp = getClientIp(rc);
                
                // Check if IP is from Tailscale network
                if (!isTailscaleIp(clientIp)) {
                    LOGGER.warning(String.format("Access denied to %s from non-Tailscale IP: %s", path, clientIp));
                    rc.response()
                        .setStatusCode(403)
                        .putHeader("Content-Type", "text/plain")
                        .end("Access denied: Metrics endpoint is only accessible from Tailscale network");
                    return;
                }

                LOGGER.fine(String.format("Allowing %s access from Tailscale IP: %s", path, clientIp));
                rc.next();
            }
        }, 10); // Priority 10 = run early, before other filters
    }

    /**
     * Checks if the given path is the metrics endpoint.
     */
    private boolean isMetricsEndpoint(String path) {
        if (path == null) {
            return false;
        }
        
        // Normalize path (remove leading slash for comparison)
        String normalizedPath = path.startsWith("/") ? path.substring(1) : path;
        return normalizedPath.equals("metrics") || normalizedPath.equals("q/metrics");
    }

    /**
     * Checks if the IP address is from the Tailscale network (100.x.x.x).
     */
    private boolean isTailscaleIp(String ip) {
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
     * 3. X-Real-IP (alternative proxy header)
     * 4. Direct connection IP
     */
    private String getClientIp(RoutingContext rc) {
        // Priority 1: CF-Connecting-IP (Cloudflare's real IP header)
        String cfConnectingIp = rc.request().getHeader("CF-Connecting-IP");
        if (cfConnectingIp != null && !cfConnectingIp.isEmpty()) {
            LOGGER.fine(String.format("Using CF-Connecting-IP: %s", cfConnectingIp));
            return cfConnectingIp.trim();
        }
        
        // Priority 2: X-Forwarded-For (standard proxy header)
        String xForwardedFor = rc.request().getHeader("X-Forwarded-For");
        if (xForwardedFor != null && !xForwardedFor.isEmpty()) {
            // X-Forwarded-For can contain multiple IPs (client, proxy1, proxy2, ...)
            // Take the first one (original client IP)
            String[] ips = xForwardedFor.split(",");
            String firstIp = ips[0].trim();
            LOGGER.fine(String.format("Using X-Forwarded-For: %s (from: %s)", firstIp, xForwardedFor));
            return firstIp;
        }
        
        // Priority 3: X-Real-IP (alternative proxy header)
        String xRealIp = rc.request().getHeader("X-Real-IP");
        if (xRealIp != null && !xRealIp.isEmpty()) {
            LOGGER.fine(String.format("Using X-Real-IP: %s", xRealIp));
            return xRealIp.trim();
        }
        
        // Priority 4: Direct connection IP
        String remoteAddress = rc.request().remoteAddress() != null 
            ? rc.request().remoteAddress().host()
            : "Unknown";
        LOGGER.fine(String.format("Using remote address: %s", remoteAddress));
        return remoteAddress;
    }
}
