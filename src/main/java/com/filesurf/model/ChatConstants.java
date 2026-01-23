package com.filesurf.model;

/**
 * Constants for chat-related string values.
 * Using constants helps prevent typos and makes refactoring easier.
 */
public class ChatConstants {

    // Common values for sender and receiver
    public static final String CLIENT = "client";
    public static final String AGENT = "agent";
    public static final String SYSTEM = "system";

    // WebSocket/SQLite queue message types
    public static final String MESSAGE_TYPE_TEXT = "TEXT";
    public static final String MESSAGE_TYPE_ERROR = "ERROR";
    public static final String MESSAGE_TYPE_STATUS = "STATUS";
    public static final String MESSAGE_TYPE_TOOL = "TOOL";
    public static final String MESSAGE_TYPE_TOOL_RESULT = "TOOL_RESULT";
    public static final String MESSAGE_TYPE_API_CALL = "API_CALL";
    public static final String MESSAGE_TYPE_END_AI_TURN = "END_AI_TURN";
    public static final String MESSAGE_TYPE_AUTO_COMPACTION = "AUTO_COMPACTION";
    public static final String MESSAGE_TYPE_AGENT_STATUS = "AGENT_STATUS";
    public static final String MESSAGE_TYPE_FILE_UPLOAD = "FILE_UPLOAD";

    // Database message types
    public static final String DB_MESSAGE_TYPE_TEXT = "text";
    public static final String DB_MESSAGE_TYPE_ERROR = "error";
    public static final String DB_MESSAGE_TYPE_STATUS = "status";
    public static final String DB_MESSAGE_TYPE_API_CALL = "api_call";
    public static final String DB_MESSAGE_TYPE_TOOL = "tool";
    public static final String DB_MESSAGE_TYPE_TOOL_RESULT = "tool_result";
    public static final String DB_MESSAGE_TYPE_FILE_UPLOAD = "file_upload";
    public static final String DB_MESSAGE_TYPE_AUTO_COMPACTION = "auto_compaction";

    private ChatConstants() {
        // Utility class, prevent instantiation
    }
}