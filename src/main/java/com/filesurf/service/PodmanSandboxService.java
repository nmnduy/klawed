package com.filesurf.service;

import jakarta.annotation.PreDestroy;
import jakarta.enterprise.context.ApplicationScoped;
import jakarta.inject.Inject;
import org.eclipse.microprofile.config.inject.ConfigProperty;

import java.io.BufferedReader;
import java.io.IOException;
import java.io.InputStreamReader;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;
import java.util.Optional;
import java.util.logging.Logger;
import java.util.stream.Collectors;

/**
 * Service for managing Podman containers that run Klawed agents in isolated sandboxes.
 * 
 * This provides security isolation for running AI agents in production environments.
 * Each container is named klawed-{sessionId} for easy tracking and management.
 * 
 * Container state is persisted in a separate SQLite database (via ContainerTrackingService)
 * to survive application restarts and avoid in-memory state sync issues.
 * 
 * Container features:
 * - Uses --userns=keep-id to map container UID to host UID (avoids permission issues)
 * - tmpfs for /tmp
 * - Resource limits (memory, CPU, PIDs)
 * - Network access for agent to call APIs and download packages
 * - Workspace directory mounted for file read/write operations
 */
@ApplicationScoped
public class PodmanSandboxService {

    private static final Logger LOGGER = Logger.getLogger(PodmanSandboxService.class.getName());
    
    @Inject
    ContainerTrackingService containerTrackingService;
    
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
    
    @ConfigProperty(name = "klawed.communication.mode", defaultValue = "unix-socket")
    String communicationMode;
    
    @ConfigProperty(name = "klawed.unix-socket.filename", defaultValue = "klawed.sock")
    String unixSocketFilename;
    
    @ConfigProperty(name = "sandbox.podman.env-file", defaultValue = "/etc/filesurf/klawed.env")
    String envFilePath;
    
    /**
     * Validate configuration on startup
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
            
            // Scan for existing klawed containers and recover tracking state OR clean them up
            // This handles the case where the application restarted while containers were running
            recoverOrCleanupExistingContainers();
        }
    }
    
    /**
     * Scan for existing klawed containers on startup and recover or clean them up.
     * 
     * Uses the container tracking database to determine which containers should still be running.
     * For each container tracked as 'running' in the database:
     * - If the container is healthy: keep it (database already tracks it)
     * - If the container is not healthy: update database status to 'died'
     * 
     * Also detects orphaned containers (running but not in database) and stops them.
     */
    private void recoverOrCleanupExistingContainers() {
        LOGGER.info("Scanning for existing klawed containers on startup...");
        
        // First, verify containers from the database are still healthy
        int recoveredCount = 0;
        int markedDeadCount = 0;
        
        List<ContainerTrackingService.ContainerRecord> dbRunningContainers = 
            containerTrackingService.getRunningContainers();
        
        for (ContainerTrackingService.ContainerRecord record : dbRunningContainers) {
            String containerName = record.containerName;
            String sessionId = record.sessionId;
            
            if (isContainerRunning(containerName)) {
                // Container is healthy - database already has correct state
                recoveredCount++;
                LOGGER.info("[SESSION:" + sessionId + "] Recovered healthy container: " + containerName);
            } else {
                // Container died while app was down - update database
                containerTrackingService.recordContainerStop(sessionId, "died");
                markedDeadCount++;
                LOGGER.info("[SESSION:" + sessionId + "] Container no longer running, marked as died: " + containerName);
            }
        }
        
        if (recoveredCount > 0) {
            LOGGER.info("Recovered " + recoveredCount + " healthy container(s) from previous session");
        }
        if (markedDeadCount > 0) {
            LOGGER.info("Marked " + markedDeadCount + " dead container(s) in database");
        }
        
        // Now check for orphaned containers (running but not in database)
        int orphanedCount = cleanupOrphanedContainersOnStartup();
        if (orphanedCount > 0) {
            LOGGER.info("Stopped " + orphanedCount + " orphaned container(s) on startup");
        }
        
        if (recoveredCount == 0 && markedDeadCount == 0 && orphanedCount == 0) {
            LOGGER.info("No existing klawed containers found on startup");
        }
    }
    
    /**
     * Clean up orphaned containers on startup.
     * These are containers that are running but not tracked in the database.
     * 
     * @return Number of orphaned containers stopped
     */
    private int cleanupOrphanedContainersOnStartup() {
        int stoppedCount = 0;
        
        // Get session IDs that are tracked as running in the database
        List<String> trackedSessionIds = containerTrackingService.getRunningSessionIds();
        
        try {
            // List all running klawed containers
            ProcessBuilder pb = new ProcessBuilder(
                "podman", "ps", "--filter", "name=klawed-", "--filter", "status=running", "--format", "{{.Names}}"
            );
            pb.redirectErrorStream(true);
            Process process = pb.start();
            
            String output = readProcessOutput(process);
            int exitCode = process.waitFor();
            
            if (exitCode != 0) {
                LOGGER.warning("Failed to list existing containers on startup: " + output);
                return 0;
            }
            
            if (output.trim().isEmpty()) {
                return 0;
            }
            
            for (String containerName : output.split("\n")) {
                containerName = containerName.trim();
                if (containerName.isEmpty()) {
                    continue;
                }
                
                // Extract session ID from container name
                String sessionId = containerName.replace("klawed-", "");
                
                // If not tracked in database, it's orphaned
                if (!trackedSessionIds.contains(sessionId)) {
                    LOGGER.info("Found orphaned container on startup: " + containerName + ", stopping it...");
                    try {
                        stopContainer(containerName);
                        stoppedCount++;
                    } catch (IOException e) {
                        LOGGER.warning("Failed to stop orphaned container " + containerName + ": " + e.getMessage());
                        try {
                            killContainer(containerName);
                            stoppedCount++;
                        } catch (IOException killEx) {
                            LOGGER.severe("Failed to kill orphaned container " + containerName + ": " + killEx.getMessage());
                        }
                    }
                }
            }
        } catch (IOException | InterruptedException e) {
            LOGGER.warning("Error scanning for orphaned containers on startup: " + e.getMessage());
        }
        
        return stoppedCount;
    }
    
    /**
     * Check if Podman sandbox mode is enabled
     */
    public boolean isEnabled() {
        return podmanEnabled;
    }
    
    /**
     * Start a Podman container for a Klawed agent session.
     * 
     * @param sessionId The session ID (used for container naming)
     * @param workspaceDir The workspace directory to mount into the container
     * @param sqliteDbPath The SQLite database path for the klawed queue (only used in SQLite mode)
     * @return The container ID
     * @throws IOException If container creation fails
     */
    public String startContainer(String sessionId, Path workspaceDir, String sqliteDbPath) throws IOException {
        LOGGER.info("[SESSION:" + sessionId + "] Starting Podman container for klawed agent");
        
        String containerName = "klawed-" + sessionId;
        
        // Check if container already exists
        if (isContainerRunning(containerName)) {
            LOGGER.warning("[SESSION:" + sessionId + "] Container already running: " + containerName);
            return containerName;
        }
        
        // Pre-create the .klawed/logs directory on the HOST before starting container
        // This ensures the directory exists with correct host permissions before any container operations
        Path klawedLogsDir = workspaceDir.resolve(".klawed").resolve("logs");
        try {
            java.nio.file.Files.createDirectories(klawedLogsDir);
            LOGGER.info("[SESSION:" + sessionId + "] Created .klawed/logs directory on host: " + klawedLogsDir);
        } catch (IOException e) {
            LOGGER.warning("[SESSION:" + sessionId + "] Failed to create .klawed/logs directory on host: " + e.getMessage());
            // Continue anyway - the container shell command will try again
        }
        
        // Build podman run command with security options
        List<String> command = buildPodmanRunCommand(containerName, workspaceDir, sqliteDbPath, sessionId);
        
        LOGGER.info("[SESSION:" + sessionId + "] Podman command: " + obfuscateCommand(command));
        
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
        String containerId = Optional.ofNullable(output)
            .map(String::strip)
            .map(s -> s.contains("\n") ? s.substring(0, s.indexOf('\n')).strip() : s)
            .orElse("");

        if (containerId.isBlank()) {
            throw new IOException("Failed to get container ID after starting container (output was empty)");
        }

        // Track in persistent database (source of truth)
        containerTrackingService.recordContainerStart(sessionId, containerId, containerName, podmanImage);
        
        LOGGER.info("[SESSION:" + sessionId + "] Started container: " + containerId + " (name: " + containerName + ")");
        
        return containerId;
    }
    
    /**
     * List of environment variable names that contain secrets and should be obfuscated in logs.
     */
    private static final List<String> SECRET_ENV_VARS = List.of(
        "OPENAI_API_KEY",
        "ANTHROPIC_API_KEY",
        "AWS_SECRET_ACCESS_KEY",
        "AWS_SESSION_TOKEN",
        "OPENAI_AUTH_HEADER",
        "OPENAI_EXTRA_HEADERS"
    );
    
    /**
     * Obfuscate secrets in a podman command for safe logging.
     * Replaces sensitive environment variable values with "[REDACTED]".
     */
    private String obfuscateCommand(List<String> command) {
        List<String> obfuscated = new ArrayList<>();
        for (int i = 0; i < command.size(); i++) {
            String arg = command.get(i);
            boolean isSecret = false;
            
            // Check if this argument is a secret environment variable (format: VAR_NAME=value)
            for (String secretVar : SECRET_ENV_VARS) {
                if (arg.startsWith(secretVar + "=")) {
                    obfuscated.add(secretVar + "=[REDACTED]");
                    isSecret = true;
                    break;
                }
            }
            
            if (!isSecret) {
                obfuscated.add(arg);
            }
        }
        return String.join(" ", obfuscated);
    }
    
    /**
     * Build the podman run command with all security options and environment variables.
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
        
        // Use keep-id to map container UID to host UID (avoids permission issues with shared volumes)
        command.add("--userns=keep-id");
        
        // Network access (bridge allows outbound connections for API calls)
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
        
        // Mount workspace directory (no SELinux relabeling needed when running as root)
        command.add("-v");
        command.add(workspaceDir.toAbsolutePath() + ":/workspace");
        
        // If using shared SQLite database outside workspace, mount its parent directory
        if (sqliteDbPath != null && !sqliteDbPath.isEmpty()) {
            Path dbPath = Path.of(sqliteDbPath);
            if (!dbPath.isAbsolute()) {
                // Convert relative path to absolute relative to current directory
                dbPath = Path.of("").toAbsolutePath().resolve(dbPath);
            }
            
            // Check if database is outside workspace directory
            if (!dbPath.normalize().startsWith(workspaceDir.toAbsolutePath().normalize())) {
                // Mount the parent directory of the database
                Path dbParentDir = dbPath.getParent();
                if (dbParentDir != null && Files.exists(dbParentDir)) {
                    command.add("-v");
                    command.add(dbParentDir.toAbsolutePath() + ":" + dbParentDir.toAbsolutePath());
                    LOGGER.info("[SESSION:" + sessionId + "] Mounted shared database directory: " + dbParentDir);
                }
            }
        }
        
        // Note: klawed binary is baked into the container image (v1.2+)
        // Note: libmemvid_ffi.so is baked into the container image (v1.1+)
        // No need to mount them separately
        
        // Working directory inside container
        command.add("-w");
        command.add("/workspace");
        
        // Add environment variables
        addEnvironmentVariables(command);
        
        // Image name
        command.add(podmanImage);
        
        // Add klawed arguments (ENTRYPOINT is already set in Dockerfile to /usr/local/bin/klawed)
        // Just pass the appropriate arguments based on communication mode
        if ("unix-socket".equals(communicationMode)) {
            String socketPath = "/workspace/" + unixSocketFilename;
            command.add("-u");
            command.add(socketPath);
        } else {
            // In podman, the working directory is /workspace, so ensure the db path is relative to /workspace
            // Convert host path to container path if database is inside workspace
            String normalizedDbPath = sqliteDbPath;
            if (normalizedDbPath != null) {
                Path dbPath = Path.of(normalizedDbPath);
                if (dbPath.isAbsolute()) {
                    // Check if database is inside workspace directory
                    Path workspaceAbs = workspaceDir.toAbsolutePath().normalize();
                    Path dbAbs = dbPath.normalize();
                    if (dbAbs.startsWith(workspaceAbs)) {
                        // Convert host path to container path: /workspace + relative path from workspace
                        Path relativePath = workspaceAbs.relativize(dbAbs);
                        normalizedDbPath = "/workspace/" + relativePath.toString();
                    }
                }
            }
            if (normalizedDbPath == null || normalizedDbPath.isEmpty()) {
                throw new IllegalArgumentException("SQLite database path cannot be null or empty in SQLite queue mode");
            }
            command.add("--sqlite-queue");
            command.add(normalizedDbPath);
        }
        
        return command;
    }
    
    /**
     * Add environment variables to the podman command.
     * Uses --env-file to load variables from the configured .env file.
     * This prevents leftover environment variables in the JVM process from affecting klawed.
     */
    private void addEnvironmentVariables(List<String> command) {
        // Use --env-file to load variables from the configured .env file
        // This is more efficient than individual -e flags and matches how users expect .env files to work
        Path envFile = Path.of(envFilePath);
        if (Files.exists(envFile)) {
            command.add("--env-file");
            command.add(envFilePath);
            LOGGER.fine("Using env file: " + envFilePath);
        } else {
            LOGGER.warning("Environment file not found: " + envFilePath + 
                         ". Klawed containers will run without environment variables from file.");
        }
        
        // LD_LIBRARY_PATH for shared libraries (libmemvid_ffi.so)
        command.add("-e");
        command.add("LD_LIBRARY_PATH=/usr/local/lib");
        
        // Set HOME so klawed can create .klawed directory for logs
        command.add("-e");
        command.add("HOME=/workspace");
    }
    
    /**
     * Stop a container gracefully (SIGTERM, wait, then SIGKILL if needed).
     * 
     * @param containerId The container ID or name
     * @throws IOException If stopping fails
     */
    public void stopContainer(String containerId) throws IOException {
        LOGGER.info("Stopping container: " + containerId);
        
        if (!isContainerRunning(containerId)) {
            LOGGER.info("Container not running: " + containerId);
            // Still try to remove it in case it exists in stopped state
            removeContainerFromPodman(containerId);
            removeFromTracking(containerId, "stopped");
            return;
        }
        
        try {
            // Stop with 10 second timeout before SIGKILL
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
        
        // Remove the container after stopping it to avoid "name already in use" errors
        removeContainerFromPodman(containerId);
        
        removeFromTracking(containerId, "stopped");
    }
    
    /**
     * Force kill a container immediately (SIGKILL).
     * 
     * @param containerId The container ID or name
     * @throws IOException If killing fails
     */
    public void killContainer(String containerId) throws IOException {
        LOGGER.warning("Force killing container: " + containerId);
        
        try {
            ProcessBuilder pb = new ProcessBuilder("podman", "kill", containerId);
            pb.redirectErrorStream(true);
            Process process = pb.start();
            
            String output = readProcessOutput(process);
            int exitCode = process.waitFor();
            
            if (exitCode != 0) {
                // Container might already be stopped
                LOGGER.warning("Container kill returned exit code " + exitCode + ": " + output);
            } else {
                LOGGER.info("Container killed: " + containerId);
            }
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
            throw new IOException("Interrupted while killing container", e);
        }
        
        // Remove the container after killing it to avoid "name already in use" errors
        removeContainerFromPodman(containerId);
        
        removeFromTracking(containerId, "killed");
    }
    
    /**
     * Remove a container from Podman.
     * This should be called after stopping/killing a container to prevent
     * "container name already in use" errors on subsequent starts.
     * 
     * @param containerId The container ID or name
     */
    private void removeContainerFromPodman(String containerId) {
        try {
            ProcessBuilder pb = new ProcessBuilder("podman", "rm", "-f", containerId);
            pb.redirectErrorStream(true);
            Process process = pb.start();
            
            String output = readProcessOutput(process);
            int exitCode = process.waitFor();
            
            if (exitCode != 0) {
                // Container might not exist, which is fine
                LOGGER.fine("Container remove returned exit code " + exitCode + ": " + output);
            } else {
                LOGGER.info("Container removed from Podman: " + containerId);
            }
        } catch (IOException | InterruptedException e) {
            // Log but don't throw - removal is best-effort cleanup
            LOGGER.warning("Failed to remove container " + containerId + ": " + e.getMessage());
        }
    }
    
    /**
     * Check if a container is currently running.
     * 
     * @param containerId The container ID or name
     * @return true if the container is running
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
                // Container doesn't exist
                return false;
            }
            
            return "true".equalsIgnoreCase(output);
        } catch (IOException | InterruptedException e) {
            LOGGER.warning("Error checking container status: " + e.getMessage());
            return false;
        }
    }
    
    /**
     * Get logs from a container.
     * 
     * @param containerId The container ID or name
     * @return The container logs
     * @throws IOException If getting logs fails
     */
    public String getContainerLogs(String containerId) throws IOException {
        return getContainerLogs(containerId, 100);
    }
    
    /**
     * Get logs from a container with specified tail lines.
     * 
     * @param containerId The container ID or name
     * @param tailLines Number of lines to retrieve from the end
     * @return The container logs
     * @throws IOException If getting logs fails
     */
    public String getContainerLogs(String containerId, int tailLines) throws IOException {
        try {
            ProcessBuilder pb = new ProcessBuilder(
                "podman", "logs", "--tail", String.valueOf(tailLines), containerId
            );
            pb.redirectErrorStream(true);
            Process process = pb.start();
            
            String output = readProcessOutput(process);
            int exitCode = process.waitFor();
            
            if (exitCode != 0) {
                throw new IOException("Failed to get container logs, exit code: " + exitCode);
            }
            
            return output;
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
            throw new IOException("Interrupted while getting container logs", e);
        }
    }
    
    /**
     * Stop container by session ID.
     * 
     * @param sessionId The session ID
     * @throws IOException If stopping fails
     */
    public void stopContainerBySession(String sessionId) throws IOException {
        String containerName = "klawed-" + sessionId;
        stopContainer(containerName);
        // Note: stopContainer() calls removeFromTracking() which updates the database
    }
    
    /**
     * Kill container by session ID.
     * 
     * @param sessionId The session ID
     * @throws IOException If killing fails
     */
    public void killContainerBySession(String sessionId) throws IOException {
        String containerName = "klawed-" + sessionId;
        killContainer(containerName);
        // Note: killContainer() calls removeFromTracking() which updates the database
    }
    
    /**
     * Check if container is running by session ID.
     * 
     * @param sessionId The session ID
     * @return true if the container is running
     */
    public boolean isContainerRunningBySession(String sessionId) {
        String containerName = "klawed-" + sessionId;
        return isContainerRunning(containerName);
    }
    
    /**
     * Get the container name for a session if it exists and is running.
     * This is used during reconnection to detect if a container is already running
     * for the session.
     * 
     * @param sessionId The session ID
     * @return The container name (klawed-{sessionId}) if running, null otherwise
     */
    public String getRunningContainerForSession(String sessionId) {
        String containerName = "klawed-" + sessionId;
        if (isContainerRunning(containerName)) {
            // Ensure database is in sync - if container is running but not tracked, add it
            if (!containerTrackingService.hasRunningContainer(sessionId)) {
                // This shouldn't normally happen, but recover gracefully
                containerTrackingService.recordContainerStart(sessionId, containerName, containerName, podmanImage);
                LOGGER.info("[SESSION:" + sessionId + "] Re-tracking existing container in database: " + containerName);
            }
            return containerName;
        }
        return null;
    }
    
    /**
     * Get all active session IDs with running containers.
     * Queries the database and verifies each container is actually running.
     * 
     * @return List of session IDs
     */
    public List<String> getActiveSessions() {
        return containerTrackingService.getRunningSessionIds().stream()
            .filter(sessionId -> isContainerRunning("klawed-" + sessionId))
            .collect(Collectors.toList());
    }
    
    /**
     * Count running klawed containers directly from podman ps.
     * This is the authoritative count for leak detection - it shows what's actually
     * running regardless of internal tracking state.
     * 
     * @return Number of running klawed-* containers, or -1 if podman query fails
     */
    public int countRunningContainersFromPodman() {
        if (!podmanEnabled) {
            return 0;
        }
        
        try {
            // Query podman for running klawed containers only (not stopped/exited)
            ProcessBuilder pb = new ProcessBuilder(
                "podman", "ps", "--filter", "name=klawed-", "--filter", "status=running", "--format", "{{.Names}}"
            );
            pb.redirectErrorStream(true);
            Process process = pb.start();
            
            String output = readProcessOutput(process);
            int exitCode = process.waitFor();
            
            if (exitCode != 0) {
                LOGGER.warning("Failed to count running containers: " + output);
                return -1;
            }
            
            // Count non-empty lines
            if (output.trim().isEmpty()) {
                return 0;
            }
            
            return (int) output.lines()
                .map(String::trim)
                .filter(line -> !line.isEmpty())
                .count();
        } catch (IOException | InterruptedException e) {
            LOGGER.warning("Error counting running containers: " + e.getMessage());
            return -1;
        }
    }
    
    /**
     * Stop all containers managed by this service.
     * Queries the database for all running containers and stops them.
     */
    public void stopAllContainers() {
        LOGGER.info("Stopping all managed containers");
        
        List<ContainerTrackingService.ContainerRecord> runningContainers = 
            containerTrackingService.getRunningContainers();
        
        for (ContainerTrackingService.ContainerRecord record : runningContainers) {
            String sessionId = record.sessionId;
            String containerName = record.containerName;
            
            try {
                LOGGER.info("[SESSION:" + sessionId + "] Stopping container: " + containerName);
                stopContainer(containerName);
            } catch (IOException e) {
                LOGGER.warning("[SESSION:" + sessionId + "] Failed to stop container: " + e.getMessage());
                
                // Try force kill
                try {
                    killContainer(containerName);
                } catch (IOException killEx) {
                    LOGGER.severe("[SESSION:" + sessionId + "] Failed to kill container: " + killEx.getMessage());
                }
            }
        }
        
        LOGGER.info("All containers stopped");
    }
    
    /**
     * Find and clean up orphaned klawed containers (containers not tracked in the database).
     * 
     * Only checks RUNNING containers since we use --rm flag which auto-removes stopped containers.
     * An orphaned container is one that:
     * - Has the klawed- prefix
     * - Is currently running
     * - Is NOT tracked in the database
     * 
     * This can happen when:
     * - The application restarts while containers are running
     * - Race conditions during disconnect/reconnect
     * - Bugs in tracking logic
     * 
     * @return Number of orphaned containers cleaned up
     */
    public int cleanupOrphanedContainers() {
        LOGGER.fine("Checking for orphaned klawed containers");
        int cleanedUp = 0;
        
        // Get session IDs that are tracked as running in the database
        List<String> trackedSessionIds = containerTrackingService.getRunningSessionIds();
        
        try {
            // List only RUNNING containers with klawed- prefix
            // (stopped containers should auto-remove due to --rm flag)
            ProcessBuilder pb = new ProcessBuilder(
                "podman", "ps", "--filter", "name=klawed-", "--filter", "status=running", "--format", "{{.Names}}"
            );
            pb.redirectErrorStream(true);
            Process process = pb.start();
            
            String output = readProcessOutput(process);
            int exitCode = process.waitFor();
            
            if (exitCode != 0) {
                LOGGER.warning("Failed to list running containers: " + output);
                return 0;
            }
            
            if (output.trim().isEmpty()) {
                LOGGER.fine("No running klawed containers found");
                return 0;
            }
            
            for (String containerName : output.split("\n")) {
                containerName = containerName.trim();
                if (containerName.isEmpty()) {
                    continue;
                }
                
                // Extract session ID from container name
                String sessionId = containerName.replace("klawed-", "");
                
                // If not tracked in database, it's orphaned
                if (!trackedSessionIds.contains(sessionId)) {
                    LOGGER.info("Found orphaned running container: " + containerName + " (not in database)");
                    try {
                        stopContainer(containerName);
                        cleanedUp++;
                    } catch (IOException e) {
                        LOGGER.warning("Failed to stop orphaned container " + containerName + ": " + e.getMessage());
                        try {
                            killContainer(containerName);
                            cleanedUp++;
                        } catch (IOException killEx) {
                            LOGGER.severe("Failed to kill orphaned container: " + killEx.getMessage());
                        }
                    }
                }
            }
        } catch (IOException | InterruptedException e) {
            LOGGER.warning("Error during orphaned container cleanup: " + e.getMessage());
        }
        
        if (cleanedUp > 0) {
            LOGGER.info("Cleaned up " + cleanedUp + " orphaned container(s)");
        }
        return cleanedUp;
    }
    
    /**
     * Get container statistics (CPU, memory usage).
     * 
     * @param containerId The container ID or name
     * @return Container stats as a formatted string
     * @throws IOException If getting stats fails
     */
    public String getContainerStats(String containerId) throws IOException {
        try {
            ProcessBuilder pb = new ProcessBuilder(
                "podman", "stats", "--no-stream", "--format",
                "CPU: {{.CPUPerc}}, Memory: {{.MemUsage}}, PIDs: {{.PIDs}}",
                containerId
            );
            pb.redirectErrorStream(true);
            Process process = pb.start();
            
            String output = readProcessOutput(process);
            int exitCode = process.waitFor();
            
            if (exitCode != 0) {
                throw new IOException("Failed to get container stats, exit code: " + exitCode);
            }
            
            return output.trim();
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
            throw new IOException("Interrupted while getting container stats", e);
        }
    }
    
    /**
     * Execute a command inside a running container.
     * 
     * @param containerId The container ID or name
     * @param command The command to execute
     * @return The command output
     * @throws IOException If execution fails
     */
    public String execInContainer(String containerId, String... command) throws IOException {
        try {
            List<String> fullCommand = new ArrayList<>();
            fullCommand.add("podman");
            fullCommand.add("exec");
            fullCommand.add(containerId);
            for (String arg : command) {
                fullCommand.add(arg);
            }
            
            ProcessBuilder pb = new ProcessBuilder(fullCommand);
            pb.redirectErrorStream(true);
            Process process = pb.start();
            
            String output = readProcessOutput(process);
            int exitCode = process.waitFor();
            
            if (exitCode != 0) {
                throw new IOException("Command failed with exit code " + exitCode + ": " + output);
            }
            
            return output;
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
            throw new IOException("Interrupted while executing command in container", e);
        }
    }
    
    /**
     * Remove container from tracking by container ID or name.
     * Updates the persistent database (source of truth).
     * 
     * @param containerId The container ID or name
     * @param status The stop status ('stopped' or 'killed')
     */
    private void removeFromTracking(String containerId, String status) {
        // Update database status (source of truth)
        containerTrackingService.recordContainerStopByContainerId(containerId, status);
    }
    
    /**
     * Read all output from a process.
     */
    private String readProcessOutput(Process process) throws IOException {
        try (BufferedReader reader = new BufferedReader(new InputStreamReader(process.getInputStream()))) {
            return reader.lines().collect(Collectors.joining("\n"));
        }
    }
    
    /**
     * Check if Podman is available on the system.
     * 
     * @return true if podman command is available
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
     * Get Podman version information.
     * 
     * @return Podman version string
     * @throws IOException If getting version fails
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
     * Cleanup on application shutdown.
     */
    @PreDestroy
    public void shutdown() {
        LOGGER.info("PodmanSandboxService shutting down");
        stopAllContainers();
        LOGGER.info("PodmanSandboxService shutdown complete");
    }
}
