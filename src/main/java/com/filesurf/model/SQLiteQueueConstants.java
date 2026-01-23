package com.filesurf.model;

/**
 * Constants for SQLite queue message types.
 * Based on the SQLite queue specification from klawedspace/docs/sqlite-queue.md
 * Using constants helps prevent typos and makes refactoring easier.
 */
public class SQLiteQueueConstants {

    // Message types for SQLite queue communication
    public static final String MESSAGE_TYPE_TEXT = "TEXT";
    public static final String MESSAGE_TYPE_TOOL = "TOOL";
    public static final String MESSAGE_TYPE_TOOL_RESULT = "TOOL_RESULT";
    public static final String MESSAGE_TYPE_ERROR = "ERROR";
    public static final String MESSAGE_TYPE_API_CALL = "API_CALL";
    public static final String MESSAGE_TYPE_END_AI_TURN = "END_AI_TURN";
    public static final String MESSAGE_TYPE_AUTO_COMPACTION = "AUTO_COMPACTION";

    // Default sender and receiver names
    public static final String DEFAULT_SENDER_NAME = "client";
    public static final String DEFAULT_RECEIVER_NAME = "klawed";

    // Shared database configuration
    public static final String SHARED_DB_FILENAME = "klawed_messages.db";
    public static final String SHARED_DB_PATH_PROPERTY = "klawed.sqlite-queue.db-path";

    // Configuration defaults
    public static final int DEFAULT_POLL_INTERVAL_MS = 100;
    public static final int DEFAULT_POLL_TIMEOUT_MS = 600000; // 10 minutes for long-running klawed tasks
    public static final int DEFAULT_MAX_RETRIES = 3;
    public static final int DEFAULT_MAX_MESSAGE_SIZE = 1024 * 1024; // 1MB
    public static final int DEFAULT_MAX_QUEUE_SIZE = 1000;

    // Environment variable names
    public static final String ENV_DB_PATH = "KLAWED_SQLITE_DB_PATH";
    public static final String ENV_SENDER_NAME = "KLAWED_SQLITE_SENDER";
    public static final String ENV_POLL_INTERVAL = "KLAWED_SQLITE_POLL_INTERVAL";
    public static final String ENV_POLL_TIMEOUT = "KLAWED_SQLITE_POLL_TIMEOUT";
    public static final String ENV_MAX_RETRIES = "KLAWED_SQLITE_MAX_RETRIES";
    public static final String ENV_MAX_MESSAGE_SIZE = "KLAWED_SQLITE_MAX_MESSAGE_SIZE";
    public static final String ENV_MAX_QUEUE_SIZE = "KLAWED_SQLITE_MAX_QUEUE_SIZE";

    // Command line argument for SQLite queue mode
    public static final String CMD_ARG_SQLITE_QUEUE = "--sqlite-queue";

    private SQLiteQueueConstants() {
        // Utility class, prevent instantiation
    }
}