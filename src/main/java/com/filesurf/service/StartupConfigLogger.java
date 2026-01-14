package com.filesurf.service;

import io.quarkus.runtime.StartupEvent;
import jakarta.enterprise.context.ApplicationScoped;
import jakarta.enterprise.event.Observes;
import org.eclipse.microprofile.config.inject.ConfigProperty;

import java.util.logging.Logger;

/**
 * Logs important configuration values at application startup.
 * This helps verify that the correct configuration is being used,
 * especially in production where configuration issues can be hard to diagnose.
 */
@ApplicationScoped
public class StartupConfigLogger {

    private static final Logger LOGGER = Logger.getLogger(StartupConfigLogger.class.getName());

    @ConfigProperty(name = "quarkus.profile")
    String profile;

    @ConfigProperty(name = "quarkus.http.port")
    int httpPort;

    @ConfigProperty(name = "quarkus.http.host")
    String httpHost;

    @ConfigProperty(name = "filesurf.persist.root")
    String persistRoot;

    @ConfigProperty(name = "filesurf.sessions.base-dir")
    String sessionsBaseDir;

    @ConfigProperty(name = "demo.videos.directory")
    String demoVideosDir;

    @ConfigProperty(name = "quarkus.datasource.jdbc.url")
    String datasourceUrl;

    @ConfigProperty(name = "sandbox.podman.enabled")
    boolean podmanEnabled;

    @ConfigProperty(name = "sandbox.podman.image")
    String podmanImage;

    @ConfigProperty(name = "sandbox.podman.env-file")
    String podmanEnvFile;

    @ConfigProperty(name = "klawed.sqlite-queue.db-dir")
    String sqliteQueueDbDir;

    @ConfigProperty(name = "cookie.secure")
    boolean cookieSecure;

    @ConfigProperty(name = "container.tracking.db.path")
    String containerTrackingDbPath;

    @ConfigProperty(name = "container.liveness.enabled")
    boolean containerLivenessEnabled;

    @ConfigProperty(name = "quarkus.log.file.path")
    String logFilePath;

    void onStart(@Observes StartupEvent ev) {
        LOGGER.info("================================================================================");
        LOGGER.info("FileSurf v2 - Startup Configuration");
        LOGGER.info("================================================================================");
        LOGGER.info("Profile: " + profile);
        LOGGER.info("");
        
        LOGGER.info("HTTP Configuration:");
        LOGGER.info("  Port: " + httpPort);
        LOGGER.info("  Host: " + httpHost);
        LOGGER.info("  Secure Cookies: " + cookieSecure);
        LOGGER.info("");
        
        LOGGER.info("Storage Paths:");
        LOGGER.info("  Persistent Root: " + persistRoot);
        LOGGER.info("  Sessions Base Dir: " + sessionsBaseDir);
        LOGGER.info("  Demo Videos: " + demoVideosDir);
        LOGGER.info("  Database: " + datasourceUrl);
        LOGGER.info("  Container Tracking DB: " + containerTrackingDbPath);
        LOGGER.info("  Log File: " + logFilePath);
        LOGGER.info("");
        
        LOGGER.info("Klawed Configuration:");
        LOGGER.info("  Podman Sandbox: " + (podmanEnabled ? "enabled" : "disabled"));
        LOGGER.info("  Podman Image: " + podmanImage);
        LOGGER.info("  Podman Env File: " + podmanEnvFile);
        LOGGER.info("  SQLite Queue Messages Dir: " + sqliteQueueDbDir);
        LOGGER.info("");
        
        LOGGER.info("Container Management:");
        LOGGER.info("  Liveness Monitoring: " + (containerLivenessEnabled ? "enabled" : "disabled"));
        LOGGER.info("");
        
        LOGGER.info("================================================================================");
    }
}
