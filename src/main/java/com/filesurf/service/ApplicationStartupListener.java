package com.filesurf.service;

import io.quarkus.runtime.StartupEvent;
import jakarta.enterprise.context.ApplicationScoped;
import jakarta.enterprise.event.Observes;
import jakarta.inject.Inject;

import java.util.logging.Logger;

/**
 * Application startup listener that initializes metrics and other startup tasks.
 */
@ApplicationScoped
public class ApplicationStartupListener {

    private static final Logger LOGGER = Logger.getLogger(ApplicationStartupListener.class.getName());

    @Inject
    MetricsService metricsService;

    void onStart(@Observes StartupEvent ev) {
        LOGGER.info("FileSurf v2 application starting up...");

        // Initialize metrics
        metricsService.initializeMetrics();

        LOGGER.info("Application startup completed. Metrics initialized.");
        LOGGER.info("Prometheus metrics available at: /metrics");
        LOGGER.info("Application endpoints:");
        LOGGER.info("  - Main interface: /file-chat");
        LOGGER.info("  - Metrics: /metrics");
        LOGGER.info("  - Health: /q/health");
        LOGGER.info("  - OpenAPI: /q/openapi");
    }
}