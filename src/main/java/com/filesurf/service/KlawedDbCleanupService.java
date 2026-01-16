package com.filesurf.service;

import io.quarkus.scheduler.Scheduled;
import jakarta.enterprise.context.ApplicationScoped;
import jakarta.enterprise.context.control.ActivateRequestContext;
import org.eclipse.microprofile.config.inject.ConfigProperty;

import java.io.BufferedReader;
import java.io.InputStreamReader;
import java.util.ArrayList;
import java.util.List;
import java.util.logging.Logger;

/**
 * Service to periodically clean up old klawed message database files.
 *
 * Runs a simple find command to remove files older than 60 days from the
 * klawed messages directory (/var/lib/filesurf/data/klawed-messages/).
 */
@ApplicationScoped
public class KlawedDbCleanupService {

    private static final Logger LOGGER = Logger.getLogger(KlawedDbCleanupService.class.getName());

    @ConfigProperty(name = "klawed.sqlite-queue.db-dir", defaultValue = "./data/klawed-messages")
    String sqliteQueueDbDir;

    @ConfigProperty(name = "klawed.db-cleanup.enabled", defaultValue = "true")
    boolean cleanupEnabled;

    /**
     * Scheduled cleanup: runs daily at 3 AM
     * Removes klawed DB files older than 60 days
     */
    @Scheduled(cron = "0 0 3 * * ?")
    @ActivateRequestContext
    public void cleanupOldDbFiles() {
        if (!cleanupEnabled) {
            return;
        }

        LOGGER.info("=== Starting scheduled klawed DB cleanup (removing files older than 60 days) ===");

        try {
            // Build find command: find <dir> -name "klawed_messages_*.db*" -mtime +60 -delete
            List<String> command = new ArrayList<>();
            command.add("find");
            command.add(sqliteQueueDbDir);
            command.add("-name");
            command.add("klawed_messages_*.db*");
            command.add("-mtime");
            command.add("+60");  // Files older than 60 days (2 months)
            command.add("-delete");

            LOGGER.info("Cleanup command: " + String.join(" ", command));

            ProcessBuilder pb = new ProcessBuilder(command);
            pb.redirectErrorStream(true);
            Process process = pb.start();

            // Read output
            StringBuilder output = new StringBuilder();
            try (BufferedReader reader = new BufferedReader(new InputStreamReader(process.getInputStream()))) {
                String line;
                while ((line = reader.readLine()) != null) {
                    output.append(line).append("\n");
                }
            }

            int exitCode = process.waitFor();

            if (exitCode == 0) {
                LOGGER.info("Klawed DB cleanup completed successfully (exit code: 0)");
                if (!output.toString().trim().isEmpty()) {
                    LOGGER.info("Cleanup output: " + output);
                }
            } else {
                LOGGER.warning("Klawed DB cleanup failed with exit code " + exitCode + ": " + output);
            }

        } catch (Exception e) {
            LOGGER.warning("Error during klawed DB cleanup: " + e.getMessage());
        }

        LOGGER.info("=== Klawed DB cleanup finished ===");
    }
}
