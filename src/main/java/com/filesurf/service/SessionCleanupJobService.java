package com.filesurf.service;

import com.filesurf.db.SQLiteManager;
import jakarta.enterprise.context.ApplicationScoped;
import jakarta.inject.Inject;
import jakarta.transaction.Transactional;
import jakarta.annotation.PostConstruct;
import io.quarkus.scheduler.Scheduled;

import java.io.IOException;
import java.nio.file.Path;
import java.sql.SQLException;
import java.time.Instant;
import java.util.Optional;
import java.util.UUID;
import java.util.logging.Logger;

/**
 * Persists session cleanup jobs into SQLite so they survive restarts.
 * Jobs are claimed and executed by a scheduled worker.
 */
@ApplicationScoped
public class SessionCleanupJobService {
    private static final Logger LOGGER = Logger.getLogger(SessionCleanupJobService.class.getName());

    private static final String CREATE_TABLE_SQL = """
        CREATE TABLE IF NOT EXISTS session_cleanup_jobs (
            id TEXT PRIMARY KEY,
            session_id TEXT NOT NULL,
            status TEXT NOT NULL,
            scheduled_at INTEGER NOT NULL,
            created_at INTEGER NOT NULL,
            updated_at INTEGER NOT NULL
        );
        CREATE INDEX IF NOT EXISTS idx_session_cleanup_jobs_status ON session_cleanup_jobs(status);
        CREATE INDEX IF NOT EXISTS idx_session_cleanup_jobs_scheduled ON session_cleanup_jobs(scheduled_at);
    """;

    private static final String INSERT_JOB_SQL = """
        INSERT INTO session_cleanup_jobs (id, session_id, status, scheduled_at, created_at, updated_at)
        VALUES (?, ?, 'pending', ?, strftime('%s','now'), strftime('%s','now'));
    """;

    private static final String CLAIM_JOB_SQL = """
        UPDATE session_cleanup_jobs
        SET status = 'in_progress', updated_at = strftime('%s','now')
        WHERE id = (
            SELECT id FROM session_cleanup_jobs
            WHERE status = 'pending' AND scheduled_at <= strftime('%s','now')
            ORDER BY scheduled_at ASC
            LIMIT 1
        )
        RETURNING id, session_id, scheduled_at;
    """;

    private static final String MARK_DONE_SQL = """
        UPDATE session_cleanup_jobs
        SET status = 'done', updated_at = strftime('%s','now')
        WHERE id = ?;
    """;

    private static final String MARK_FAILED_SQL = """
        UPDATE session_cleanup_jobs
        SET status = 'failed', updated_at = strftime('%s','now')
        WHERE id = ?;
    """;

    private static final String CANCEL_JOBS_FOR_SESSION_SQL = """
        UPDATE session_cleanup_jobs
        SET status = 'cancelled', updated_at = strftime('%s','now')
        WHERE session_id = ? AND status IN ('pending', 'in_progress');
    """;

    @Inject
    SQLiteManager sqliteManager;

    @Inject
    SessionManager sessionManager;
    
    @Inject
    FileChatService fileChatService;
    
    @Inject
    KlawedAgentManager klawedAgentManager;

    @PostConstruct
    void init() {
        try {
            sqliteManager.execute(conn -> {
                try (var stmt = conn.createStatement()) {
                    stmt.executeUpdate(CREATE_TABLE_SQL);
                }
                return null;
            });
            LOGGER.info("SessionCleanupJobService schema ensured");
        } catch (SQLException e) {
            LOGGER.severe("Failed to initialize session_cleanup_jobs schema: " + e.getMessage());
        }
    }

    /**
     * Cancel pending cleanup jobs for a session by marking them as 'cancelled'.
     * This should be called when a session is resumed to prevent unnecessary cleanup.
     * Jobs are marked as cancelled rather than deleted to maintain audit trail.
     */
    public void cancelCleanupJobs(String sessionId) {
        try {
            int cancelled = sqliteManager.execute(conn -> {
                try (var ps = conn.prepareStatement(CANCEL_JOBS_FOR_SESSION_SQL)) {
                    ps.setString(1, sessionId);
                    return ps.executeUpdate();
                }
            });
            if (cancelled > 0) {
                LOGGER.info("Marked " + cancelled + " pending cleanup job(s) as cancelled for session " + sessionId);
            }
        } catch (SQLException e) {
            LOGGER.severe("Failed to cancel cleanup jobs for session " + sessionId + ": " + e.getMessage());
        }
    }

    /**
     * Enqueue a cleanup job to run in the near future (default delay 2 minutes).
     */
    public void enqueueCleanup(String sessionId) {
        enqueueCleanup(sessionId, Instant.now().plusSeconds(120));
    }

    public void enqueueCleanup(String sessionId, Instant when) {
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
            LOGGER.info("Enqueued session cleanup job id=" + id + " session=" + sessionId + " scheduled_at=" + when);
        } catch (SQLException e) {
            LOGGER.severe("Failed to enqueue cleanup job for session " + sessionId + ": " + e.getMessage());
        }
    }

    /**
     * Scheduled worker to claim and execute cleanup jobs.
     */
    @Scheduled(every = "30s")
    void processQueue() {
        try {
            Optional<Job> jobOpt = claimJob();
            jobOpt.ifPresent(job -> {
                LOGGER.info("Processing cleanup job id=" + job.id + " session=" + job.sessionId + " scheduled_at=" + job.scheduledAt);
                try {
                    // Check if the session is still active in the database
                    if (fileChatService.isChatSessionActive(job.sessionId)) {
                        LOGGER.info("Session " + job.sessionId + " is still active, skipping cleanup");
                        // Reschedule the job for later (e.g., 5 minutes from now)
                        rescheduleJob(job.id, Instant.now().plusSeconds(300).getEpochSecond());
                        return;
                    }
                    
                    Path sessionDir = sessionManager.resolveSessionPath(job.sessionId);
                    
                    // First, ensure any klawed agent processes are stopped
                    try {
                        KlawedAgentManager.AgentStatus agentStatus = klawedAgentManager.getAgentStatus(job.sessionId);
                        if (agentStatus != null && agentStatus.isProcessAlive()) {
                            LOGGER.info("Stopping klawed agent for session " + job.sessionId + " before cleanup");
                            klawedAgentManager.stopAgentForSession(job.sessionId);
                            // Give process a moment to shut down
                            Thread.sleep(1000);
                        }
                    } catch (Exception e) {
                        LOGGER.warning("Error checking/stopping agent for session " + job.sessionId + ": " + e.getMessage());
                    }
                    
                    // Delete the session directory (includes SQLite files)
                    sessionManager.deleteSessionDirectory(sessionDir);
                    markDone(job.id);
                } catch (Exception e) {
                    LOGGER.warning("Cleanup job failed for session " + job.sessionId + ": " + e.getMessage());
                    try {
                        markFailed(job.id);
                    } catch (SQLException ex) {
                        LOGGER.severe("Failed to mark job failed for id=" + job.id + ": " + ex.getMessage());
                    }
                }
            });
        } catch (Exception e) {
            LOGGER.severe("Error while processing cleanup queue: " + e.getMessage());
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
    
    /**
     * Reschedule a job for a later time.
     */
    private void rescheduleJob(String jobId, long newScheduledAt) throws SQLException {
        sqliteManager.execute(conn -> {
            try (var ps = conn.prepareStatement(
                "UPDATE session_cleanup_jobs SET status = 'pending', scheduled_at = ?, updated_at = strftime('%s','now') WHERE id = ?")) {
                ps.setLong(1, newScheduledAt);
                ps.setString(2, jobId);
                ps.executeUpdate();
                LOGGER.info("Rescheduled job id=" + jobId + " to new scheduled_at=" + newScheduledAt);
            }
            return null;
        });
    }

    private record Job(String id, String sessionId, long scheduledAt) {}
}
