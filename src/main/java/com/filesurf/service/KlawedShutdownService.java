package com.filesurf.service;

import jakarta.annotation.PostConstruct;
import jakarta.annotation.PreDestroy;
import jakarta.enterprise.context.ApplicationScoped;
import jakarta.inject.Inject;
import java.io.BufferedReader;
import java.io.File;
import java.io.IOException;
import java.io.InputStreamReader;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.logging.Logger;

/**
 * Service to handle SIGKILL and other shutdown signals to clean up
 * running klawed sqlite-queue instances before shutting down the server.
 * 
 * This service registers a shutdown hook that will be called when the
 * JVM receives SIGTERM, SIGINT, or other termination signals.
 */
@ApplicationScoped
public class KlawedShutdownService {
    
    private static final Logger LOGGER = Logger.getLogger(KlawedShutdownService.class.getName());
    
    @Inject
    KlawedAgentManager klawedAgentManager;
    
    private final AtomicBoolean shutdownInProgress = new AtomicBoolean(false);
    private Thread shutdownHook;
    
    /**
     * Initialize the shutdown service and register shutdown hook
     */
    @PostConstruct
    public void init() {
        LOGGER.info("Initializing KlawedShutdownService");
        
        // Register shutdown hook
        shutdownHook = new Thread(() -> {
            if (shutdownInProgress.compareAndSet(false, true)) {
                LOGGER.info("Shutdown hook triggered, cleaning up klawed processes...");
                cleanupAllKlawedProcesses();
            }
        }, "KlawedShutdownHook");
        
        Runtime.getRuntime().addShutdownHook(shutdownHook);
        LOGGER.info("Registered shutdown hook for klawed process cleanup");
        
        // Also register signal handlers for more immediate response
        registerSignalHandlers();
    }
    
    /**
     * Cleanup method called on application shutdown
     */
    @PreDestroy
    public void cleanup() {
        LOGGER.info("KlawedShutdownService cleanup called");
        
        if (shutdownInProgress.compareAndSet(false, true)) {
            LOGGER.info("Application shutdown detected, cleaning up klawed processes...");
            cleanupAllKlawedProcesses();
        }
        
        // Remove shutdown hook to avoid duplicate cleanup
        if (shutdownHook != null) {
            try {
                Runtime.getRuntime().removeShutdownHook(shutdownHook);
                LOGGER.info("Removed shutdown hook");
            } catch (IllegalStateException e) {
                // JVM is already shutting down, ignore
                LOGGER.fine("Could not remove shutdown hook (JVM shutting down)");
            }
        }
    }
    
    /**
     * Register signal handlers for SIGTERM, SIGINT, etc.
     */
    private void registerSignalHandlers() {
        try {
            // Use sun.misc.Signal for signal handling if available
            Class<?> signalClass = Class.forName("sun.misc.Signal");
            Class<?> signalHandlerClass = Class.forName("sun.misc.SignalHandler");
            
            // Create signal handler
            Object signalHandler = java.lang.reflect.Proxy.newProxyInstance(
                getClass().getClassLoader(),
                new Class<?>[]{signalHandlerClass},
                (proxy, method, args) -> {
                    if (method.getName().equals("handle")) {
                        sun.misc.Signal signal = (sun.misc.Signal) args[0];
                        LOGGER.warning("Received signal: " + signal.getName() + " (" + signal.getNumber() + ")");
                        
                        if (shutdownInProgress.compareAndSet(false, true)) {
                            LOGGER.info("Signal handler triggered, cleaning up klawed processes...");
                            cleanupAllKlawedProcesses();
                        }
                    }
                    return null;
                }
            );
            
            // Register for common termination signals
            String[] signals = {"TERM", "INT", "HUP"};
            for (String sigName : signals) {
                try {
                    Object signal = signalClass.getConstructor(String.class).newInstance(sigName);
                    signalClass.getMethod("handle", signalClass, signalHandlerClass)
                              .invoke(null, signal, signalHandler);
                    LOGGER.info("Registered signal handler for SIG" + sigName);
                } catch (Exception e) {
                    LOGGER.warning("Could not register handler for SIG" + sigName + ": " + e.getMessage());
                }
            }
        } catch (ClassNotFoundException e) {
            LOGGER.warning("sun.misc.Signal not available, using only shutdown hooks");
        } catch (Exception e) {
            LOGGER.warning("Failed to register signal handlers: " + e.getMessage());
        }
    }
    
    /**
     * Clean up all running klawed processes
     */
    public void cleanupAllKlawedProcesses() {
        LOGGER.info("Starting cleanup of all klawed processes");
        
        try {
            // First, use KlawedAgentManager to stop all managed agents
            if (klawedAgentManager != null) {
                LOGGER.info("Stopping all managed klawed agents");
                klawedAgentManager.stopAllAgents();
            }
            
            // Then, find and kill any orphaned klawed processes
            List<Long> orphanedPids = findOrphanedKlawedPids();
            LOGGER.info("Found " + orphanedPids.size() + " orphaned klawed processes");
            
            for (Long pid : orphanedPids) {
                killProcess(pid);
            }
            
            // Clean up PID files
            cleanupPidFiles();
            
            LOGGER.info("Klawed process cleanup completed");
        } catch (Exception e) {
            LOGGER.severe("Error during klawed process cleanup: " + e.getMessage());
        }
    }
    
    /**
     * Find orphaned klawed process PIDs by scanning the session directory
     */
    private List<Long> findOrphanedKlawedPids() {
        List<Long> pids = new ArrayList<>();
        Path sessionsDir = Paths.get("/tmp/is-sessions");
        
        if (!Files.exists(sessionsDir) || !Files.isDirectory(sessionsDir)) {
            LOGGER.info("Session directory not found: " + sessionsDir);
            return pids;
        }
        
        try {
            Files.list(sessionsDir)
                .filter(Files::isDirectory)
                .forEach(sessionDir -> {
                    Path pidFile = sessionDir.resolve("klawed.pid");
                    if (Files.exists(pidFile)) {
                        try {
                            String content = Files.readString(pidFile);
                            String[] lines = content.split("\n");
                            for (String line : lines) {
                                if (line.startsWith("pid=")) {
                                    try {
                                        long pid = Long.parseLong(line.substring(4).trim());
                                        pids.add(pid);
                                        LOGGER.info("Found PID " + pid + " in " + pidFile);
                                    } catch (NumberFormatException e) {
                                        LOGGER.warning("Invalid PID format in " + pidFile + ": " + line);
                                    }
                                    break;
                                }
                            }
                        } catch (IOException e) {
                            LOGGER.warning("Could not read PID file " + pidFile + ": " + e.getMessage());
                        }
                    }
                });
        } catch (IOException e) {
            LOGGER.warning("Error scanning session directory: " + e.getMessage());
        }
        
        return pids;
    }
    
    /**
     * Kill a process by PID
     */
    private void killProcess(long pid) {
        LOGGER.info("Attempting to kill process with PID: " + pid);
        
        try {
            // First try graceful termination
            ProcessHandle processHandle = ProcessHandle.of(pid).orElse(null);
            if (processHandle != null && processHandle.isAlive()) {
                LOGGER.info("Process " + pid + " is alive, attempting to destroy...");
                
                // Try to get process info
                try {
                    ProcessHandle.Info info = processHandle.info();
                    LOGGER.info("Process info - command: " + info.command().orElse("unknown") +
                               ", arguments: " + info.arguments().map(args -> String.join(" ", args)).orElse("none"));
                } catch (Exception e) {
                    LOGGER.fine("Could not get process info: " + e.getMessage());
                }
                
                // Destroy the process
                processHandle.destroy();
                
                // Wait for termination
                boolean terminated = processHandle.onExit().thenApply(ph -> {
                    LOGGER.info("Process " + pid + " terminated gracefully");
                    return true;
                }).exceptionally(e -> {
                    LOGGER.warning("Process " + pid + " did not terminate gracefully: " + e.getMessage());
                    return false;
                }).get(5, java.util.concurrent.TimeUnit.SECONDS);
                
                if (!terminated) {
                    // Force kill if graceful termination failed
                    LOGGER.warning("Process " + pid + " did not terminate, forcing kill...");
                    processHandle.destroyForcibly();
                    
                    try {
                        boolean forceTerminated = processHandle.onExit().thenApply(ph -> {
                            LOGGER.info("Process " + pid + " force terminated");
                            return true;
                        }).get(2, java.util.concurrent.TimeUnit.SECONDS);
                        
                        if (!forceTerminated) {
                            LOGGER.severe("Failed to force terminate process " + pid);
                        }
                    } catch (Exception e) {
                        LOGGER.severe("Error force terminating process " + pid + ": " + e.getMessage());
                    }
                }
            } else {
                LOGGER.info("Process " + pid + " is not running");
            }
        } catch (Exception e) {
            LOGGER.warning("Error killing process " + pid + ": " + e.getMessage());
            
            // Fallback to system kill command
            try {
                LOGGER.info("Trying system kill command for PID " + pid);
                Process killProcess = new ProcessBuilder("kill", "-9", String.valueOf(pid)).start();
                int exitCode = killProcess.waitFor();
                if (exitCode == 0) {
                    LOGGER.info("System kill command succeeded for PID " + pid);
                } else {
                    LOGGER.warning("System kill command failed for PID " + pid + " with exit code: " + exitCode);
                }
            } catch (Exception killEx) {
                LOGGER.severe("System kill command also failed for PID " + pid + ": " + killEx.getMessage());
            }
        }
    }
    
    /**
     * Clean up PID files in session directories
     */
    private void cleanupPidFiles() {
        Path sessionsDir = Paths.get("/tmp/is-sessions");
        
        if (!Files.exists(sessionsDir) || !Files.isDirectory(sessionsDir)) {
            return;
        }
        
        try {
            Files.list(sessionsDir)
                .filter(Files::isDirectory)
                .forEach(sessionDir -> {
                    Path pidFile = sessionDir.resolve("klawed.pid");
                    if (Files.exists(pidFile)) {
                        try {
                            Files.delete(pidFile);
                            LOGGER.info("Deleted PID file: " + pidFile);
                        } catch (IOException e) {
                            LOGGER.warning("Could not delete PID file " + pidFile + ": " + e.getMessage());
                        }
                    }
                });
        } catch (IOException e) {
            LOGGER.warning("Error cleaning up PID files: " + e.getMessage());
        }
    }
    
    /**
     * Find all running klawed processes using ps command
     * This is a more aggressive method that finds all klawed processes
     */
    public List<Long> findAllKlawedProcesses() {
        List<Long> pids = new ArrayList<>();
        
        try {
            ProcessBuilder pb = new ProcessBuilder("ps", "aux");
            Process process = pb.start();
            
            try (BufferedReader reader = new BufferedReader(new InputStreamReader(process.getInputStream()))) {
                String line;
                while ((line = reader.readLine()) != null) {
                    if (line.contains("klawed") && line.contains("--sqlite-queue")) {
                        // Parse PID from ps output
                        String[] parts = line.trim().split("\\s+");
                        if (parts.length > 1) {
                            try {
                                long pid = Long.parseLong(parts[1]);
                                pids.add(pid);
                                LOGGER.fine("Found klawed sqlite-queue process: PID=" + pid);
                            } catch (NumberFormatException e) {
                                // Skip invalid PID
                            }
                        }
                    }
                }
            }
            
            process.waitFor();
        } catch (Exception e) {
            LOGGER.warning("Error finding klawed processes: " + e.getMessage());
        }
        
        return pids;
    }
    
    /**
     * Emergency cleanup - kill all klawed processes found via ps
     */
    public void emergencyCleanup() {
        LOGGER.warning("Performing emergency cleanup of all klawed processes");
        
        List<Long> allPids = findAllKlawedProcesses();
        LOGGER.warning("Found " + allPids.size() + " klawed processes to kill");
        
        for (Long pid : allPids) {
            killProcess(pid);
        }
        
        LOGGER.warning("Emergency cleanup completed");
    }
    
    /**
     * Check if shutdown is in progress
     */
    public boolean isShutdownInProgress() {
        return shutdownInProgress.get();
    }
}