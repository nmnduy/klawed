package com.filesurf.service;

import io.quarkus.runtime.StartupEvent;
import jakarta.enterprise.context.ApplicationScoped;
import jakarta.enterprise.event.Observes;
import jakarta.inject.Inject;
import org.eclipse.microprofile.config.inject.ConfigProperty;

import java.io.File;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.logging.Logger;

/**
 * Application startup listener that initializes metrics and other startup tasks.
 */
@ApplicationScoped
public class ApplicationStartupListener {

    private static final Logger LOGGER = Logger.getLogger(ApplicationStartupListener.class.getName());

    @Inject
    MetricsService metricsService;

    @ConfigProperty(name = "chat.messages.db-dir", defaultValue = "./data/chat-messages")
    String chatMessagesDbDir;

    @ConfigProperty(name = "klawed.sqlite-queue.db-dir", defaultValue = "./data/klawed-messages")
    String klawedMessagesDbDir;

    @ConfigProperty(name = "filesurf.persist.root", defaultValue = "./data/persistent")
    String persistRootDir;

    @ConfigProperty(name = "demo.videos.directory", defaultValue = "./data/demos")
    String demoVideosDir;

    void onStart(@Observes StartupEvent ev) {
        LOGGER.info("FileSurf v2 application starting up...");

        // Create required directories
        createRequiredDirectories();

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

    /**
     * Creates all required directories for the application.
     * This ensures that directories exist before any service tries to use them.
     */
    private void createRequiredDirectories() {
        LOGGER.info("Creating required directories...");
        
        // List of directories to create
        String[] directories = {
            chatMessagesDbDir,
            klawedMessagesDbDir,
            persistRootDir,
            demoVideosDir
        };
        
        for (String dirPath : directories) {
            createDirectory(dirPath);
        }
        
        LOGGER.info("Directory creation completed.");
    }

    /**
     * Creates a directory if it doesn't exist.
     * 
     * @param dirPath The directory path to create
     */
    private void createDirectory(String dirPath) {
        try {
            Path path = Paths.get(dirPath);
            if (!Files.exists(path)) {
                Files.createDirectories(path);
                LOGGER.info("Created directory: " + dirPath);
            } else {
                LOGGER.fine("Directory already exists: " + dirPath);
            }
        } catch (Exception e) {
            LOGGER.warning("Failed to create directory '" + dirPath + "': " + e.getMessage());
        }
    }
}