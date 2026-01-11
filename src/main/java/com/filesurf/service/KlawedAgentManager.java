package com.filesurf.service;

import jakarta.annotation.PreDestroy;
import jakarta.enterprise.context.ApplicationScoped;
import jakarta.inject.Inject;
import org.eclipse.microprofile.config.inject.ConfigProperty;

import java.io.BufferedReader;
import java.io.File;
import java.io.IOException;
import java.io.InputStreamReader;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.TimeUnit;
import java.util.logging.Logger;

/**
 * Manages individual klawed agent instances, one per session
 */
@ApplicationScoped
public class KlawedAgentManager {

    private static final Logger LOGGER = Logger.getLogger(KlawedAgentManager.class.getName());
    
    // Map session ID to agent instance
    private final ConcurrentHashMap<String, KlawedAgentInstance> agents = new ConcurrentHashMap<>();
    
    @ConfigProperty(name = "openai.proxy.http")
    Optional<String> httpProxy;
    
    @ConfigProperty(name = "openai.proxy.https")
    Optional<String> httpsProxy;
    
    @ConfigProperty(name = "openai.api.base")
    Optional<String> apiBase;
    
    @ConfigProperty(name = "openai.api.model")
    Optional<String> apiModel;
    
    @ConfigProperty(name = "klawed.path")
    Optional<String> klawedPath;
    
    @ConfigProperty(name = "klawed.communication.mode", defaultValue = "unix-socket")
    String communicationMode;
    
    @ConfigProperty(name = "klawed.unix-socket.filename", defaultValue = "klawed.sock")
    String unixSocketFilename;
    
    @ConfigProperty(name = "klawed.unix-socket.timeout-ms", defaultValue = "30000")
    int unixSocketTimeoutMs;
    
    @ConfigProperty(name = "klawed.unix-socket.max-message-size", defaultValue = "67108864")
    int unixSocketMaxMessageSize;
    
    @ConfigProperty(name = "klawed.sqlite-queue.db-path")
    Optional<String> sqliteQueueDbPath;
    
    @Inject
    FileChatService fileChatService;
    
    @Inject
    PodmanSandboxService podmanSandboxService;
    
    // Thread pool for async polling
    private final ExecutorService asyncPollingExecutor = Executors.newCachedThreadPool();
    
    /**
     * Start a new klawed agent for a session
     */
    public KlawedAgentInstance startAgentForSession(String sessionId, Path sessionDir) throws IOException {
        boolean sandboxMode = podmanSandboxService.isEnabled();
        LOGGER.info("[SESSION:" + sessionId + "] Starting dedicated klawed agent" + 
                   (sandboxMode ? " (sandbox mode)" : " (direct mode)"));
        
        // Check if agent already exists for this session
        KlawedAgentInstance existingAgent = agents.get(sessionId);
        if (existingAgent != null) {
            LOGGER.info("[SESSION:" + sessionId + "] Agent already exists, returning existing instance");
            return existingAgent;
        }
        
        // Verify session directory exists and is writable
        if (!Files.exists(sessionDir)) {
            throw new IOException("Session directory does not exist: " + sessionDir);
        }
        if (!Files.isDirectory(sessionDir)) {
            throw new IOException("Session directory is not a directory: " + sessionDir);
        }
        if (!Files.isWritable(sessionDir)) {
            throw new IOException("Session directory is not writable: " + sessionDir);
        }
        LOGGER.info("[SESSION:" + sessionId + "] Session directory verified: " + sessionDir);

        // Guardrail: if a klawed.pid already exists and the process is running, do NOT spawn a duplicate
        // This is per-user workspace level (sessionDir is the user workspace)
        ensureNoRunningAgentForWorkspace(sessionId, sessionDir);
        
        // Use shared SQLite database path for all sessions
        String sqliteDbPath;
        String containerDbPath;
        
        if (sqliteQueueDbPath.isPresent() && !sqliteQueueDbPath.get().isEmpty()) {
            // Use configured database path
            String configuredPath = sqliteQueueDbPath.get();
            // Resolve the path relative to the session directory (user workspace)
            Path resolvedPath = Path.of(configuredPath);
            if (!resolvedPath.isAbsolute()) {
                // If path is relative, resolve it relative to the session directory (user workspace)
                resolvedPath = sessionDir.resolve(resolvedPath);
            }
            sqliteDbPath = resolvedPath.toString();
            containerDbPath = sqliteDbPath; // For container mode, use same path (should be mounted)
            LOGGER.info("[SESSION:" + sessionId + "] Using SQLite database in workspace: " + sqliteDbPath);
        } else {
            // Fallback to per-session database (backward compatibility)
            String dbFileName = "klawed_messages_" + sessionId + ".db";
            Path dbPath = sessionDir.resolve(dbFileName);
            sqliteDbPath = dbPath.toString();
            containerDbPath = "/workspace/" + dbFileName;
            LOGGER.info("[SESSION:" + sessionId + "] Using per-session SQLite database: " + sqliteDbPath);
        }
        
        KlawedAgentInstance instance;
        
        if (sandboxMode) {
            // --- Sandbox mode: Run klawed in a Podman container ---
            instance = startAgentInContainer(sessionId, sessionDir, sqliteDbPath, containerDbPath);
        } else {
            // --- Direct mode: Run klawed via ProcessBuilder ---
            instance = startAgentDirectly(sessionId, sessionDir, sqliteDbPath);
        }
        
        agents.put(sessionId, instance);
        
        // Connect to SQLite queue (starts continuous async polling)
        try {
            instance.connect();
            LOGGER.info("[SESSION:" + sessionId + "] Klawed agent instance created and connected");
        } catch (IOException e) {
            LOGGER.warning("[SESSION:" + sessionId + "] Failed to connect to SQLite queue (but continuing): " + e.getMessage());
            // Don't throw - the agent might still work if connect is retried later
        }
        
        return instance;
    }
    
    /**
     * Start klawed agent in a Podman container (sandbox mode)
     */
    private KlawedAgentInstance startAgentInContainer(String sessionId, Path sessionDir, 
                                                       String sqliteDbPath, String containerDbPath) throws IOException {
        LOGGER.info("[SESSION:" + sessionId + "] Starting klawed in Podman container");
        
        // Start container via PodmanSandboxService
        // Note: containerDbPath is the path inside the container (/workspace/...)
        String containerId = podmanSandboxService.startContainer(sessionId, sessionDir, containerDbPath);
        
        LOGGER.info("[SESSION:" + sessionId + "] Container started: " + containerId);
        
        // Give container a grace period to initialize
        int maxAttempts = 30; // 3 seconds should be plenty
        for (int i = 0; i < maxAttempts; i++) {
            // Check if container is still running
            if (!podmanSandboxService.isContainerRunning(containerId)) {
                String logs = "";
                try {
                    logs = podmanSandboxService.getContainerLogs(containerId, 50);
                } catch (Exception e) {
                    logs = "(could not fetch logs: " + e.getMessage() + ")";
                }
                LOGGER.severe("[SESSION:" + sessionId + "] Container died unexpectedly. Logs:\n" + logs);
                throw new IOException("Container died unexpectedly");
            }
            
            // Check if directory still exists
            if (!Files.exists(sessionDir)) {
                LOGGER.severe("[SESSION:" + sessionId + "] Session directory disappeared: " + sessionDir);
                try {
                    podmanSandboxService.stopContainer(containerId);
                } catch (Exception e) {
                    LOGGER.warning("[SESSION:" + sessionId + "] Failed to stop container after error: " + e.getMessage());
                }
                throw new IOException("Session directory disappeared: " + sessionDir);
            }
            
            // After 1 second, if container is still running, we're good
            if (i >= 10) {
                LOGGER.info("[SESSION:" + sessionId + "] Container stable after " + (i * 100) + "ms");
                break;
            }
            
            try {
                Thread.sleep(100);
            } catch (InterruptedException e) {
                Thread.currentThread().interrupt();
                try {
                    podmanSandboxService.stopContainer(containerId);
                } catch (Exception ex) {
                    LOGGER.warning("[SESSION:" + sessionId + "] Failed to stop container: " + ex.getMessage());
                }
                throw new IOException("Interrupted while waiting for container to stabilize", e);
            }
        }
        
        // Create instance with container ID (no direct process handle)
        return new KlawedAgentInstance(sessionId, null, sqliteDbPath, sessionDir, containerId);
    }
    
    /**
     * Start klawed agent directly via ProcessBuilder (direct mode)
     */
    private KlawedAgentInstance startAgentDirectly(String sessionId, Path sessionDir, 
                                                    String sqliteDbPath) throws IOException {
        // Get agent binary path - try config first, then which command, finally default
        String klawedBinary = klawedPath.orElseGet(() -> {
            try {
                Process whichProcess = new ProcessBuilder("which", "klawed").start();
                BufferedReader reader = new BufferedReader(new InputStreamReader(whichProcess.getInputStream()));
                String path = reader.readLine();
                whichProcess.waitFor();
                if (path != null && !path.isEmpty() && new File(path).exists()) {
                    LOGGER.info("[SESSION:" + sessionId + "] Found klawed at: " + path);
                    return path;
                }
            } catch (Exception e) {
                LOGGER.warning("[SESSION:" + sessionId + "] Failed to locate klawed with 'which': " + e.getMessage());
            }
            return "klawed"; // fallback to PATH
        });
        
        LOGGER.info("[SESSION:" + sessionId + "] Using klawed binary: " + klawedBinary);
        
        // Build the process with environment configuration based on communication mode
        ProcessBuilder processBuilder;
        if ("unix-socket".equals(communicationMode)) {
            Path socketPath = sessionDir.resolve(unixSocketFilename);
            processBuilder = new ProcessBuilder(klawedBinary, "-u", socketPath.toString());
            LOGGER.info("[SESSION:" + sessionId + "] Starting klawed in Unix socket mode: " + socketPath);
        } else {
            processBuilder = new ProcessBuilder(klawedBinary, "--sqlite-queue", sqliteDbPath);
            LOGGER.info("[SESSION:" + sessionId + "] Starting klawed in SQLite queue mode: " + sqliteDbPath);
        }
        LOGGER.info("[SESSION:" + sessionId + "] Klawed command: " + String.join(" ", processBuilder.command()));

        // Before launch, double-check there isn't already a klawed process for this workspace
        if (isExistingKlawedProcessRunning(sessionDir)) {
            throw new IOException("Klawed already running for workspace: " + sessionDir);
        }
        
        // Set working directory to session directory
        processBuilder.directory(sessionDir.toFile());
        LOGGER.info("[SESSION:" + sessionId + "] Klawed working directory: " + sessionDir);
        
        // Set environment variables from configuration
        if (httpsProxy.isPresent()) {
            processBuilder.environment().put("HTTPS_PROXY", httpsProxy.get());
            LOGGER.info("[SESSION:" + sessionId + "] Set HTTPS_PROXY: " + httpsProxy.get());
        }
        
        if (httpProxy.isPresent()) {
            processBuilder.environment().put("HTTP_PROXY", httpProxy.get());
            LOGGER.info("[SESSION:" + sessionId + "] Set HTTP_PROXY: " + httpProxy.get());
        }
        
        // Forward Bedrock/Anthropic specific environment if user has configured it (e.g., via shell aliases)
        copyEnvIfPresent(processBuilder, "KLAWED_USE_BEDROCK");
        copyEnvIfPresent(processBuilder, "ANTHROPIC_MODEL");
        copyEnvIfPresent(processBuilder, "ANTHROPIC_VERSION");
        copyEnvIfPresent(processBuilder, "AWS_REGION");
        copyEnvIfPresent(processBuilder, "AWS_PROFILE");
        copyEnvIfPresent(processBuilder, "AWS_ACCESS_KEY_ID");
        copyEnvIfPresent(processBuilder, "AWS_SECRET_ACCESS_KEY");
        copyEnvIfPresent(processBuilder, "AWS_SESSION_TOKEN");
        
        // Forward LD_LIBRARY_PATH for shared libraries (e.g., libmemvid_ffi.so)
        copyEnvIfPresent(processBuilder, "LD_LIBRARY_PATH");
        
        // API configuration - prioritize OPENAI_* environment variables for simplicity
        // This aligns with the user's request to use only OpenAI-compatible environment variables
        // Configuration properties are now openai.api.base and openai.api.model
        String apiBaseUrl = System.getenv("OPENAI_API_BASE");
        if (apiBaseUrl == null || apiBaseUrl.isBlank()) {
            apiBaseUrl = apiBase.orElse("https://api.deepseek.com");
        }
        
        String modelName = System.getenv("OPENAI_MODEL");
        if (modelName == null || modelName.isBlank()) {
            modelName = apiModel.orElse("deepseek-chat");
        }

        // Push resolved base/model into child env so aliases (incl. Bedrock/Anthropic) flow through
        processBuilder.environment().put("OPENAI_API_BASE", apiBaseUrl);
        processBuilder.environment().put("OPENAI_MODEL", modelName);
        
        // Get API key from system environment - prefer OPENAI_API_KEY, allow OPENROUTER_API_KEY and ANTHROPIC_API_KEY fallback
        String apiKey = System.getenv("OPENAI_API_KEY");
        if (apiKey == null || apiKey.isBlank()) {
            apiKey = System.getenv("OPENROUTER_API_KEY");
        }
        if (apiKey == null || apiKey.isBlank()) {
            apiKey = System.getenv("ANTHROPIC_API_KEY");
        }
        if (apiKey != null && !apiKey.isBlank()) {
            processBuilder.environment().put("OPENAI_API_KEY", apiKey);
            LOGGER.info("[SESSION:" + sessionId + "] OPENAI_API_KEY set from environment");
        } else {
            LOGGER.warning("[SESSION:" + sessionId + "] OPENAI_API_KEY not found in environment variables");
            LOGGER.warning("[SESSION:" + sessionId + "] Note: Set OPENAI_API_KEY for your chosen API (DeepSeek, OpenRouter, Anthropic, etc.)");
        }

        // Forward optional auth header and extra headers for Anthropic-compatible endpoints (e.g., Sonnet/Opus via Anthropic API)
        copyEnvIfPresent(processBuilder, "OPENAI_AUTH_HEADER");
        copyEnvIfPresent(processBuilder, "OPENAI_EXTRA_HEADERS");
        
        // Enable verbose tool output for debugging
        processBuilder.environment().put("KLAWED_TOOL_VERBOSE", "1");
        LOGGER.info("[SESSION:" + sessionId + "] Set KLAWED_TOOL_VERBOSE=1");
        
        // Ensure klawed can find shared libraries (e.g., libmemvid_ffi.so)
        copyEnvIfPresent(processBuilder, "LD_LIBRARY_PATH");
        
        LOGGER.info("[SESSION:" + sessionId + "] API configuration - Base: " + apiBaseUrl + ", Model: " + modelName);

        // Create logs directory if it doesn't exist
        File logsDir = new File("logs");
        if (!logsDir.exists()) {
            logsDir.mkdirs();
        }

        // All klawed agents write to the same log file
        // We'll prefix log messages with session ID
        processBuilder.redirectOutput(ProcessBuilder.Redirect.appendTo(new File("logs/klawed-agents.log")));
        processBuilder.redirectError(ProcessBuilder.Redirect.appendTo(new File("logs/klawed-agents.log")));

        Process process = processBuilder.start();
        long pid = process.pid();
        
        String dbInfo = "unix-socket".equals(communicationMode) ? 
            "Unix socket: " + sessionDir.resolve(unixSocketFilename) : 
            "SQLite database: " + sqliteDbPath;
        LOGGER.info("[SESSION:" + sessionId + "] Klawed agent started with PID: " + pid + " with " + dbInfo);
        
        // Write PID file to session directory for management
        writePidFile(sessionDir, pid, sqliteDbPath);

        // Record in-memory too (agents map already has it) so we don't allow duplicates concurrently
        
        // Give klawed a grace period to initialize (klawed creates DB lazily on first message)
        // We just need to make sure the process starts successfully
        int maxAttempts = 30; // 3 seconds should be plenty
        for (int i = 0; i < maxAttempts; i++) {
            // Check if process is still alive
            if (!process.isAlive()) {
                int exitCode = process.exitValue();
                LOGGER.severe("[SESSION:" + sessionId + "] Klawed process died with exit code: " + exitCode);
                throw new IOException("Klawed process died with exit code: " + exitCode);
            }
            
            // Check if directory still exists
            if (!Files.exists(sessionDir)) {
                LOGGER.severe("[SESSION:" + sessionId + "] Session directory disappeared: " + sessionDir);
                process.destroy();
                throw new IOException("Session directory disappeared: " + sessionDir);
            }
            
            // After 1 second, if process is still alive, we're good
            // (klawed creates DB lazily when first message arrives)
            if (i >= 10) {
                LOGGER.info("[SESSION:" + sessionId + "] Klawed process stable after " + (i * 100) + "ms");
                break;
            }
            
            try {
                Thread.sleep(100);
            } catch (InterruptedException e) {
                Thread.currentThread().interrupt();
                process.destroy();
                throw new IOException("Interrupted while waiting for process to stabilize", e);
            }
        }
        
        return new KlawedAgentInstance(sessionId, process, sqliteDbPath, sessionDir);
    }
    
    /**
     * Get agent instance for a session
     */
    public KlawedAgentInstance getAgentForSession(String sessionId) {
        KlawedAgentInstance instance = agents.get(sessionId);
        if (instance == null) {
            LOGGER.warning("[SESSION:" + sessionId + "] No agent instance found for session");
        }
        return instance;
    }
    
    /**
     * Check if an agent exists for a session
     */
    public boolean hasAgentForSession(String sessionId) {
        return agents.containsKey(sessionId);
    }
    
    /**
     * Stop and cleanup agent for a session
     */
    public void stopAgentForSession(String sessionId) {
        KlawedAgentInstance instance = agents.remove(sessionId);
        if (instance != null) {
            LOGGER.info("[SESSION:" + sessionId + "] Stopping klawed agent");
            instance.stop();
        }
    }
    
    /**
     * Stop all agents (for application shutdown)
     */
    public void stopAllAgents() {
        LOGGER.info("Stopping all klawed agents");
        agents.forEach((sessionId, instance) -> {
            LOGGER.info("[SESSION:" + sessionId + "] Stopping agent");
            instance.stop();
        });
        agents.clear();
        
        // Also stop any orphaned containers when in sandbox mode
        if (podmanSandboxService.isEnabled()) {
            LOGGER.info("Stopping all Podman containers (sandbox mode)");
            podmanSandboxService.stopAllContainers();
        }
    }
    
    /**
     * Check if klawed is still working for a session (has pending tool requests)
     */
    public boolean isKlawedWorking(String sessionId) {
        KlawedAgentInstance instance = agents.get(sessionId);
        if (instance != null) {
            return instance.isKlawedWorking();
        }
        return false;
    }
    
    /**
     * Get number of pending tool requests for a session
     */
    public int getPendingToolRequestCount(String sessionId) {
        KlawedAgentInstance instance = agents.get(sessionId);
        if (instance != null) {
            return instance.getPendingToolRequestCount();
        }
        return 0;
    }
    

    private static void copyEnvIfPresent(ProcessBuilder pb, String key) {
        String val = System.getenv(key);
        if (val != null && !val.isBlank()) {
            pb.environment().put(key, val);
        }
    }

    /**
     * Represents a running klawed agent instance
     */
    public class KlawedAgentInstance {
        private final String sessionId;
        private final Process process;  // null if running in container
        private final String sqliteDbPath;
        private final Path sessionDir;
        private final String containerId;  // null if running directly (not in container)
        private SQLiteQueueClient sqliteQueueClient;
        private UnixSocketClient unixSocketClient;
        private volatile boolean asyncPollingActive = false;
        private volatile boolean shouldPollContinuously = false;
        
        public KlawedAgentInstance(String sessionId, Process process, String sqliteDbPath, Path sessionDir) {
            this(sessionId, process, sqliteDbPath, sessionDir, null);
        }
        
        public KlawedAgentInstance(String sessionId, Process process, String sqliteDbPath, Path sessionDir, String containerId) {
            this.sessionId = sessionId;
            this.process = process;
            this.sqliteDbPath = sqliteDbPath;
            this.sessionDir = sessionDir;
            this.containerId = containerId;
            
            String modeInfo = containerId != null ? "sandbox mode (container: " + containerId + ")" : "direct mode";
            String commInfo = "unix-socket".equals(communicationMode) ? "Unix socket mode" : "SQLite queue mode";
            LOGGER.info("[SESSION:" + sessionId + "] Agent instance created in " + modeInfo + 
                       " with " + commInfo + ", sessionDir: " + sessionDir);
        }
        
        public String getSessionId() {
            return sessionId;
        }
        
        /**
         * Get the container ID if running in sandbox mode.
         * @return container ID, or null if running directly (not in container)
         */
        public String getContainerId() {
            return containerId;
        }
        
        /**
         * Check if this instance is running in a Podman container.
         */
        public boolean isRunningInContainer() {
            return containerId != null;
        }
        
        public boolean isRunning() {
            if (containerId != null) {
                // Running in container - check container status
                return podmanSandboxService.isContainerRunning(containerId);
            } else {
                // Running directly - check process status
                return process != null && process.isAlive();
            }
        }
        
        /**
         * Check if klawed is still working (has pending tool requests)
         */
        public boolean isKlawedWorking() {
            if (sqliteQueueClient != null) {
                return sqliteQueueClient.isKlawedWorking();
            }
            return false;
        }
        
        /**
         * Get number of pending tool requests
         */
        public int getPendingToolRequestCount() {
            if (sqliteQueueClient != null) {
                return sqliteQueueClient.getPendingToolRequestCount();
            }
            return 0;
        }
        
        public String getSqliteDbPath() {
            return sqliteDbPath;
        }
        
        /**
         * Connect to the agent based on communication mode
         */
        public void connect() throws IOException {
            if ("unix-socket".equals(communicationMode)) {
                connectUnixSocket();
            } else {
                connectSqliteQueue();
            }
        }
        
        /**
         * Connect to the agent's Unix socket
         */
        private void connectUnixSocket() throws IOException {
            Path socketPath = sessionDir.resolve(unixSocketFilename);
            LOGGER.info("[SESSION:" + sessionId + "] Connecting to klawed Unix socket: " + socketPath);
            
            if (unixSocketClient != null) {
                LOGGER.warning("[SESSION:" + sessionId + "] Already connected to Unix socket");
                return;
            }
            
            // Create UnixSocketClient with sessionId and fileChatService
            UnixSocketClient.Config config = new UnixSocketClient.Config(socketPath.toString())
                .withSessionId(sessionId)
                .withFileChatService(fileChatService)
                .withTimeoutMs(unixSocketTimeoutMs)
                .withMaxMessageSize(unixSocketMaxMessageSize);
            
            unixSocketClient = new UnixSocketClient(config);
            unixSocketClient.connect();
            
            LOGGER.info("[SESSION:" + sessionId + "] Successfully connected to Unix socket");
            
            // Unix socket doesn't need async polling - responses come directly
        }
        
        /**
         * Connect to the agent's SQLite queue
         */
        private void connectSqliteQueue() throws IOException {
            LOGGER.info("[SESSION:" + sessionId + "] Connecting to klawed SQLite queue: " + sqliteDbPath);
            
            if (sqliteQueueClient != null) {
                LOGGER.warning("[SESSION:" + sessionId + "] Already connected to SQLite queue");
                return;
            }
            
            // Create SQLiteQueueClient with sessionId and fileChatService
            SQLiteQueueClient.Config config = new SQLiteQueueClient.Config(sqliteDbPath)
                .withSessionId(sessionId)
                .withFileChatService(fileChatService);
            
            sqliteQueueClient = new SQLiteQueueClient(config);
            sqliteQueueClient.connect();
            
            LOGGER.info("[SESSION:" + sessionId + "] Successfully connected to SQLite queue");
            
            // Start continuous polling for responses
            startAsyncPolling();
        }
        
        /**
         * Send message to the agent and get response
         */
        public String sendMessage(String message) throws IOException, InterruptedException {
            if ("unix-socket".equals(communicationMode)) {
                return sendMessageUnixSocket(message);
            } else {
                return sendMessageSqliteQueue(message);
            }
        }
        
        private String sendMessageUnixSocket(String message) throws IOException, InterruptedException {
            LOGGER.info("[SESSION:" + sessionId + "] Sending message to klawed via Unix socket");
            LOGGER.info("[SESSION:" + sessionId + "] Message: " + 
                       message.substring(0, Math.min(100, message.length())) + 
                       (message.length() > 100 ? "..." : ""));
            
            if (unixSocketClient == null) {
                connect();
            }
            
            if (!unixSocketClient.isConnected()) {
                throw new IOException("UnixSocketClient not connected to Unix socket");
            }
            
            String response = unixSocketClient.sendMessage(message);
            LOGGER.info("[SESSION:" + sessionId + "] Received response of length: " + 
                       (response != null ? response.length() : 0));
            
            return response;
        }
        
        private String sendMessageSqliteQueue(String message) throws IOException, InterruptedException {
            LOGGER.info("[SESSION:" + sessionId + "] Sending message to klawed via SQLite queue");
            LOGGER.info("[SESSION:" + sessionId + "] Message: " + 
                       message.substring(0, Math.min(100, message.length())) + 
                       (message.length() > 100 ? "..." : ""));
            
            if (sqliteQueueClient == null) {
                connect();
            }
            
            if (!sqliteQueueClient.isConnected()) {
                throw new IOException("SQLiteQueueClient not connected to SQLite queue");
            }
            
            String response = sqliteQueueClient.sendAndReceive(message);
            LOGGER.info("[SESSION:" + sessionId + "] Received response of length: " + 
                       (response != null ? response.length() : 0));
            
            return response;
        }
        
        /**
         * Send message to the agent asynchronously
         * Returns immediately, responses delivered via polling service
         */
        public void sendMessageAsync(String message) throws IOException {
            if ("unix-socket".equals(communicationMode)) {
                sendMessageAsyncUnixSocket(message);
            } else {
                sendMessageAsyncSqliteQueue(message);
            }
        }
        
        private void sendMessageAsyncUnixSocket(String message) throws IOException {
            LOGGER.info("[SESSION:" + sessionId + "] Sending message asynchronously to klawed via Unix socket");
            LOGGER.info("[SESSION:" + sessionId + "] Message: " + 
                       message.substring(0, Math.min(100, message.length())) + 
                       (message.length() > 100 ? "..." : ""));
            
            if (unixSocketClient == null) {
                connect();
            }
            
            if (!unixSocketClient.isConnected()) {
                throw new IOException("UnixSocketClient not connected to Unix socket");
            }
            
            unixSocketClient.sendMessageAsync(message);
            LOGGER.info("[SESSION:" + sessionId + "] Message sent asynchronously via Unix socket");
        }
        
        private void sendMessageAsyncSqliteQueue(String message) throws IOException {
            LOGGER.info("[SESSION:" + sessionId + "] Sending message asynchronously to klawed via SQLite queue");
            LOGGER.info("[SESSION:" + sessionId + "] Message: " + 
                       message.substring(0, Math.min(100, message.length())) + 
                       (message.length() > 100 ? "..." : ""));
            
            if (sqliteQueueClient == null) {
                connect();
            }
            
            if (!sqliteQueueClient.isConnected()) {
                throw new IOException("SQLiteQueueClient not connected to SQLite queue");
            }
            
            sqliteQueueClient.sendMessageAsync(message);
            LOGGER.info("[SESSION:" + sessionId + "] Message sent asynchronously, responses will be delivered via polling service");
        }
        
        /**
         * Start continuous background polling for async responses
         */
        private void startAsyncPolling() {
            if (asyncPollingActive) {
                LOGGER.fine("[SESSION:" + sessionId + "] Async polling already active");
                return;
            }
            
            if (sqliteQueueClient == null) {
                LOGGER.warning("[SESSION:" + sessionId + "] Cannot start async polling: SQLiteQueueClient is null");
                return;
            }
            
            asyncPollingActive = true;
            shouldPollContinuously = true;
            asyncPollingExecutor.submit(() -> {
                try {
                    LOGGER.info("[SESSION:" + sessionId + "] Starting continuous async polling for responses");
                    
                    while (shouldPollContinuously && asyncPollingActive) {
                        // Check if client is still connected before polling
                        if (sqliteQueueClient == null || !sqliteQueueClient.isConnected()) {
                            LOGGER.fine("[SESSION:" + sessionId + "] SQLiteQueueClient no longer connected, exiting poll loop");
                            break;
                        }
                        
                        try {
                            // Poll for messages with a short timeout
                            List<String> messages = sqliteQueueClient.receiveMessages(500);
                            
                            // If we got messages, they've been saved to main database by receiveMessages
                            if (!messages.isEmpty()) {
                                LOGGER.fine("[SESSION:" + sessionId + "] Received " + messages.size() + " messages via async polling");
                            }
                            
                            // Sleep a bit before polling again
                            Thread.sleep(100);
                        } catch (IOException e) {
                            // Check if client is still connected - if not, stop polling
                            if (sqliteQueueClient == null || !sqliteQueueClient.isConnected()) {
                                LOGGER.fine("[SESSION:" + sessionId + "] SQLiteQueueClient disconnected, stopping async polling");
                                break;
                            }
                            LOGGER.warning("[SESSION:" + sessionId + "] Error in async polling: " + e.getMessage());
                            // Continue polling despite errors
                        } catch (InterruptedException e) {
                            Thread.currentThread().interrupt();
                            LOGGER.info("[SESSION:" + sessionId + "] Async polling interrupted");
                            break;
                        }
                    }
                    
                    LOGGER.info("[SESSION:" + sessionId + "] Continuous async polling stopped");
                    
                } catch (Exception e) {
                    LOGGER.severe("[SESSION:" + sessionId + "] Error in async polling thread: " + e.getMessage());
                } finally {
                    asyncPollingActive = false;
                    shouldPollContinuously = false;
                }
            });
        }
        
        /**
         * Send message to the agent and stream responses line by line
         */
        public java.util.List<String> sendMessageStreaming(String message) throws IOException, InterruptedException {
            LOGGER.info("[SESSION:" + sessionId + "] Starting sendMessageStreaming via SQLite queue");
            LOGGER.info("[SESSION:" + sessionId + "] Message: " + 
                       message.substring(0, Math.min(100, message.length())) + 
                       (message.length() > 100 ? "..." : ""));
            
            if (sqliteQueueClient == null) {
                connect();
            }
            
            if (!sqliteQueueClient.isConnected()) {
                throw new IOException("SQLiteQueueClient not connected to SQLite queue");
            }
            
            // Send message and get response
            String response = sqliteQueueClient.sendAndReceive(message);
            
            java.util.List<String> responseLines = new java.util.ArrayList<>();
            if (response != null) {
                // Split response into lines
                String[] lines = response.split("\\r?\\n");
                for (String line : lines) {
                    if (!line.trim().isEmpty()) {
                        responseLines.add(line);
                    }
                }
            }
            
            LOGGER.info("[SESSION:" + sessionId + "] Streaming response complete, lines: " + responseLines.size());
            return responseLines;
        }
        
        /**
         * Stop the agent process and cleanup
         */
        public void stop() {
            String modeInfo = containerId != null ? "sandbox mode (container: " + containerId + ")" : "direct mode";
            LOGGER.info("[SESSION:" + sessionId + "] Stopping klawed agent instance (" + modeInfo + ")");
            
            // Stop continuous async polling
            shouldPollContinuously = false;
            asyncPollingActive = false;
            
            // Shutdown clients first
            if (sqliteQueueClient != null) {
                try {
                    sqliteQueueClient.shutdown();
                } catch (Exception e) {
                    LOGGER.warning("[SESSION:" + sessionId + "] Error shutting down SQLiteQueueClient: " + e.getMessage());
                }
                sqliteQueueClient = null;
            }
            
            if (unixSocketClient != null) {
                try {
                    unixSocketClient.disconnect();
                } catch (Exception e) {
                    LOGGER.warning("[SESSION:" + sessionId + "] Error disconnecting UnixSocketClient: " + e.getMessage());
                }
                unixSocketClient = null;
            }
            
            if (containerId != null) {
                // Running in container - stop via PodmanSandboxService
                try {
                    LOGGER.info("[SESSION:" + sessionId + "] Stopping container: " + containerId);
                    podmanSandboxService.stopContainer(containerId);
                } catch (Exception e) {
                    LOGGER.warning("[SESSION:" + sessionId + "] Error stopping container: " + e.getMessage());
                    // Try force kill
                    try {
                        podmanSandboxService.killContainer(containerId);
                    } catch (Exception killEx) {
                        LOGGER.severe("[SESSION:" + sessionId + "] Failed to kill container: " + killEx.getMessage());
                    }
                }
            } else {
                // Running directly - stop via Process
                // Log PID if process is still alive
                if (process != null && process.isAlive()) {
                    try {
                        long pid = process.pid();
                        LOGGER.info("[SESSION:" + sessionId + "] Stopping klawed process with PID: " + pid);
                    } catch (Exception e) {
                        LOGGER.warning("[SESSION:" + sessionId + "] Could not get PID: " + e.getMessage());
                    }
                }
                
                if (process != null && process.isAlive()) {
                    process.destroy();
                    try {
                        if (process.isAlive()) {
                            process.waitFor(5, java.util.concurrent.TimeUnit.SECONDS);
                            if (process.isAlive()) {
                                process.destroyForcibly();
                            }
                        }
                    } catch (InterruptedException e) {
                        Thread.currentThread().interrupt();
                        process.destroyForcibly();
                    }
                }
            }
            
            // Clean up klawed database files
            cleanupKlawedDbFiles(sessionId, sessionDir);
            
            LOGGER.info("[SESSION:" + sessionId + "] Klawed agent instance stopped");
            
            // Delete PID file when agent is stopped (only for direct mode)
            if (containerId == null) {
                deletePidFile(sessionId, sessionDir);
            }
        }
    }
    
    /**
     * Shutdown the async polling executor and stop all agents
     */
    @PreDestroy
    public void shutdown() {
        LOGGER.info("Shutting down KlawedAgentManager");
        
        // First stop all agents
        stopAllAgents();
        
        // Then shutdown the executor
        LOGGER.info("Shutting down async polling executor");
        asyncPollingExecutor.shutdown();
        try {
            if (!asyncPollingExecutor.awaitTermination(5, TimeUnit.SECONDS)) {
                asyncPollingExecutor.shutdownNow();
            }
        } catch (InterruptedException e) {
            asyncPollingExecutor.shutdownNow();
            Thread.currentThread().interrupt();
        }
        
        LOGGER.info("KlawedAgentManager shutdown complete");
    }
    
    /**
     * Clean up klawed database files from the session directory.
     * This includes:
     * - klawed_messages_{sessionId}.db
     * - klawed_messages_{sessionId}.db-shm (shared memory)
     * - klawed_messages_{sessionId}.db-wal (write-ahead log)
     */
    private void cleanupKlawedDbFiles(String sessionId, Path sessionDir) {
        if (sessionDir == null) {
            LOGGER.warning("[SESSION:" + sessionId + "] Cannot cleanup klawed DB files: sessionDir is null");
            return;
        }
        
        // Check if we're using a shared database - if so, don't delete it
        if (sqliteQueueDbPath.isPresent() && !sqliteQueueDbPath.get().isEmpty()) {
            LOGGER.info("[SESSION:" + sessionId + "] Using shared database, skipping cleanup of per-session files");
            return;
        }
        
        LOGGER.info("[SESSION:" + sessionId + "] Cleaning up klawed database files from: " + sessionDir);
        
        String dbFileName = "klawed_messages_" + sessionId + ".db";
        String[] sqliteExtensions = {"", "-shm", "-wal"};
        
        for (String ext : sqliteExtensions) {
            Path dbFile = sessionDir.resolve(dbFileName + ext);
            if (Files.exists(dbFile)) {
                try {
                    Files.delete(dbFile);
                    LOGGER.info("[SESSION:" + sessionId + "] Deleted " + dbFile.getFileName());
                } catch (IOException e) {
                    LOGGER.warning("[SESSION:" + sessionId + "] Failed to delete " + dbFile.getFileName() + ": " + e.getMessage());
                }
            }
        }
    }
    
    /**
     * Write PID file to session directory for agent management
     */
    private void writePidFile(Path sessionDir, long pid, String sqliteDbPath) throws IOException {
        Path pidFile = sessionDir.resolve("klawed.pid");
        String pidContent = String.format("pid=%d%ndb_path=%s%ntimestamp=%d%n",
            pid, sqliteDbPath, System.currentTimeMillis());
        
        Files.writeString(pidFile, pidContent, java.nio.charset.StandardCharsets.UTF_8);
        LOGGER.info("Wrote PID file: " + pidFile + " for PID: " + pid);
    }

    /**
     * Prevent duplicate klawed per user workspace by checking pid file + ps aux | grep.
     * Throws IOException if a running klawed is detected.
     */
    private void ensureNoRunningAgentForWorkspace(String sessionId, Path sessionDir) throws IOException {
        Path pidFile = sessionDir.resolve("klawed.pid");

        // 1) If we already track an agent in memory for this session, respect it
        KlawedAgentInstance existing = agents.get(sessionId);
        if (existing != null && existing.isRunning()) {
            throw new IOException("Klawed already running for session " + sessionId + " in memory");
        }

        // 2) If pid file exists, check if that PID is alive
        if (Files.exists(pidFile)) {
            try {
                String content = Files.readString(pidFile);
                long pid = parsePid(content);
                if (pid > 0 && isProcessAlive(pid)) {
                    throw new IOException("Klawed already running (pid file) for workspace: " + sessionDir + " pid=" + pid);
                }
            } catch (Exception e) {
                LOGGER.warning("[SESSION:" + sessionId + "] Failed to parse pid file; continuing to ps check: " + e.getMessage());
            }
        }

        // 3) Belt-and-suspenders: run ps aux | grep <user-workspace> to see if a klawed is alive
        if (isExistingKlawedProcessRunning(sessionDir)) {
            throw new IOException("Klawed already running (ps scan) for workspace: " + sessionDir);
        }
    }

    private long parsePid(String content) {
        for (String line : content.split("\n")) {
            if (line.startsWith("pid=")) {
                try {
                    return Long.parseLong(line.substring("pid=".length()).trim());
                } catch (NumberFormatException ignored) {
                }
            }
        }
        return -1;
    }

    private boolean isProcessAlive(long pid) {
        try {
            return ProcessHandle.of(pid).map(ProcessHandle::isAlive).orElse(false);
        } catch (Exception e) {
            return false;
        }
    }

    /**
     * Check via ps aux | grep to see if any klawed process is using this workspace
     */
    private boolean isExistingKlawedProcessRunning(Path sessionDir) {
        try {
            Process process = new ProcessBuilder("ps", "aux").start();
            try (BufferedReader reader = new BufferedReader(new InputStreamReader(process.getInputStream()))) {
                String line;
                while ((line = reader.readLine()) != null) {
                    // Look for klawed and the workspace path
                    if (line.contains("klawed") && line.contains(sessionDir.toString())) {
                        return true;
                    }
                }
            }
        } catch (IOException e) {
            LOGGER.warning("ps aux scan failed: " + e.getMessage());
        }
        return false;
    }
    
    /**
     * Delete PID file for a session
     */
    private void deletePidFile(String sessionId, Path sessionDir) {
        try {
            Path pidFile = sessionDir.resolve("klawed.pid");
            if (Files.exists(pidFile)) {
                Files.delete(pidFile);
                LOGGER.info("[SESSION:" + sessionId + "] Deleted PID file: " + pidFile);
            }
        } catch (IOException e) {
            LOGGER.warning("[SESSION:" + sessionId + "] Failed to delete PID file: " + e.getMessage());
        }
    }
    
    /**
     * Delete PID file for a session (overload without sessionDir)
     * Only works if agent is still tracked in memory.
     */
    private void deletePidFile(String sessionId) {
        KlawedAgentInstance agent = agents.get(sessionId);
        if (agent != null && agent.sessionDir != null) {
            deletePidFile(sessionId, agent.sessionDir);
        } else {
            LOGGER.warning("[SESSION:" + sessionId + "] Cannot delete PID file: agent not tracked in memory");
        }
    }
    
    /**
     * Read PID file from session directory.
     * Only works for agents tracked in memory or in container mode.
     * Returns null if agent is not tracked or running in container.
     */
    public AgentPidInfo readPidFile(String sessionId) throws IOException {
        KlawedAgentInstance agent = agents.get(sessionId);
        if (agent == null || agent.sessionDir == null) {
            return null;
        }
        
        // In container mode, there's no PID file
        if (agent.isRunningInContainer()) {
            return null;
        }
        
        Path pidFile = agent.sessionDir.resolve("klawed.pid");
        
        if (!Files.exists(pidFile)) {
            return null;
        }
        
        String content = Files.readString(pidFile, java.nio.charset.StandardCharsets.UTF_8);
        Map<String, String> props = new java.util.HashMap<>();
        
        for (String line : content.split("\\n")) {
            String[] parts = line.split("=", 2);
            if (parts.length == 2) {
                props.put(parts[0].trim(), parts[1].trim());
            }
        }
        
        try {
            long pid = Long.parseLong(props.get("pid"));
            String dbPath = props.get("db_path");
            long timestamp = Long.parseLong(props.get("timestamp"));
            
            return new AgentPidInfo(sessionId, pid, dbPath, timestamp);
        } catch (Exception e) {
            throw new IOException("Invalid PID file format: " + pidFile, e);
        }
    }
    
    /**
     * Check if agent process is still alive.
     * For container mode, checks if container is running.
     * For direct mode, checks if process PID is alive.
     */
    public boolean isAgentAlive(String sessionId) throws IOException {
        KlawedAgentInstance agent = agents.get(sessionId);
        if (agent != null) {
            return agent.isRunning();
        }
        
        // Agent not in memory - for direct mode, check PID file
        AgentPidInfo pidInfo = readPidFile(sessionId);
        if (pidInfo == null) {
            return false;
        }
        
        // Check if process is still running
        try {
            ProcessHandle processHandle = ProcessHandle.of(pidInfo.getPid()).orElse(null);
            return processHandle != null && processHandle.isAlive();
        } catch (Exception e) {
            LOGGER.warning("[SESSION:" + sessionId + "] Error checking process status for PID " + 
                          pidInfo.getPid() + ": " + e.getMessage());
            return false;
        }
    }
    
    /**
     * Get agent status for a session
     */
    public AgentStatus getAgentStatus(String sessionId) throws IOException {
        KlawedAgentInstance instance = agents.get(sessionId);
        AgentPidInfo pidInfo = readPidFile(sessionId);
        
        boolean inMemory = instance != null;
        boolean hasPidFile = pidInfo != null;
        boolean processAlive = false;
        long pid = -1;
        String dbPath = null;
        
        if (pidInfo != null) {
            pid = pidInfo.getPid();
            dbPath = pidInfo.getDbPath();
            processAlive = isAgentAlive(sessionId);
        }
        
        return new AgentStatus(sessionId, inMemory, hasPidFile, processAlive, pid, dbPath);
    }
    
    /**
     * Get all active session IDs
     */
    public java.util.List<String> getActiveSessions() {
        return new java.util.ArrayList<>(agents.keySet());
    }
    
    /**
     * Clean up orphaned agents that are tracked in memory but not running.
     * In container mode, this stops any orphaned containers.
     * In direct mode, this deletes PID files for dead processes.
     */
    public void cleanupOrphanedAgents() {
        // Check all tracked agents
        for (String sessionId : new java.util.ArrayList<>(agents.keySet())) {
            try {
                KlawedAgentInstance agent = agents.get(sessionId);
                if (agent != null && !agent.isRunning()) {
                    LOGGER.info("[SESSION:" + sessionId + "] Found orphaned agent, cleaning up");
                    stopAgentForSession(sessionId);
                }
            } catch (Exception e) {
                LOGGER.warning("[SESSION:" + sessionId + "] Error checking/cleaning orphaned agent: " + e.getMessage());
            }
        }
        
        // In container mode, also check for orphaned containers via PodmanSandboxService
        if (podmanSandboxService.isEnabled()) {
            podmanSandboxService.cleanupOrphanedContainers();
        }
    }
    
    /**
     * Data class for agent PID information
     */
    public static class AgentPidInfo {
        private final String sessionId;
        private final long pid;
        private final String dbPath;
        private final long timestamp;
        
        public AgentPidInfo(String sessionId, long pid, String dbPath, long timestamp) {
            this.sessionId = sessionId;
            this.pid = pid;
            this.dbPath = dbPath;
            this.timestamp = timestamp;
        }
        
        public String getSessionId() { return sessionId; }
        public long getPid() { return pid; }
        public String getDbPath() { return dbPath; }
        public long getTimestamp() { return timestamp; }
        
        @Override
        public String toString() {
            return String.format("AgentPidInfo[sessionId=%s, pid=%d, dbPath=%s, timestamp=%d]",
                sessionId, pid, dbPath, timestamp);
        }
    }
    
    /**
     * Data class for agent status information
     */
    public static class AgentStatus {
        private final String sessionId;
        private final boolean inMemory;
        private final boolean hasPidFile;
        private final boolean processAlive;
        private final long pid;
        private final String dbPath;
        
        public AgentStatus(String sessionId, boolean inMemory, boolean hasPidFile, 
                          boolean processAlive, long pid, String dbPath) {
            this.sessionId = sessionId;
            this.inMemory = inMemory;
            this.hasPidFile = hasPidFile;
            this.processAlive = processAlive;
            this.pid = pid;
            this.dbPath = dbPath;
        }
        
        public String getSessionId() { return sessionId; }
        public boolean isInMemory() { return inMemory; }
        public boolean hasPidFile() { return hasPidFile; }
        public boolean isProcessAlive() { return processAlive; }
        public long getPid() { return pid; }
        public String getDbPath() { return dbPath; }
        
        @Override
        public String toString() {
            return String.format("AgentStatus[sessionId=%s, inMemory=%s, hasPidFile=%s, " +
                               "processAlive=%s, pid=%d, dbPath=%s]",
                sessionId, inMemory, hasPidFile, processAlive, pid, dbPath);
        }
    }
}