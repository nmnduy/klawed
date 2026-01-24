package com.filesurf.service;

import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.filesurf.model.ChatMessageRecord;
import com.filesurf.model.ChatConstants;
import io.quarkus.scheduler.Scheduled;
import jakarta.annotation.PreDestroy;
import jakarta.enterprise.context.ApplicationScoped;
import jakarta.enterprise.context.control.ActivateRequestContext;
import jakarta.inject.Inject;
import org.eclipse.microprofile.config.inject.ConfigProperty;

import java.io.BufferedReader;
import java.io.IOException;
import java.io.InputStreamReader;
import java.nio.file.Files;
import java.nio.file.Path;
import java.sql.*;
import java.time.Instant;
import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import java.util.Set;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.logging.Logger;
import java.util.stream.Collectors;

/**
 * Service for managing Klawed containers and session tracking.
 *
 * This service combines session management with container lifecycle management:
 * - Tracks active sessions in sessions.db
 * - Runs a scheduled loop every 10 seconds to ensure containers exist for active sessions
 * - Stops orphaned containers that no longer have active sessions
 *
 * Session lifecycle:
 * - registerSession() - Called when WebSocket connects
 * - unregisterSession() - Called when WebSocket disconnects (sets disconnected_at)
 * - Containers are stopped 1.5 minutes after disconnected_at is set
 */
@ApplicationScoped
public class KlawedSandboxService {

    private static final Logger LOGGER = Logger.getLogger(KlawedSandboxService.class.getName());

    // Configuration properties
    @ConfigProperty(name = "sandbox.podman.enabled", defaultValue = "false")
    boolean podmanEnabled;

    @ConfigProperty(name = "sandbox.podman.image", defaultValue = "klawed-sandbox:1.0.0")
    String podmanImage;

    @ConfigProperty(name = "sandbox.podman.memory", defaultValue = "2g")
    String memoryLimit;

    @ConfigProperty(name = "sandbox.podman.cpus", defaultValue = "2")
    String cpuLimit;

    @ConfigProperty(name = "sandbox.podman.pids-limit", defaultValue = "512")
    int pidsLimit;

    @ConfigProperty(name = "klawed.path", defaultValue = "/usr/local/bin/klawed")
    String klawedPath;

    @ConfigProperty(name = "sandbox.podman.env-file", defaultValue = "/etc/filesurf/klawed.env")
    String envFilePath;

    @ConfigProperty(name = "klawed.sqlite-queue.db-dir", defaultValue = "./data/klawed-messages")
    String sqliteQueueDbDir;

    // Persistent storage root path (matches SessionManager)
    @ConfigProperty(name = "filesurf.persist.root", defaultValue = "./data/persistent")
    String persistRoot;

    // Sessions database path
    @ConfigProperty(name = "klawed.sessions.db.path", defaultValue = "data/sessions.db")
    String sessionsDbPath;

    private String jdbcUrl;

    // FileChatService for conversation seeding
    @Inject
    FileChatService fileChatService;

    // ObjectMapper for JSON serialization
    private final ObjectMapper objectMapper = new ObjectMapper();

    // Maximum number of messages to seed for conversation context
    private static final int MAX_SEED_MESSAGES = 100;

    // Inactivity timeout: 1.5 minutes = 90 seconds (for disconnected sessions)
    private static final long INACTIVITY_TIMEOUT_SECONDS = 90;

    // Idle timeout: 30 minutes = 1800 seconds (for active but idle sessions)
    private static final long IDLE_TIMEOUT_SECONDS = 1800;

    // Shutdown flag to stop scheduled tasks from running during shutdown
    private final AtomicBoolean shuttingDown = new AtomicBoolean(false);

    /**
     * Initialize on startup
     */
    @jakarta.annotation.PostConstruct
    public void init() {
        if (podmanEnabled) {
            // Validate that 'latest' tag is never used
            if (podmanImage.endsWith(":latest")) {
                throw new IllegalStateException(
                    "Podman sandbox image must not use ':latest' tag. " +
                    "Please specify a version tag in sandbox.podman.image configuration. " +
                    "Current value: " + podmanImage
                );
            }

            LOGGER.info("Podman sandbox enabled with image: " + podmanImage);

            // Check if Podman is available
            if (!isPodmanAvailable()) {
                LOGGER.warning("Podman is enabled but podman command is not available on the system");
            } else {
                try {
                    String version = getPodmanVersion();
                    LOGGER.info("Podman version: " + version);
                } catch (IOException e) {
                    LOGGER.warning("Failed to get Podman version: " + e.getMessage());
                }
            }
        }

        // Initialize klawed messages directory
        try {
            Path messagesDir = Path.of(sqliteQueueDbDir);
            if (!Files.exists(messagesDir)) {
                Files.createDirectories(messagesDir);
                LOGGER.info("Created klawed messages directory: " + messagesDir);
            }
        } catch (Exception e) {
            throw new RuntimeException("Failed to initialize klawed messages directory", e);
        }

        // Initialize sessions database
        try {
            Path dbFile = Path.of(sessionsDbPath);
            Path parentDir = dbFile.getParent();
            if (parentDir != null && !Files.exists(parentDir)) {
                Files.createDirectories(parentDir);
                LOGGER.info("Created sessions database directory: " + parentDir);
            }

            jdbcUrl = "jdbc:sqlite:" + sessionsDbPath;
            initializeSessionsSchema();
            LOGGER.info("Sessions database initialized: " + sessionsDbPath);
        } catch (Exception e) {
            throw new RuntimeException("Failed to initialize sessions database", e);
        }
    }

    /**
     * Initialize sessions database schema
     */
    private void initializeSessionsSchema() throws SQLException {
        try (Connection conn = DriverManager.getConnection(jdbcUrl);
             Statement stmt = conn.createStatement()) {

            // Enable WAL mode for better concurrency
            stmt.execute("PRAGMA journal_mode = WAL;");
            stmt.execute("PRAGMA synchronous = NORMAL;");
            stmt.execute("PRAGMA busy_timeout = 5000;");

            // Create sessions table
            stmt.execute(
                "CREATE TABLE IF NOT EXISTS sessions (" +
                "session_id TEXT PRIMARY KEY, " +
                "user_id TEXT NOT NULL, " +
                "email TEXT, " +  // Client identity (email address)
                "registered_at INTEGER NOT NULL, " +
                "last_active_at INTEGER NOT NULL, " +
                "disconnected_at INTEGER" +  // NULL when connected, timestamp when disconnected
                ");"
            );

            // Migrate existing tables: add email column if it doesn't exist
            // This is safe to run multiple times (ALTER TABLE IF NOT EXISTS would fail on older SQLite)
            try {
                stmt.execute("ALTER TABLE sessions ADD COLUMN email TEXT;");
                LOGGER.info("Added email column to sessions table (migration)");
            } catch (SQLException e) {
                // Column already exists, ignore
                if (!e.getMessage().contains("duplicate column name")) {
                    throw e;
                }
            }

            LOGGER.info("Sessions schema initialized");
        }
    }

    /**
     * Register a session (called when session is generated)
     */
    public void registerSession(String sessionId, String userId) {
        registerSession(sessionId, userId, null);
    }

    /**
     * Register a session with email (called when session is generated)
     */
    public void registerSession(String sessionId, String userId, String email) {
        try (Connection conn = DriverManager.getConnection(jdbcUrl);
             PreparedStatement pstmt = conn.prepareStatement(
                 "INSERT INTO sessions (session_id, user_id, email, registered_at, last_active_at, disconnected_at) " +
                 "VALUES (?, ?, ?, ?, ?, NULL) " +
                 "ON CONFLICT(session_id) DO UPDATE SET " +
                 "last_active_at = ?, disconnected_at = NULL, email = COALESCE(?, email)"
             )) {

            long now = Instant.now().getEpochSecond();
            pstmt.setString(1, sessionId);
            pstmt.setString(2, userId);
            pstmt.setString(3, email);
            pstmt.setLong(4, now);
            pstmt.setLong(5, now);
            pstmt.setLong(6, now);
            pstmt.setString(7, email);
            pstmt.executeUpdate();

            LOGGER.info("[SESSION:" + sessionId + "] Session registered for user: " + userId + 
                        (email != null ? " (email: " + email + ")" : ""));
        } catch (SQLException e) {
            LOGGER.severe("[SESSION:" + sessionId + "] Failed to register session: " + e.getMessage());
        }
    }

    /**
     * Unregister a session (called on WebSocket disconnect)
     * Sets disconnected_at timestamp
     */
    public void unregisterSession(String sessionId) {
        try (Connection conn = DriverManager.getConnection(jdbcUrl);
             PreparedStatement pstmt = conn.prepareStatement(
                 "UPDATE sessions SET disconnected_at = ? WHERE session_id = ?"
             )) {

            long now = Instant.now().getEpochSecond();
            pstmt.setLong(1, now);
            pstmt.setString(2, sessionId);
            pstmt.executeUpdate();

            LOGGER.info("[SESSION:" + sessionId + "] Session disconnected at: " + now);
        } catch (SQLException e) {
            LOGGER.severe("[SESSION:" + sessionId + "] Failed to unregister session: " + e.getMessage());
        }
    }

    /**
     * Update last active time for a session
     */
    public void updateLastActive(String sessionId) {
        try (Connection conn = DriverManager.getConnection(jdbcUrl);
             PreparedStatement pstmt = conn.prepareStatement(
                 "UPDATE sessions SET last_active_at = ? WHERE session_id = ?"
             )) {

            long now = Instant.now().getEpochSecond();
            pstmt.setLong(1, now);
            pstmt.setString(2, sessionId);
            pstmt.executeUpdate();

            LOGGER.fine("[SESSION:" + sessionId + "] Last active updated: " + now);
        } catch (SQLException e) {
            LOGGER.warning("[SESSION:" + sessionId + "] Failed to update last active: " + e.getMessage());
        }
    }

    /**
     * Get all active sessions (disconnected_at IS NULL)
     */
    private List<SessionRecord> getActiveSessions() throws SQLException {
        List<SessionRecord> sessions = new ArrayList<>();

        try (Connection conn = DriverManager.getConnection(jdbcUrl);
             PreparedStatement pstmt = conn.prepareStatement(
                 "SELECT session_id, user_id, registered_at, last_active_at FROM sessions " +
                 "WHERE disconnected_at IS NULL"
             );
             ResultSet rs = pstmt.executeQuery()) {

            while (rs.next()) {
                sessions.add(new SessionRecord(
                    rs.getString("session_id"),
                    rs.getString("user_id"),
                    rs.getLong("registered_at"),
                    rs.getLong("last_active_at")
                ));
            }
        }

        return sessions;
    }

    /**
     * Get all disconnected sessions that have exceeded the inactivity timeout
     */
    private List<String> getInactiveSessions() throws SQLException {
        List<String> sessionIds = new ArrayList<>();
        long cutoffTime = Instant.now().getEpochSecond() - INACTIVITY_TIMEOUT_SECONDS;

        try (Connection conn = DriverManager.getConnection(jdbcUrl);
             PreparedStatement pstmt = conn.prepareStatement(
                 "SELECT session_id FROM sessions " +
                 "WHERE disconnected_at IS NOT NULL AND disconnected_at < ?"
             )) {

            pstmt.setLong(1, cutoffTime);

            try (ResultSet rs = pstmt.executeQuery()) {
                while (rs.next()) {
                    sessionIds.add(rs.getString("session_id"));
                }
            }
        }

        return sessionIds;
    }

    /**
     * Get all active sessions that have been idle (no activity) for too long
     */
    private List<String> getIdleSessions() throws SQLException {
        List<String> sessionIds = new ArrayList<>();
        long cutoffTime = Instant.now().getEpochSecond() - IDLE_TIMEOUT_SECONDS;

        try (Connection conn = DriverManager.getConnection(jdbcUrl);
             PreparedStatement pstmt = conn.prepareStatement(
                 "SELECT session_id FROM sessions " +
                 "WHERE disconnected_at IS NULL AND last_active_at < ?"
             )) {

            pstmt.setLong(1, cutoffTime);

            try (ResultSet rs = pstmt.executeQuery()) {
                while (rs.next()) {
                    sessionIds.add(rs.getString("session_id"));
                }
            }
        }

        return sessionIds;
    }

    /**
     * Check if a session is active (exists and not disconnected)
     * This is the source of truth for session validation.
     */
    public boolean isSessionActive(String sessionId) {
        try (Connection conn = DriverManager.getConnection(jdbcUrl);
             PreparedStatement pstmt = conn.prepareStatement(
                 "SELECT 1 FROM sessions WHERE session_id = ? AND disconnected_at IS NULL"
             )) {

            pstmt.setString(1, sessionId);
            try (ResultSet rs = pstmt.executeQuery()) {
                boolean active = rs.next();
                LOGGER.fine("[SESSION:" + sessionId + "] Session active check: " + active);
                return active;
            }
        } catch (SQLException e) {
            LOGGER.warning("[SESSION:" + sessionId + "] Failed to check session active: " + e.getMessage());
            return false;
        }
    }

    /**
     * Get the email (client identity) for a session
     * Returns null if session doesn't exist or has no email
     */
    public String getSessionEmail(String sessionId) {
        try (Connection conn = DriverManager.getConnection(jdbcUrl);
             PreparedStatement pstmt = conn.prepareStatement(
                 "SELECT email FROM sessions WHERE session_id = ?"
             )) {

            pstmt.setString(1, sessionId);
            try (ResultSet rs = pstmt.executeQuery()) {
                if (rs.next()) {
                    String email = rs.getString("email");
                    LOGGER.fine("[SESSION:" + sessionId + "] Retrieved email: " + email);
                    return email;
                }
                LOGGER.fine("[SESSION:" + sessionId + "] No session found in database");
                return null;
            }
        } catch (SQLException e) {
            LOGGER.warning("[SESSION:" + sessionId + "] Failed to get session email: " + e.getMessage());
            return null;
        }
    }

    /**
     * Get count of active sessions (connected, not disconnected)
     * @return Number of active sessions in the database
     */
    public int getActiveSessionCount() {
        try (Connection conn = DriverManager.getConnection(jdbcUrl);
             PreparedStatement pstmt = conn.prepareStatement(
                 "SELECT COUNT(*) FROM sessions WHERE disconnected_at IS NULL"
             );
             ResultSet rs = pstmt.executeQuery()) {

            if (rs.next()) {
                return rs.getInt(1);
            }
            return 0;
        } catch (SQLException e) {
            LOGGER.warning("Failed to get active session count: " + e.getMessage());
            return 0;
        }
    }

    /**
     * Check if a session exists in the database
     */
    private boolean sessionExists(String sessionId) throws SQLException {
        try (Connection conn = DriverManager.getConnection(jdbcUrl);
             PreparedStatement pstmt = conn.prepareStatement(
                 "SELECT 1 FROM sessions WHERE session_id = ?"
             )) {

            pstmt.setString(1, sessionId);
            try (ResultSet rs = pstmt.executeQuery()) {
                return rs.next();
            }
        }
    }

    /**
     * Scheduled loop: Manage container lifecycle
     * Runs every 10 seconds
     */
    @Scheduled(every = "10s")
    @ActivateRequestContext
    public void manageContainerLifecycle() {
        if (!podmanEnabled) {
            return;
        }

        // Skip if shutdown is in progress
        if (shuttingDown.get()) {
            LOGGER.fine("Skipping container lifecycle management - shutdown in progress");
            return;
        }

        try {
            // Step 1: Get all active sessions (disconnected_at IS NULL)
            List<SessionRecord> activeSessions = getActiveSessions();
            LOGGER.fine("Managing containers for " + activeSessions.size() + " active session(s)");

            // Get idle sessions to exclude them from auto-start
            List<String> idleSessionIds = getIdleSessions();
            Set<String> idleSet = new HashSet<>(idleSessionIds);

            // Step 2: For each active session, ensure container exists and is healthy
            // BUT skip idle sessions (they should stay stopped)
            for (SessionRecord session : activeSessions) {
                String sessionId = session.sessionId;
                String containerName = "klawed-" + sessionId;

                // Skip starting containers for idle sessions
                if (idleSet.contains(sessionId)) {
                    LOGGER.fine("[SESSION:" + sessionId + "] Skipping auto-start (session is idle)");
                    continue;
                }

                if (!isContainerRunning(containerName)) {
                    LOGGER.info("[SESSION:" + sessionId + "] Container not running, starting it...");
                    try {
                        // Get session directory from SessionManager (assuming it exists)
                        // For now, we'll need to construct the path or inject SessionManager
                        // Let's start the container with a basic setup
                        startContainerForSession(sessionId, session.userId);
                    } catch (IOException e) {
                        LOGGER.severe("[SESSION:" + sessionId + "] Failed to start container: " + e.getMessage());
                    }
                } else {
                    LOGGER.fine("[SESSION:" + sessionId + "] Container is running and healthy");
                }
            }

            // Step 3: List all klawed-* containers from podman
            List<String> runningContainers = listRunningKlawedContainers();
            LOGGER.fine("Found " + runningContainers.size() + " running klawed container(s)");

            // Step 4: For each container, check if it should be stopped
            Set<String> activeSessionIds = activeSessions.stream()
                .map(s -> s.sessionId)
                .collect(Collectors.toSet());

            List<String> inactiveSessionIds = getInactiveSessions();
            Set<String> inactiveSet = new HashSet<>(inactiveSessionIds);

            // Note: idleSet already populated in Step 2

            for (String containerName : runningContainers) {
                // Extract session ID from container name
                String sessionId = containerName.replace("klawed-", "");

                // Check if session doesn't exist OR is inactive OR is idle
                boolean shouldStop = false;
                String reason = "";

                if (!sessionExists(sessionId)) {
                    shouldStop = true;
                    reason = "session does not exist in database";
                } else if (inactiveSet.contains(sessionId)) {
                    shouldStop = true;
                    reason = "session inactive for >" + INACTIVITY_TIMEOUT_SECONDS + " seconds";
                } else if (idleSet.contains(sessionId)) {
                    shouldStop = true;
                    reason = "session idle (no activity) for >" + IDLE_TIMEOUT_SECONDS + " seconds";
                } else if (!activeSessionIds.contains(sessionId)) {
                    shouldStop = true;
                    reason = "session is disconnected";
                }

                if (shouldStop) {
                    LOGGER.info("[SESSION:" + sessionId + "] Stopping container: " + reason);
                    try {
                        stopContainer(containerName);
                    } catch (IOException e) {
                        LOGGER.warning("[SESSION:" + sessionId + "] Failed to stop container: " + e.getMessage());
                    }
                }
            }

        } catch (Exception e) {
            LOGGER.severe("Error in container lifecycle management: " + e.getMessage());
            e.printStackTrace();
        }
    }

    /**
     * List all running klawed-* containers
     */
    private List<String> listRunningKlawedContainers() throws IOException, InterruptedException {
        List<String> containers = new ArrayList<>();

        ProcessBuilder pb = new ProcessBuilder(
            "podman", "ps", "--filter", "name=klawed-", "--filter", "status=running", "--format", "{{.Names}}"
        );
        pb.redirectErrorStream(true);
        Process process = pb.start();

        String output = readProcessOutput(process);
        int exitCode = process.waitFor();

        if (exitCode != 0) {
            LOGGER.warning("Failed to list running containers: " + output);
            return containers;
        }

        if (output.trim().isEmpty()) {
            return containers;
        }

        for (String line : output.split("\n")) {
            String containerName = line.trim();
            if (!containerName.isEmpty()) {
                containers.add(containerName);
            }
        }

        return containers;
    }

    /**
     * Start a container for a session
     * This is a simplified version that uses the SQLite queue mode only
     */
    private void startContainerForSession(String sessionId, String userId) throws IOException {
        LOGGER.info("[SESSION:" + sessionId + "] Starting Podman container for klawed agent");

        String containerName = "klawed-" + sessionId;

        // Construct workspace directory path
        // This matches SessionManager's logic: persistRoot/{userId}/
        Path workspaceDir = Path.of(persistRoot).resolve(userId);

        // Ensure workspace directory exists
        if (!Files.exists(workspaceDir)) {
            Files.createDirectories(workspaceDir);
            LOGGER.info("[SESSION:" + sessionId + "] Created workspace directory: " + workspaceDir);
        }

        // Pre-create the .klawed/logs directory on the HOST before starting container
        Path klawedLogsDir = workspaceDir.resolve(".klawed").resolve("logs");
        try {
            Files.createDirectories(klawedLogsDir);
            LOGGER.info("[SESSION:" + sessionId + "] Created .klawed/logs directory: " + klawedLogsDir);
        } catch (IOException e) {
            LOGGER.warning("[SESSION:" + sessionId + "] Failed to create .klawed/logs directory: " + e.getMessage());
        }

        // Determine SQLite database path (in separate temp directory, not workspace)
        String dbFileName = "klawed_messages_" + sessionId + ".db";
        Path sqliteDbPath = Path.of(sqliteQueueDbDir).resolve(dbFileName);

        // Seed the conversation with previous chat messages before starting the container
        // This allows klawed to have context when the user resumes a session
        seedConversation(sessionId, sqliteDbPath);

        // Build podman run command
        List<String> command = buildPodmanRunCommand(containerName, workspaceDir, sqliteDbPath.toString(), sessionId);

        LOGGER.info("[SESSION:" + sessionId + "] Podman command: " + String.join(" ", command));

        ProcessBuilder processBuilder = new ProcessBuilder(command);
        processBuilder.redirectErrorStream(true);

        Process process = processBuilder.start();

        String output;
        int exitCode;
        try {
            output = readProcessOutput(process);
            exitCode = process.waitFor();
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
            throw new IOException("Interrupted while starting container", e);
        }

        if (exitCode != 0) {
            throw new IOException("Failed to start container, exit code: " + exitCode + ", output: " + output);
        }

        // First line should be the container ID
        String containerId = output.lines().findFirst().orElse("").trim();

        if (containerId.isBlank()) {
            throw new IOException("Failed to get container ID after starting container");
        }

        LOGGER.info("[SESSION:" + sessionId + "] Started container: " + containerId + " (name: " + containerName + ")");
    }

    /**
     * Build the podman run command
     */
    private List<String> buildPodmanRunCommand(String containerName, Path workspaceDir, String sqliteDbPath, String sessionId) {
        List<String> command = new ArrayList<>();

        command.add("podman");
        command.add("run");

        // Detached mode
        command.add("-d");

        // Container name for easy management
        command.add("--name");
        command.add(containerName);

        // Use keep-id to map container UID to host UID
        command.add("--userns=keep-id");

        // Network access
        command.add("--network=bridge");

        // Tmpfs for /tmp
        command.add("--tmpfs");
        command.add("/tmp:rw,size=1g");

        // Resource limits
        command.add("--memory=" + memoryLimit);
        command.add("--cpus=" + cpuLimit);
        command.add("--pids-limit=" + pidsLimit);

        // Auto-remove container on exit
        command.add("--rm");

        // Mount workspace directory
        command.add("-v");
        command.add(workspaceDir.toAbsolutePath() + ":/workspace");

        // Mount klawed messages directory to /tmp/klawed-messages in container
        // This keeps the DB files separate and they'll be auto-cleaned when container stops
        Path dbPath = Path.of(sqliteDbPath);
        Path dbDir = dbPath.getParent();
        command.add("-v");
        command.add(dbDir.toAbsolutePath() + ":/tmp/klawed-messages:rw");

        // Working directory inside container
        command.add("-w");
        command.add("/workspace");

        // Add environment variables
        addEnvironmentVariables(command);

        // Image name
        command.add(podmanImage);

        // Klawed arguments (SQLite queue mode only)
        // DB file is mounted in /tmp/klawed-messages/ inside container
        String dbFileName = dbPath.getFileName().toString();
        String containerDbPath = "/tmp/klawed-messages/" + dbFileName;

        command.add("--sqlite-queue");
        command.add(containerDbPath);

        return command;
    }

    /**
     * Add environment variables to the podman command
     */
    private void addEnvironmentVariables(List<String> command) {
        // Use --env-file to load variables from the configured .env file
        Path envFile = Path.of(envFilePath);
        if (Files.exists(envFile)) {
            command.add("--env-file");
            command.add(envFilePath);
            LOGGER.fine("Using env file: " + envFilePath);
        } else {
            LOGGER.warning("Environment file not found: " + envFilePath);
        }

        // LD_LIBRARY_PATH for shared libraries
        command.add("-e");
        command.add("LD_LIBRARY_PATH=/usr/local/lib");

        // Set HOME so klawed can create .klawed directory for logs
        command.add("-e");
        command.add("HOME=/workspace");

        // Set KLAWED_DATA_DIR to /tmp/.klawed so klawed files don't persist in workspace
        // This keeps .klawed directory in the container's tmpfs mount (requires klawed >= 0.19.2)
        command.add("-e");
        command.add("KLAWED_DATA_DIR=/tmp/.klawed");
    }

    /**
     * Stop a container gracefully
     */
    public void stopContainer(String containerId) throws IOException {
        LOGGER.info("Stopping container: " + containerId);

        if (!isContainerRunning(containerId)) {
            LOGGER.info("Container not running: " + containerId);
            removeContainerFromPodman(containerId);
            return;
        }

        try {
            ProcessBuilder pb = new ProcessBuilder("podman", "stop", "-t", "10", containerId);
            pb.redirectErrorStream(true);
            Process process = pb.start();

            String output = readProcessOutput(process);
            int exitCode = process.waitFor();

            if (exitCode != 0) {
                LOGGER.warning("Container stop returned exit code " + exitCode + ": " + output);
            } else {
                LOGGER.info("Container stopped: " + containerId);
            }
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
            throw new IOException("Interrupted while stopping container", e);
        }

        // Remove the container after stopping it
        removeContainerFromPodman(containerId);
    }

    /**
     * Remove a container from Podman
     */
    private void removeContainerFromPodman(String containerId) {
        try {
            ProcessBuilder pb = new ProcessBuilder("podman", "rm", "-f", containerId);
            pb.redirectErrorStream(true);
            Process process = pb.start();

            String output = readProcessOutput(process);
            int exitCode = process.waitFor();

            if (exitCode != 0) {
                LOGGER.fine("Container remove returned exit code " + exitCode + ": " + output);
            } else {
                LOGGER.info("Container removed from Podman: " + containerId);
            }
        } catch (IOException | InterruptedException e) {
            LOGGER.warning("Failed to remove container " + containerId + ": " + e.getMessage());
        }
    }

    /**
     * Check if a container is currently running
     */
    public boolean isContainerRunning(String containerId) {
        try {
            ProcessBuilder pb = new ProcessBuilder(
                "podman", "inspect", "-f", "{{.State.Running}}", containerId
            );
            pb.redirectErrorStream(true);
            Process process = pb.start();

            String output = readProcessOutput(process).trim();
            int exitCode = process.waitFor();

            if (exitCode != 0) {
                return false;
            }

            return "true".equalsIgnoreCase(output);
        } catch (IOException | InterruptedException e) {
            LOGGER.warning("Error checking container status: " + e.getMessage());
            return false;
        }
    }

    /**
     * Check if Podman is available on the system
     */
    public boolean isPodmanAvailable() {
        try {
            ProcessBuilder pb = new ProcessBuilder("podman", "--version");
            pb.redirectErrorStream(true);
            Process process = pb.start();

            int exitCode = process.waitFor();
            return exitCode == 0;
        } catch (IOException | InterruptedException e) {
            return false;
        }
    }

    /**
     * Get Podman version information
     */
    public String getPodmanVersion() throws IOException {
        try {
            ProcessBuilder pb = new ProcessBuilder("podman", "--version");
            pb.redirectErrorStream(true);
            Process process = pb.start();

            String output = readProcessOutput(process);
            int exitCode = process.waitFor();

            if (exitCode != 0) {
                throw new IOException("Failed to get podman version");
            }

            return output.trim();
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
            throw new IOException("Interrupted while getting podman version", e);
        }
    }

    /**
     * Read all output from a process
     */
    private String readProcessOutput(Process process) throws IOException {
        try (BufferedReader reader = new BufferedReader(new InputStreamReader(process.getInputStream()))) {
            return reader.lines().collect(Collectors.joining("\n"));
        }
    }

    /**
     * Seed the conversation with previous chat messages.
     * This allows klawed to have context when the user resumes a session.
     * Messages are inserted with sent=1 so klawed reads them at startup.
     *
     * @param sessionId The session ID
     * @param sqliteDbPath Path to the klawed messages SQLite database
     */
    private void seedConversation(String sessionId, Path sqliteDbPath) {
        LOGGER.info("[SESSION:" + sessionId + "] Seeding conversation from chat history");

        try {
            // Get the last MAX_SEED_MESSAGES from the chat database
            List<ChatMessageRecord> messages = fileChatService.findMessagesBySession(sessionId);

            if (messages == null || messages.isEmpty()) {
                LOGGER.info("[SESSION:" + sessionId + "] No previous messages to seed");
                return;
            }

            // Take only the last MAX_SEED_MESSAGES
            int startIndex = Math.max(0, messages.size() - MAX_SEED_MESSAGES);
            List<ChatMessageRecord> messagesToSeed = messages.subList(startIndex, messages.size());

            LOGGER.info("[SESSION:" + sessionId + "] Found " + messages.size() + " messages, seeding last " + messagesToSeed.size());

            // Ensure the database directory exists
            Path dbDir = sqliteDbPath.getParent();
            if (dbDir != null && !Files.exists(dbDir)) {
                Files.createDirectories(dbDir);
            }

            // Connect to the SQLite queue database and insert seed messages
            String jdbcUrl = "jdbc:sqlite:" + sqliteDbPath.toString();
            try (Connection conn = DriverManager.getConnection(jdbcUrl)) {
                // Set pragmas
                try (Statement stmt = conn.createStatement()) {
                    stmt.execute("PRAGMA journal_mode = WAL;");
                    stmt.execute("PRAGMA synchronous = NORMAL;");
                    stmt.execute("PRAGMA busy_timeout = 5000;");
                }

                // Create messages table if it doesn't exist
                try (Statement stmt = conn.createStatement()) {
                    stmt.execute(
                        "CREATE TABLE IF NOT EXISTS messages (" +
                        "id INTEGER PRIMARY KEY AUTOINCREMENT," +
                        "sender TEXT NOT NULL," +
                        "receiver TEXT NOT NULL," +
                        "message TEXT NOT NULL," +
                        "sent INTEGER DEFAULT 0," +
                        "created_at INTEGER DEFAULT (strftime('%s', 'now'))," +
                        "updated_at INTEGER DEFAULT (strftime('%s', 'now'))" +
                        ");"
                    );
                    stmt.execute("CREATE INDEX IF NOT EXISTS idx_messages_sender ON messages(sender, sent);");
                    stmt.execute("CREATE INDEX IF NOT EXISTS idx_messages_receiver ON messages(receiver, sent);");
                }

                // Insert seed messages with sent=1 (so klawed reads them at startup)
                String insertSql = "INSERT INTO messages (sender, receiver, message, sent, created_at) VALUES (?, ?, ?, 1, ?)";
                try (PreparedStatement pstmt = conn.prepareStatement(insertSql)) {
                    int seededCount = 0;

                    for (ChatMessageRecord msg : messagesToSeed) {
                        // Skip non-text messages (tool results, status, etc.)
                        String msgType = msg.getMessageType();
                        if (msgType != null && !ChatConstants.DB_MESSAGE_TYPE_TEXT.equals(msgType)) {
                            continue;
                        }

                        // Skip empty messages
                        String content = msg.getContent();
                        if (content == null || content.trim().isEmpty()) {
                            continue;
                        }

                        // Determine sender/receiver for klawed's perspective
                        // In chat_message: sender="client" means user, sender="agent" means klawed
                        // In klawed queue: sender="client" means user message, sender="klawed" means assistant
                        String sender;
                        String receiver;
                        if (ChatConstants.CLIENT.equals(msg.getSender())) {
                            // User message: client -> klawed
                            sender = "client";
                            receiver = "klawed";
                        } else if (ChatConstants.AGENT.equals(msg.getSender())) {
                            // Assistant message: klawed -> client
                            sender = "klawed";
                            receiver = "client";
                        } else {
                            // Skip system or other messages
                            continue;
                        }

                        // Create JSON message format
                        ObjectNode json = objectMapper.createObjectNode();
                        json.put("messageType", "TEXT");
                        json.put("content", content);
                        String jsonMessage = objectMapper.writeValueAsString(json);

                        // Get timestamp (use epoch seconds)
                        long createdAt = msg.getCreatedAt() != null ?
                            msg.getCreatedAt().atZone(java.time.ZoneId.systemDefault()).toEpochSecond() :
                            Instant.now().getEpochSecond();

                        pstmt.setString(1, sender);
                        pstmt.setString(2, receiver);
                        pstmt.setString(3, jsonMessage);
                        pstmt.setLong(4, createdAt);
                        pstmt.executeUpdate();
                        seededCount++;
                    }

                    LOGGER.info("[SESSION:" + sessionId + "] Seeded " + seededCount + " TEXT messages for conversation context");
                }
            }

        } catch (Exception e) {
            // Log error but don't fail container startup - seeding is best-effort
            LOGGER.warning("[SESSION:" + sessionId + "] Failed to seed conversation: " + e.getMessage());
            e.printStackTrace();
        }
    }

    /**
     * Stop all containers on shutdown
     */
    @PreDestroy
    public void shutdown() {
        LOGGER.info("KlawedSandboxService shutting down");

        // Set shutdown flag first to prevent scheduled task from interfering
        shuttingDown.set(true);

        if (podmanEnabled) {
            try {
                List<String> runningContainers = listRunningKlawedContainers();
                for (String containerName : runningContainers) {
                    try {
                        stopContainer(containerName);
                    } catch (IOException e) {
                        LOGGER.warning("Failed to stop container " + containerName + ": " + e.getMessage());
                    }
                }
            } catch (Exception e) {
                LOGGER.severe("Error stopping containers on shutdown: " + e.getMessage());
            }
        }

        LOGGER.info("KlawedSandboxService shutdown complete");
    }

    /**
     * Session record
     */
    private static class SessionRecord {
        final String sessionId;
        final String userId;
        final long registeredAt;
        final long lastActiveAt;

        SessionRecord(String sessionId, String userId, long registeredAt, long lastActiveAt) {
            this.sessionId = sessionId;
            this.userId = userId;
            this.registeredAt = registeredAt;
            this.lastActiveAt = lastActiveAt;
        }
    }
}
