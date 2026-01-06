package com.filesurf.filter;

import io.vertx.core.http.HttpServerRequest;
import jakarta.ws.rs.container.ContainerRequestContext;
import jakarta.ws.rs.container.ContainerResponseContext;
import jakarta.ws.rs.core.Context;
import org.jboss.resteasy.reactive.server.ServerRequestFilter;
import org.jboss.resteasy.reactive.server.ServerResponseFilter;
import org.jboss.resteasy.reactive.server.SimpleResourceInfo;

import java.time.Duration;
import java.time.Instant;
import java.util.logging.Logger;

/**
 * HTTP logging filter that logs request and response details including:
 * - HTTP method
 * - Request URI
 * - Response status code
 * - Duration
 * - Client IP address
 * - User agent
 */
public class HttpLoggingFilter {
    
    private static final Logger LOGGER = Logger.getLogger(HttpLoggingFilter.class.getName());
    
    @Context
    HttpServerRequest httpServerRequest;
    
    private static final String START_TIME_PROPERTY = "httpRequestStartTime";
    
    /**
     * Log incoming request
     */
    @ServerRequestFilter
    public void filterRequest(ContainerRequestContext requestContext) {
        String uri = requestContext.getUriInfo().getRequestUri().toString();
        
        // Skip logging for health checks and common static resources
        if (shouldSkipLogging(uri)) {
            return;
        }
        
        // Store start time for duration calculation
        requestContext.setProperty(START_TIME_PROPERTY, Instant.now());
        
        // Log request details
        String method = requestContext.getMethod();
        String clientIp = getClientIp();
        String userAgent = requestContext.getHeaderString("User-Agent");
        
        LOGGER.info(String.format("HTTP Request: %s %s | Client: %s | User-Agent: %s", 
            method, uri, clientIp, userAgent != null ? userAgent : "Unknown"));
    }
    
    /**
     * Log response details including duration
     */
    @ServerResponseFilter
    public void filterResponse(ContainerRequestContext requestContext, ContainerResponseContext responseContext) {
        String uri = requestContext.getUriInfo().getRequestUri().toString();
        
        // Skip logging for health checks and common static resources
        if (shouldSkipLogging(uri)) {
            return;
        }
        
        Instant startTime = (Instant) requestContext.getProperty(START_TIME_PROPERTY);
        if (startTime == null) {
            startTime = Instant.now();
        }
        
        Duration duration = Duration.between(startTime, Instant.now());
        String method = requestContext.getMethod();
        int status = responseContext.getStatus();
        String clientIp = getClientIp();
        
        // Log response details
        LOGGER.info(String.format("HTTP Response: %s %s | Status: %d | Duration: %dms | Client: %s", 
            method, uri, status, duration.toMillis(), clientIp));
    }
    
    /**
     * Get client IP address from request
     */
    private String getClientIp() {
        if (httpServerRequest == null) {
            return "Unknown";
        }
        
        // Try to get X-Forwarded-For header first (for proxies)
        String xForwardedFor = httpServerRequest.getHeader("X-Forwarded-For");
        if (xForwardedFor != null && !xForwardedFor.isEmpty()) {
            // X-Forwarded-For can contain multiple IPs, take the first one
            String[] ips = xForwardedFor.split(",");
            return ips[0].trim();
        }
        
        // Fall back to remote address
        return httpServerRequest.remoteAddress() != null 
            ? httpServerRequest.remoteAddress().toString() 
            : "Unknown";
    }
    
    /**
     * Check if we should skip logging for this URI
     * Skips health checks, metrics, and static resources
     */
    private boolean shouldSkipLogging(String uri) {
        // Skip health checks
        if (uri.contains("/health") || uri.contains("/q/health")) {
            return true;
        }
        
        // Skip metrics
        if (uri.contains("/metrics") || uri.contains("/q/metrics")) {
            return true;
        }
        
        // Skip static resources (CSS, JS, images, fonts)
        if (uri.endsWith(".css") || uri.endsWith(".js") || 
            uri.endsWith(".png") || uri.endsWith(".jpg") || 
            uri.endsWith(".jpeg") || uri.endsWith(".gif") ||
            uri.endsWith(".ico") || uri.endsWith(".svg") ||
            uri.endsWith(".woff") || uri.endsWith(".woff2") ||
            uri.endsWith(".ttf") || uri.endsWith(".eot")) {
            return true;
        }
        
        return false;
    }
}