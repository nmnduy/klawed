package com.filesurf.service;

import com.filesurf.db.SQLiteManager;
import jakarta.enterprise.context.ApplicationScoped;
import jakarta.inject.Inject;
import jakarta.annotation.PostConstruct;
import io.quarkus.scheduler.Scheduled;

import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.sql.SQLException;
import java.time.Instant;
import java.util.Optional;
import java.util.UUID;
import java.util.logging.Logger;
import java.util.stream.Stream;

/**
 * Manages delayed shutdown of klawed agents to allow for reconnections.
 * When a user disconnects, the agent is scheduled for shutdown after a grace period
 * (default 30 seconds). If the user reconnects before the grace period expires,
 * the shutdown job is cancelled and the agent is reused.
 * 
 * After stopping the agent/container, this service also cleans up klawed artifacts
 * from the user's workspace (.klawed/ directory and SQLite queue files).
 */
@ApplicationScoped
public class AgentShutdownJobService {
    private static final Logger LOGGER = Logger.getLogger(AgentShutdownJobService.class.getName());

    private static final String CREATE_TABLE_SQL = """
        CREATE TABLE IF NOT EXISTS agent_shutdown_jobs (
            id TEXT PRIMARY KEY,
            session_id TEXT NOT NULL UNIQUE,
            status TEXT NOT NULL,
            scheduled_at INTEGER NOT NULL,
            created_at INTEGER NOT NULL,
            updated_at INTEGER NOT NULL
        );
        CREATE INDEX IF NOT EXISTS idx_agent_shutdown_jobs_status ON agent_shutdown_jobs(status);
        CREATE INDEX IF NOT EXISTS idx_agent_shutdown_jobs_scheduled ON agent_shutdown_jobs(scheduled_at);
        CREATE INDEX IF NOT EXISTS idx_agent_shutdown_jobs_session ON agent_shutdown_jobs(session_id);
    """;

    private static final String INSERT_JOB_SQL = """
        INSERT OR REPLACE INTO agent_shutdown_jobs (id, session_id, status, scheduled_at, created_at, updated_at)
        VALUES (?, ?, 'pending', ?, strftime('%s','now'), strftime('%s','now'));
    """;

    private static final String CLAIM_JOB_SQL = """
        UPDATE agent_shutdown_jobs
        SET status = 'in_progress', updated_at = strftime('%s','now')
        WHERE id = (
            SELECT id FROM agent_shutdown_jobs
            WHERE status = 'pending' AND scheduled_at <= strftime('%s','now')
            ORDER BY scheduled_at ASC
            LIMIT 1
        )
        RETURNING id, session_id, scheduled_at;
    """;

    private static final String MARK_DONE_SQL = """
        UPDATE agent_shutdown_jobs
        SET status = 'done', updated_at = strftime('%s','now')
        WHERE id = ?;
    """;

    private static final String MARK_FAILED_SQL = """
        UPDATE agent_shutdown_jobs
        SET status = 'failed', updated_at = strftime('%s','now')
        WHERE id = ?;
    """;

    private static final String CANCEL_JOB_FOR_SESSION_SQL = """
        UPDATE agent_shutdown_jobs
        SET status = 'cancelled', updated_at = strftime('%s','now')
        WHERE session_id = ? AND status IN ('pending', 'in_progress');
    """;
    
    private static final String DELETE_OLD_JOBS_SQL = """
        DELETE FROM agent_shutdown_jobs
        WHERE status IN ('done', 'failed', 'cancelled')
        AND updated_at < strftime('%s','now') - 86400;
    """;

    @Inject
    SQLiteManager sqliteManager;

    @Inject
    KlawedAgentManager agentManager;
    
    @Inject
    SessionManager sessionManager;

    @PostConstruct
    void init() {
        try {
            sqliteManager.execute(conn -> {
                try (var stmt = conn.createStatement()) {
                    stmt.executeUpdate(CREATE_TABLE_SQL);
                }
                return null;
            });
            LOGGER.info("AgentShutdownJobService schema ensured");
        } catch (SQLException e) {
            LOGGER.severe("Failed to initialize agent_shutdown_jobs schema: " + e.getMessage());
        }
    }

    /**
     * Cancel pending shutdown jobs for a session.
     * This should be called when a user reconnects to prevent unnecessary agent shutdown.
     */
    public void cancelShutdownJob(String sessionId) {
        try {
            int cancelled = sqliteManager.execute(conn -> {
                try (var ps = conn.prepareStatement(CANCEL_JOB_FOR_SESSION_SQL)) {
                    ps.setString(1, sessionId);
                    return ps.executeUpdate();
                }
            });
            if (cancelled > 0) {
                LOGGER.info("[SESSION:" + sessionId + "] Cancelled " + cancelled + " pending agent shutdown job(s)");
            }
        } catch (SQLException e) {
            LOGGER.severe("[SESSION:" + sessionId + "] Failed to cancel shutdown jobs: " + e.getMessage());
        }
    }

    /**
     * Enqueue an agent shutdown job with default grace period (30 seconds).
     * This allows brief network blips without losing the agent, while still
     * cleaning up promptly when the user has actually disconnected.
     */
    public void enqueueShutdown(String sessionId) {
        enqueueShutdown(sessionId, Instant.now().plusSeconds(30)); // 30 seconds default
    }

    /**
     * Enqueue an agent shutdown job with custom grace period.
     */
    public void enqueueShutdown(String sessionId, Instant when) {
        try {
            String id = UUID.randomUUID().toString();
            long scheduled = when.getEpochSecond();
            sqliteManager.execute(conn -> {
                try (var ps = conn.prepareStatement(INSERT_JOB_SQL)) {
                    ps.setString(1, id);
                    ps.setString(2, sessionId);
                    ps.setLong(3, scheduled);
                    ps.executeUpdate();
                }
                return null;
            });
            LOGGER.info("[SESSION:" + sessionId + "] Enqueued agent shutdown job id=" + id + " scheduled_at=" + when);
        } catch (SQLException e) {
            LOGGER.severe("[SESSION:" + sessionId + "] Failed to enqueue shutdown job: " + e.getMessage());
        }
    }

    /**
     * Scheduled worker to claim and execute shutdown jobs.
     * Runs every 10 seconds to check for agents ready to be shut down.
     */
    @Scheduled(every = "10s")
    void processQueue() {
        try {
            Optional<Job> jobOpt = claimJob();
            jobOpt.ifPresent(job -> {
                LOGGER.info("[SESSION:" + job.sessionId + "] Processing agent shutdown job id=" + job.id + 
                           " scheduled_at=" + Instant.ofEpochSecond(job.scheduledAt));
                try {
                    // Get the workspace path before stopping the agent
                    Path workspace = sessionManager.getWorkspaceForSession(job.sessionId);
                    
                    // Check if agent still exists for this session
                    KlawedAgentManager.KlawedAgentInstance agent = agentManager.getAgentForSession(job.sessionId);
                    if (agent != null) {
                        LOGGER.info("[SESSION:" + job.sessionId + "] Stopping klawed agent after grace period");
                        agentManager.stopAgentForSession(job.sessionId);
                    } else {
                        LOGGER.info("[SESSION:" + job.sessionId + "] Agent already stopped");
                    }
                    
                    // Clean up klawed artifacts from workspace
                    if (workspace != null) {
                        cleanupKlawedArtifacts(job.sessionId, workspace);
                    }
                    
                    markDone(job.id);
                } catch (Exception e) {
                    LOGGER.warning("[SESSION:" + job.sessionId + "] Shutdown job failed: " + e.getMessage());
                    try {
                        markFailed(job.id);
                    } catch (SQLException ex) {
                        LOGGER.severe("[SESSION:" + job.sessionId + "] Failed to mark job failed for id=" + job.id + ": " + ex.getMessage());
                    }
                }
            });
        } catch (Exception e) {
            LOGGER.severe("Error while processing agent shutdown queue: " + e.getMessage());
        }
    }
    
    /**
     * Clean up klawed artifacts from the user's workspace.
     * This includes:
     * - .klawed/ directory (logs, config)
     * - klawed_messages_{sessionId}.db and related WAL files
     */
    private void cleanupKlawedArtifacts(String sessionId, Path workspace) {
        LOGGER.info("[SESSION:" + sessionId + "] Cleaning up klawed artifacts from workspace: " + workspace);
        
        // Delete .klawed/ directory
        Path klawedDir = workspace.resolve(".klawed");
        if (Files.exists(klawedDir)) {
            try {
                deleteDirectory(klawedDir);
                LOGGER.info("[SESSION:" + sessionId + "] Deleted .klawed/ directory");
            } catch (IOException e) {
                LOGGER.warning("[SESSION:" + sessionId + "] Failed to delete .klawed/ directory: " + e.getMessage());
            }
        }
        
        // Delete SQLite queue files (klawed_messages_{sessionId}.db, .db-shm, .db-wal)
        String dbFileName = "klawed_messages_" + sessionId + ".db";
        String[] sqliteExtensions = {"", "-shm", "-wal"};
        
        for (String ext : sqliteExtensions) {
            Path dbFile = workspace.resolve(dbFileName + ext);
            if (Files.exists(dbFile)) {
                try {
                    Files.delete(dbFile);
                    LOGGER.info("[SESSION:" + sessionId + "] Deleted " + dbFile.getFileName());
                } catch (IOException e) {
                    LOGGER.warning("[SESSION:" + sessionId + "] Failed to delete " + dbFile.getFileName() + ": " + e.getMessage());
                }
            }
        }
        
        LOGGER.info("[SESSION:" + sessionId + "] Klawed artifact cleanup completed");
    }
    
    /**
     * Recursively delete a directory.
     */
    private void deleteDirectory(Path dir) throws IOException {
        if (!Files.exists(dir)) {
            return;
        }
        try (Stream<Path> walk = Files.walk(dir)) {
            walk.sorted((a, b) -> b.compareTo(a)) // reverse order to delete children first
                .forEach(path -> {
                    try {
                        Files.delete(path);
                    } catch (IOException e) {
                        LOGGER.warning("Failed to delete: " + path + " - " + e.getMessage());
                    }
                });
        }
    }
    
    /**
     * Cleanup old completed jobs (runs daily).
     */
    @Scheduled(cron = "0 0 3 * * ?") // 3 AM daily
    void cleanupOldJobs() {
        try {
            int deleted = sqliteManager.execute(conn -> {
                try (var ps = conn.prepareStatement(DELETE_OLD_JOBS_SQL)) {
                    return ps.executeUpdate();
                }
            });
            if (deleted > 0) {
                LOGGER.info("Cleaned up " + deleted + " old agent shutdown jobs");
            }
        } catch (SQLException e) {
            LOGGER.warning("Failed to cleanup old agent shutdown jobs: " + e.getMessage());
        }
    }

    private Optional<Job> claimJob() throws SQLException {
        return sqliteManager.execute(conn -> {
            try (var stmt = conn.createStatement()) {
                try (var rs = stmt.executeQuery(CLAIM_JOB_SQL)) {
                    if (rs.next()) {
                        Job job = new Job(
                                rs.getString("id"),
                                rs.getString("session_id"),
                                rs.getLong("scheduled_at"));
                        return Optional.of(job);
                    }
                }
            }
            return Optional.empty();
        });
    }

    private void markDone(String jobId) throws SQLException {
        sqliteManager.execute(conn -> {
            try (var ps = conn.prepareStatement(MARK_DONE_SQL)) {
                ps.setString(1, jobId);
                ps.executeUpdate();
            }
            return null;
        });
    }

    private void markFailed(String jobId) throws SQLException {
        sqliteManager.execute(conn -> {
            try (var ps = conn.prepareStatement(MARK_FAILED_SQL)) {
                ps.setString(1, jobId);
                ps.executeUpdate();
            }
            return null;
        });
    }

    private record Job(String id, String sessionId, long scheduledAt) {}
}
