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
    
    @Inject
    FileChatService invoiceChatService;
    
    // Thread pool for async polling
    private final ExecutorService asyncPollingExecutor = Executors.newCachedThreadPool();
    
    /**
     * Start a new klawed agent for a session
     */
    public KlawedAgentInstance startAgentForSession(String sessionId, Path sessionDir) throws IOException {
        LOGGER.info("[SESSION:" + sessionId + "] Starting dedicated klawed agent");
        
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
        
        // Create SQLite database path for this session
        String dbFileName = "klawed_messages_" + sessionId + ".db";
        Path dbPath = sessionDir.resolve(dbFileName);
        String sqliteDbPath = dbPath.toString();
        LOGGER.info("[SESSION:" + sessionId + "] SQLite database will be: " + sqliteDbPath);
        
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
        
        // Build the process with environment configuration - use SQLite queue mode
        ProcessBuilder processBuilder = new ProcessBuilder(klawedBinary, "--sqlite-queue", sqliteDbPath);
        LOGGER.info("[SESSION:" + sessionId + "] Klawed command: " + String.join(" ", processBuilder.command()));
        
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
        
        processBuilder.environment().put("KLAWED_USE_BEDROCK", "");
        
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
        
        // Get API key from system environment - use OPENAI_API_KEY only
        // This keeps it simple and compatible with any OpenAI-compatible API
        String apiKey = System.getenv("OPENAI_API_KEY");
        if (apiKey != null) {
            processBuilder.environment().put("OPENAI_API_KEY", apiKey);
            LOGGER.info("[SESSION:" + sessionId + "] OPENAI_API_KEY set from environment");
        } else {
            LOGGER.warning("[SESSION:" + sessionId + "] OPENAI_API_KEY not found in environment variables");
            LOGGER.warning("[SESSION:" + sessionId + "] Note: Set OPENAI_API_KEY for your chosen API (DeepSeek, OpenRouter, etc.)");
        }
        
        // Enable verbose tool output for debugging
        processBuilder.environment().put("KLAWED_TOOL_VERBOSE", "1");
        LOGGER.info("[SESSION:" + sessionId + "] Set KLAWED_TOOL_VERBOSE=1");
        
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
        LOGGER.info("[SESSION:" + sessionId + "] Klawed agent started with PID: " + pid + " with SQLite database: " + sqliteDbPath);
        
        // Write PID file to session directory for management
        writePidFile(sessionDir, pid, sqliteDbPath);
        
        // Wait for socket to be created
        int maxAttempts = 100; // 10 seconds
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
            
            // Check if SQLite database file exists and is readable
            if (Files.exists(dbPath) && Files.isReadable(dbPath)) {
                LOGGER.info("[SESSION:" + sessionId + "] SQLite database is ready after " + (i * 100) + "ms: " + sqliteDbPath);
                break;
            }
            
            // Log progress every second
            if (i > 0 && i % 10 == 0) {
                LOGGER.info("[SESSION:" + sessionId + "] Still waiting for SQLite database... (" + (i / 10) + "s elapsed, process alive: " + process.isAlive() + ")");
            }
            
            try {
                Thread.sleep(100);
            } catch (InterruptedException e) {
                Thread.currentThread().interrupt();
                process.destroy();
                throw new IOException("Interrupted while waiting for socket", e);
            }
        }
        
        if (!Files.exists(dbPath) || !Files.isReadable(dbPath)) {
            LOGGER.severe("[SESSION:" + sessionId + "] SQLite database not ready after 10 seconds");
            LOGGER.severe("[SESSION:" + sessionId + "] Process alive: " + process.isAlive());
            LOGGER.severe("[SESSION:" + sessionId + "] Session directory exists: " + Files.exists(sessionDir));
            LOGGER.severe("[SESSION:" + sessionId + "] SQLite database path: " + sqliteDbPath);
            
            // Try to list directory contents
            try {
                LOGGER.info("[SESSION:" + sessionId + "] Listing session directory contents:");
                Files.list(sessionDir).forEach(p -> LOGGER.info("  - " + p.getFileName()));
            } catch (IOException e) {
                LOGGER.severe("[SESSION:" + sessionId + "] Could not list directory: " + e.getMessage());
            }
            
            process.destroy();
            throw new IOException("SQLite database not ready after 10 seconds: " + sqliteDbPath);
        }
        
        KlawedAgentInstance instance = new KlawedAgentInstance(sessionId, process, sqliteDbPath, sessionDir);
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
    

    
    /**
     * Represents a running klawed agent instance
     */
    public class KlawedAgentInstance {
        private final String sessionId;
        private final Process process;
        private final String sqliteDbPath;
        private final Path sessionDir;
        private SQLiteQueueClient sqliteQueueClient;
        private volatile boolean asyncPollingActive = false;
        private volatile boolean shouldPollContinuously = false;
        
        public KlawedAgentInstance(String sessionId, Process process, String sqliteDbPath, Path sessionDir) {
            this.sessionId = sessionId;
            this.process = process;
            this.sqliteDbPath = sqliteDbPath;
            this.sessionDir = sessionDir;
            
            LOGGER.info("[SESSION:" + sessionId + "] Agent instance created with SQLite database: " + sqliteDbPath + 
                       ", sessionDir: " + sessionDir);
        }
        
        public String getSessionId() {
            return sessionId;
        }
        
        public boolean isRunning() {
            return process != null && process.isAlive();
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
         * Connect to the agent's SQLite queue
         */
        public void connect() throws IOException {
            LOGGER.info("[SESSION:" + sessionId + "] Connecting to klawed SQLite queue: " + sqliteDbPath);
            
            if (sqliteQueueClient != null) {
                LOGGER.warning("[SESSION:" + sessionId + "] Already connected to SQLite queue");
                return;
            }
            
            // Create SQLiteQueueClient with sessionId and invoiceChatService
            SQLiteQueueClient.Config config = new SQLiteQueueClient.Config(sqliteDbPath)
                .withSessionId(sessionId)
                .withFileChatService(invoiceChatService);
            
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
            LOGGER.info("[SESSION:" + sessionId + "] Stopping klawed agent instance");
            
            // Stop continuous async polling
            shouldPollContinuously = false;
            asyncPollingActive = false;
            
            // Log PID if process is still alive
            if (process != null && process.isAlive()) {
                try {
                    long pid = process.pid();
                    LOGGER.info("[SESSION:" + sessionId + "] Stopping klawed process with PID: " + pid);
                } catch (Exception e) {
                    LOGGER.warning("[SESSION:" + sessionId + "] Could not get PID: " + e.getMessage());
                }
            }
            
            if (sqliteQueueClient != null) {
                try {
                    sqliteQueueClient.shutdown();
                } catch (Exception e) {
                    LOGGER.warning("[SESSION:" + sessionId + "] Error shutting down SQLiteQueueClient: " + e.getMessage());
                }
                sqliteQueueClient = null;
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
            
            LOGGER.info("[SESSION:" + sessionId + "] Klawed agent instance stopped");
            
            // Delete PID file when agent is stopped
            deletePidFile(sessionId, sessionDir);
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
     */
    private void deletePidFile(String sessionId) {
        try {
            Path sessionDir = Path.of("/tmp/is-sessions", sessionId);
            deletePidFile(sessionId, sessionDir);
        } catch (Exception e) {
            LOGGER.warning("[SESSION:" + sessionId + "] Failed to delete PID file: " + e.getMessage());
        }
    }
    
    /**
     * Read PID file from session directory
     */
    public AgentPidInfo readPidFile(String sessionId) throws IOException {
        Path sessionDir = Path.of("/tmp/is-sessions", sessionId);
        Path pidFile = sessionDir.resolve("klawed.pid");
        
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
     * Check if agent process is still alive by PID
     */
    public boolean isAgentAlive(String sessionId) throws IOException {
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
     * Clean up orphaned agents (those with PID files but no active process)
     */
    public void cleanupOrphanedAgents() throws IOException {
        Path baseDir = Path.of("/tmp/is-sessions");
        if (!Files.exists(baseDir)) {
            return;
        }
        
        try (java.util.stream.Stream<Path> dirs = Files.list(baseDir)) {
            dirs.filter(Files::isDirectory)
                .forEach(dir -> {
                    String sessionId = dir.getFileName().toString();
                    try {
                        AgentPidInfo pidInfo = readPidFile(sessionId);
                        if (pidInfo != null && !isAgentAlive(sessionId)) {
                            LOGGER.info("[SESSION:" + sessionId + "] Found orphaned agent PID: " + 
                                       pidInfo.getPid() + ", cleaning up");
                            deletePidFile(sessionId);
                            
                            // Also clean up session directory if it exists
                            try {
                                SessionManager sessionManager = new SessionManager();
                                sessionManager.cleanupSession(sessionId);
                            } catch (Exception e) {
                                LOGGER.warning("[SESSION:" + sessionId + "] Failed to cleanup session directory: " + 
                                              e.getMessage());
                            }
                        }
                    } catch (IOException e) {
                        LOGGER.warning("[SESSION:" + sessionId + "] Error checking orphaned agent: " + e.getMessage());
                    }
                });
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