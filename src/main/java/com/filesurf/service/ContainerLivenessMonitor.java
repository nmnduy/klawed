package com.filesurf.service;

import io.quarkus.scheduler.Scheduled;
import jakarta.annotation.PostConstruct;
import jakarta.annotation.PreDestroy;
import jakarta.enterprise.context.ApplicationScoped;
import jakarta.inject.Inject;
import org.eclipse.microprofile.config.inject.ConfigProperty;

import java.time.Instant;
import java.util.List;
import java.util.concurrent.ConcurrentHashMap;
import java.util.logging.Logger;

/**
 * Monitors the liveness of klawed containers for active sessions and handles dead containers.
 * 
 * This service periodically checks if containers running klawed agents are still alive.
 * If a container is found to be dead, it can:
 * - Log a warning
 * - Notify the client via WebSocket
 * - Attempt to restart the container (if auto-restart is enabled)
 * 
 * Configuration properties:
 * - container.liveness.enabled: Enable/disable liveness monitoring (default: true)
 * - container.liveness.check.interval: Check interval in seconds (used in @Scheduled)
 * - container.liveness.auto-restart: Enable/disable auto-restart of dead containers (default: true)
 * - container.liveness.max-restart-attempts: Maximum restart attempts per session (default: 3)
 * - container.liveness.restart-cooldown-seconds: Cooldown between restart attempts (default: 30)
 */
@ApplicationScoped
public class ContainerLivenessMonitor {

    private static final Logger LOGGER = Logger.getLogger(ContainerLivenessMonitor.class.getName());
    
    // Track restart attempts per session: sessionId -> RestartInfo
    private final ConcurrentHashMap<String, RestartInfo> restartTracking = new ConcurrentHashMap<>();
    
    @Inject
    KlawedAgentManager agentManager;
    
    @Inject
    PodmanSandboxService podmanSandboxService;
    
    @ConfigProperty(name = "container.liveness.enabled", defaultValue = "true")
    boolean livenessEnabled;
    
    @ConfigProperty(name = "container.liveness.auto-restart", defaultValue = "true")
    boolean autoRestartEnabled;
    
    @ConfigProperty(name = "container.liveness.max-restart-attempts", defaultValue = "3")
    int maxRestartAttempts;
    
    @ConfigProperty(name = "container.liveness.restart-cooldown-seconds", defaultValue = "30")
    int restartCooldownSeconds;
    
    @PostConstruct
    void init() {
        if (livenessEnabled) {
            LOGGER.info("ContainerLivenessMonitor initialized - enabled: " + livenessEnabled + 
                       ", auto-restart: " + autoRestartEnabled + 
                       ", max-attempts: " + maxRestartAttempts + 
                       ", cooldown: " + restartCooldownSeconds + "s");
        } else {
            LOGGER.info("ContainerLivenessMonitor disabled");
        }
    }
    
    /**
     * Scheduled liveness check that runs every 10 seconds.
     * Checks all active sessions with containers and handles any dead containers.
     */
    @Scheduled(every = "${container.liveness.check.interval:10s}")
    void checkContainerLiveness() {
        if (!livenessEnabled) {
            return;
        }
        
        // Only perform checks when sandbox mode is enabled
        if (!podmanSandboxService.isEnabled()) {
            LOGGER.fine("Container liveness check skipped - sandbox mode not enabled");
            return;
        }
        
        LOGGER.fine("Running container liveness check...");
        
        List<String> activeSessions = agentManager.getActiveSessions();
        
        if (activeSessions.isEmpty()) {
            LOGGER.fine("No active sessions to check");
            return;
        }
        
        for (String sessionId : activeSessions) {
            try {
                checkSessionContainer(sessionId);
            } catch (Exception e) {
                LOGGER.warning("[SESSION:" + sessionId + "] Error checking container liveness: " + e.getMessage());
            }
        }
    }
    
    /**
     * Check the container status for a specific session.
     */
    private void checkSessionContainer(String sessionId) {
        KlawedAgentManager.KlawedAgentInstance agent = agentManager.getAgentForSession(sessionId);
        
        if (agent == null) {
            LOGGER.fine("[SESSION:" + sessionId + "] No agent instance found, skipping");
            return;
        }
        
        // Only check sessions running in containers
        if (!agent.isRunningInContainer()) {
            LOGGER.fine("[SESSION:" + sessionId + "] Agent not running in container, skipping liveness check");
            return;
        }
        
        // Check if container is still running
        if (agent.isRunning()) {
            LOGGER.fine("[SESSION:" + sessionId + "] Container is healthy");
            return;
        }
        
        // Container is dead - handle it
        handleDeadContainer(sessionId, agent);
    }
    
    /**
     * Handle a dead container: log, notify client, and optionally attempt restart.
     */
    private void handleDeadContainer(String sessionId, KlawedAgentManager.KlawedAgentInstance agent) {
        String containerId = agent.getContainerId();
        LOGGER.warning("[SESSION:" + sessionId + "] Container died unexpectedly: " + containerId);
        
        // Get or create restart tracking info
        RestartInfo restartInfo = restartTracking.computeIfAbsent(sessionId, 
            k -> new RestartInfo());
        
        // Notify client about container death
        notifyClientContainerDied(sessionId, restartInfo.attempts, maxRestartAttempts);
        
        // Check if we should attempt restart
        if (!autoRestartEnabled) {
            LOGGER.info("[SESSION:" + sessionId + "] Auto-restart disabled, not attempting restart");
            notifyClientRestartDisabled(sessionId);
            return;
        }
        
        // Check if we've exceeded max restart attempts
        if (restartInfo.attempts >= maxRestartAttempts) {
            LOGGER.warning("[SESSION:" + sessionId + "] Max restart attempts (" + maxRestartAttempts + 
                         ") exceeded, not attempting restart");
            notifyClientMaxRestartsExceeded(sessionId, maxRestartAttempts);
            return;
        }
        
        // Check cooldown
        if (restartInfo.lastAttemptTime != null) {
            long secondsSinceLastAttempt = java.time.Duration.between(
                restartInfo.lastAttemptTime, Instant.now()).getSeconds();
            
            if (secondsSinceLastAttempt < restartCooldownSeconds) {
                long remainingCooldown = restartCooldownSeconds - secondsSinceLastAttempt;
                LOGGER.info("[SESSION:" + sessionId + "] Restart cooldown active, " + 
                           remainingCooldown + "s remaining");
                notifyClientCooldownActive(sessionId, remainingCooldown);
                return;
            }
        }
        
        // Attempt restart
        attemptRestart(sessionId, agent, restartInfo);
    }
    
    /**
     * Attempt to restart a dead container.
     */
    private void attemptRestart(String sessionId, KlawedAgentManager.KlawedAgentInstance agent, 
                                RestartInfo restartInfo) {
        restartInfo.attempts++;
        restartInfo.lastAttemptTime = Instant.now();
        
        LOGGER.info("[SESSION:" + sessionId + "] Attempting restart (attempt " + 
                   restartInfo.attempts + "/" + maxRestartAttempts + ")");
        
        notifyClientRestartAttempt(sessionId, restartInfo.attempts, maxRestartAttempts);
        
        try {
            // Stop the old agent (cleanup)
            agentManager.stopAgentForSession(sessionId);
            
            // Try to get container logs before restart for debugging
            String containerId = agent.getContainerId();
            if (containerId != null) {
                try {
                    String logs = podmanSandboxService.getContainerLogs(containerId, 50);
                    LOGGER.info("[SESSION:" + sessionId + "] Container logs before restart:\n" + logs);
                } catch (Exception e) {
                    LOGGER.fine("[SESSION:" + sessionId + "] Could not fetch container logs: " + e.getMessage());
                }
            }
            
            // Note: We don't automatically restart here because:
            // 1. The user needs to send a new message to trigger agent start
            // 2. Session directory and other context may need verification
            // 3. The client should be notified and may want to take action
            
            LOGGER.info("[SESSION:" + sessionId + "] Agent stopped, ready for reconnection");
            notifyClientRestartReady(sessionId);
            
        } catch (Exception e) {
            LOGGER.severe("[SESSION:" + sessionId + "] Failed to restart agent: " + e.getMessage());
            notifyClientRestartFailed(sessionId, e.getMessage());
        }
    }
    
    /**
     * Clean up restart tracking for a session.
     * Should be called when a session ends or is explicitly cleaned up.
     */
    public void cleanupSession(String sessionId) {
        RestartInfo removed = restartTracking.remove(sessionId);
        if (removed != null) {
            LOGGER.fine("[SESSION:" + sessionId + "] Cleaned up restart tracking (had " + 
                       removed.attempts + " restart attempts)");
        }
    }
    
    /**
     * Reset restart tracking for a session.
     * Can be called when a session successfully reconnects.
     */
    public void resetRestartTracking(String sessionId) {
        restartTracking.remove(sessionId);
        LOGGER.fine("[SESSION:" + sessionId + "] Reset restart tracking");
    }
    
    /**
     * Get restart info for a session (for monitoring/debugging).
     */
    public RestartInfo getRestartInfo(String sessionId) {
        return restartTracking.get(sessionId);
    }
    
    // ========== Client Notification Methods ==========
    
    private void notifyClientContainerDied(String sessionId, int currentAttempts, int maxAttempts) {
        // Backend-only logging for now - not forwarding to frontend
        LOGGER.info("[SESSION:" + sessionId + "] AGENT_STATUS: CONTAINER_DIED (attempts: " + 
                   currentAttempts + "/" + maxAttempts + ")");
    }
    
    private void notifyClientRestartDisabled(String sessionId) {
        // Backend-only logging for now - not forwarding to frontend
        LOGGER.info("[SESSION:" + sessionId + "] AGENT_STATUS: RESTART_DISABLED");
    }
    
    private void notifyClientMaxRestartsExceeded(String sessionId, int maxAttempts) {
        // Backend-only logging for now - not forwarding to frontend
        LOGGER.warning("[SESSION:" + sessionId + "] AGENT_STATUS: MAX_RESTARTS_EXCEEDED (max: " + maxAttempts + ")");
    }
    
    private void notifyClientCooldownActive(String sessionId, long remainingSeconds) {
        // Backend-only logging for now - not forwarding to frontend
        LOGGER.info("[SESSION:" + sessionId + "] AGENT_STATUS: COOLDOWN_ACTIVE (" + remainingSeconds + "s remaining)");
    }
    
    private void notifyClientRestartAttempt(String sessionId, int attempt, int maxAttempts) {
        // Backend-only logging for now - not forwarding to frontend
        LOGGER.info("[SESSION:" + sessionId + "] AGENT_STATUS: RESTART_ATTEMPT (" + attempt + "/" + maxAttempts + ")");
    }
    
    private void notifyClientRestartReady(String sessionId) {
        // Backend-only logging for now - not forwarding to frontend
        LOGGER.info("[SESSION:" + sessionId + "] AGENT_STATUS: RESTART_READY");
    }
    
    private void notifyClientRestartFailed(String sessionId, String errorMessage) {
        // Backend-only logging for now - not forwarding to frontend
        LOGGER.severe("[SESSION:" + sessionId + "] AGENT_STATUS: RESTART_FAILED - " + errorMessage);
    }
    
    /**
     * Periodic cleanup of orphaned containers.
     * 
     * Runs every 5 minutes to find and stop containers that are:
     * - Running with klawed-* prefix
     * - Not tracked in the in-memory sessionContainers map
     * 
     * This handles cases where containers become orphaned due to:
     * - Application restart/crash losing in-memory tracking state
     * - Race conditions during disconnect/reconnect
     * - Bugs in tracking logic
     */
    @Scheduled(every = "5m")
    void cleanupOrphanedContainers() {
        if (!livenessEnabled) {
            return;
        }
        
        // Only perform cleanup when sandbox mode is enabled
        if (!podmanSandboxService.isEnabled()) {
            return;
        }
        
        LOGGER.fine("Running periodic orphaned container cleanup...");
        
        try {
            int cleaned = podmanSandboxService.cleanupOrphanedContainers();
            if (cleaned > 0) {
                LOGGER.info("Periodic cleanup: stopped " + cleaned + " orphaned klawed container(s)");
            }
        } catch (Exception e) {
            LOGGER.warning("Error during periodic orphaned container cleanup: " + e.getMessage());
        }
    }
    
    /**
     * Shutdown and cleanup.
     */
    @PreDestroy
    void shutdown() {
        LOGGER.info("ContainerLivenessMonitor shutting down, clearing restart tracking for " + 
                   restartTracking.size() + " sessions");
        restartTracking.clear();
    }
    
    // ========== Inner Classes ==========
    
    /**
     * Tracks restart attempts and timing for a session.
     */
    public static class RestartInfo {
        private int attempts = 0;
        private Instant lastAttemptTime = null;
        
        public int getAttempts() {
            return attempts;
        }
        
        public Instant getLastAttemptTime() {
            return lastAttemptTime;
        }
        
        @Override
        public String toString() {
            return "RestartInfo[attempts=" + attempts + ", lastAttemptTime=" + lastAttemptTime + "]";
        }
    }
}
