package com.filesurf.service;

import jakarta.annotation.PreDestroy;
import jakarta.enterprise.context.ApplicationScoped;
import org.eclipse.microprofile.config.inject.ConfigProperty;

import java.io.BufferedReader;
import java.io.IOException;
import java.io.InputStreamReader;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.TimeUnit;
import java.util.logging.Logger;
import java.util.stream.Collectors;

/**
 * Service for managing Podman containers that run Klawed agents in isolated sandboxes.
 * 
 * This provides security isolation for running AI agents in production environments.
 * Each container is named klawed-{sessionId} for easy tracking and management.
 * 
 * Container security features:
 * - no-new-privileges: Prevents privilege escalation
 * - cap-drop=ALL: Drops all Linux capabilities
 * - tmpfs for /tmp with noexec
 * - Resource limits (memory, CPU, PIDs)
 * - Network access for agent to call APIs and download packages
 * - Workspace directory mounted for file read/write operations
 */
@ApplicationScoped
public class PodmanSandboxService {

    private static final Logger LOGGER = Logger.getLogger(PodmanSandboxService.class.getName());
    
    // Track active containers by session ID
    private final ConcurrentHashMap<String, String> sessionContainers = new ConcurrentHashMap<>();
    
    // Configuration properties
    @ConfigProperty(name = "sandbox.podman.enabled", defaultValue = "false")
    boolean podmanEnabled;
    
    @ConfigProperty(name = "sandbox.podman.image", defaultValue = "klawed-sandbox:latest")
    String podmanImage;
    
    @ConfigProperty(name = "sandbox.podman.memory", defaultValue = "2g")
    String memoryLimit;
    
    @ConfigProperty(name = "sandbox.podman.cpus", defaultValue = "2")
    String cpuLimit;
    
    @ConfigProperty(name = "sandbox.podman.pids-limit", defaultValue = "512")
    int pidsLimit;
    
    @ConfigProperty(name = "klawed.path", defaultValue = "/usr/local/bin/klawed")
    String klawedPath;
    
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
     * @param sqliteDbPath The SQLite database path for the klawed queue
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
        
        // Build podman run command with security options
        List<String> command = buildPodmanRunCommand(containerName, workspaceDir, sqliteDbPath);
        
        LOGGER.info("[SESSION:" + sessionId + "] Podman command: " + String.join(" ", command));
        
        ProcessBuilder processBuilder = new ProcessBuilder(command);
        processBuilder.redirectErrorStream(true);
        
        Process process = processBuilder.start();
        
        // Read container ID from stdout
        String containerId;
        try (BufferedReader reader = new BufferedReader(new InputStreamReader(process.getInputStream()))) {
            containerId = reader.readLine();
        }
        
        try {
            int exitCode = process.waitFor();
            if (exitCode != 0) {
                // Read any remaining output for error message
                String errorOutput = new String(process.getInputStream().readAllBytes());
                throw new IOException("Failed to start container, exit code: " + exitCode + 
                                     ", output: " + errorOutput);
            }
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
            throw new IOException("Interrupted while starting container", e);
        }
        
        if (containerId == null || containerId.isBlank()) {
            throw new IOException("Failed to get container ID after starting container");
        }
        
        containerId = containerId.trim();
        sessionContainers.put(sessionId, containerId);
        
        LOGGER.info("[SESSION:" + sessionId + "] Started container: " + containerId + " (name: " + containerName + ")");
        
        return containerId;
    }
    
    /**
     * Build the podman run command with all security options and environment variables.
     */
    private List<String> buildPodmanRunCommand(String containerName, Path workspaceDir, String sqliteDbPath) {
        List<String> command = new ArrayList<>();
        
        command.add("podman");
        command.add("run");
        
        // Detached mode
        command.add("-d");
        
        // Container name for easy management
        command.add("--name");
        command.add(containerName);
        
        // Security options
        command.add("--security-opt=no-new-privileges");
        command.add("--cap-drop=ALL");
        // Note: NOT using --read-only because agent needs to create/edit files in workspace
        
        // Network access (bridge allows outbound connections for API calls)
        command.add("--network=bridge");
        
        // Tmpfs for /tmp with restrictions
        command.add("--tmpfs");
        command.add("/tmp:rw,noexec,nosuid,size=1g");
        
        // Resource limits
        command.add("--memory=" + memoryLimit);
        command.add("--cpus=" + cpuLimit);
        command.add("--pids-limit=" + pidsLimit);
        
        // Auto-remove container on exit
        command.add("--rm");
        
        // Mount workspace directory
        command.add("-v");
        command.add(workspaceDir.toAbsolutePath() + ":/workspace:Z");
        
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
        
        // Klawed command inside container
        command.add("/usr/local/bin/klawed");
        command.add("--sqlite-queue");
        command.add(sqliteDbPath);
        
        return command;
    }
    
    /**
     * Add environment variables to the podman command.
     */
    private void addEnvironmentVariables(List<String> command) {
        // OpenAI/LLM API configuration
        addEnvIfPresent(command, "OPENAI_API_KEY");
        addEnvIfPresent(command, "OPENAI_API_BASE");
        addEnvIfPresent(command, "OPENAI_MODEL");
        
        // Proxy settings
        addEnvIfPresent(command, "HTTPS_PROXY");
        addEnvIfPresent(command, "HTTP_PROXY");
        addEnvIfPresent(command, "https_proxy");
        addEnvIfPresent(command, "http_proxy");
        addEnvIfPresent(command, "NO_PROXY");
        addEnvIfPresent(command, "no_proxy");
        
        // AWS/Bedrock environment variables
        addEnvIfPresent(command, "AWS_REGION");
        addEnvIfPresent(command, "AWS_PROFILE");
        addEnvIfPresent(command, "AWS_ACCESS_KEY_ID");
        addEnvIfPresent(command, "AWS_SECRET_ACCESS_KEY");
        addEnvIfPresent(command, "AWS_SESSION_TOKEN");
        addEnvIfPresent(command, "AWS_DEFAULT_REGION");
        
        // Anthropic/Bedrock specific
        addEnvIfPresent(command, "KLAWED_USE_BEDROCK");
        addEnvIfPresent(command, "ANTHROPIC_MODEL");
        addEnvIfPresent(command, "ANTHROPIC_VERSION");
        addEnvIfPresent(command, "ANTHROPIC_API_KEY");
        
        // Extra headers for custom API endpoints
        addEnvIfPresent(command, "OPENAI_AUTH_HEADER");
        addEnvIfPresent(command, "OPENAI_EXTRA_HEADERS");
        
        // LD_LIBRARY_PATH for shared libraries (libmemvid_ffi.so)
        command.add("-e");
        command.add("LD_LIBRARY_PATH=/usr/local/lib");
        
        // Always enable verbose tool output for debugging
        command.add("-e");
        command.add("KLAWED_TOOL_VERBOSE=1");
    }
    
    /**
     * Add environment variable to command if it exists in the system environment.
     */
    private void addEnvIfPresent(List<String> command, String envName) {
        String value = System.getenv(envName);
        if (value != null && !value.isBlank()) {
            command.add("-e");
            command.add(envName + "=" + value);
        }
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
            removeFromTracking(containerId);
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
        
        removeFromTracking(containerId);
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
        
        removeFromTracking(containerId);
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
        sessionContainers.remove(sessionId);
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
        sessionContainers.remove(sessionId);
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
     * Get all active session IDs with running containers.
     * 
     * @return List of session IDs
     */
    public List<String> getActiveSessions() {
        return sessionContainers.entrySet().stream()
            .filter(entry -> isContainerRunning(entry.getValue()))
            .map(Map.Entry::getKey)
            .collect(Collectors.toList());
    }
    
    /**
     * Stop all containers managed by this service.
     */
    public void stopAllContainers() {
        LOGGER.info("Stopping all managed containers");
        
        for (Map.Entry<String, String> entry : sessionContainers.entrySet()) {
            String sessionId = entry.getKey();
            String containerId = entry.getValue();
            
            try {
                LOGGER.info("[SESSION:" + sessionId + "] Stopping container: " + containerId);
                stopContainer(containerId);
            } catch (IOException e) {
                LOGGER.warning("[SESSION:" + sessionId + "] Failed to stop container: " + e.getMessage());
                
                // Try force kill
                try {
                    killContainer(containerId);
                } catch (IOException killEx) {
                    LOGGER.severe("[SESSION:" + sessionId + "] Failed to kill container: " + killEx.getMessage());
                }
            }
        }
        
        sessionContainers.clear();
        LOGGER.info("All containers stopped");
    }
    
    /**
     * Find and clean up orphaned klawed containers (containers not tracked by this service).
     * 
     * @return Number of orphaned containers cleaned up
     */
    public int cleanupOrphanedContainers() {
        LOGGER.info("Cleaning up orphaned klawed containers");
        int cleanedUp = 0;
        
        try {
            // List all containers with klawed- prefix
            ProcessBuilder pb = new ProcessBuilder(
                "podman", "ps", "-a", "--filter", "name=klawed-", "--format", "{{.Names}}"
            );
            pb.redirectErrorStream(true);
            Process process = pb.start();
            
            String output = readProcessOutput(process);
            int exitCode = process.waitFor();
            
            if (exitCode != 0) {
                LOGGER.warning("Failed to list containers: " + output);
                return 0;
            }
            
            for (String containerName : output.split("\n")) {
                containerName = containerName.trim();
                if (containerName.isEmpty()) {
                    continue;
                }
                
                // Extract session ID from container name
                String sessionId = containerName.replace("klawed-", "");
                
                // If not tracked by this service, it's orphaned
                if (!sessionContainers.containsKey(sessionId)) {
                    LOGGER.info("Found orphaned container: " + containerName);
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
        
        LOGGER.info("Cleaned up " + cleanedUp + " orphaned containers");
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
     * Remove container ID from tracking by container ID or name.
     */
    private void removeFromTracking(String containerId) {
        // Remove by container ID or by name
        sessionContainers.entrySet().removeIf(entry -> 
            entry.getValue().equals(containerId) || 
            ("klawed-" + entry.getKey()).equals(containerId)
        );
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
