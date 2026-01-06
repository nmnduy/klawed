-- Create chat_session table
CREATE TABLE IF NOT EXISTS chat_session (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    session_id TEXT NOT NULL UNIQUE,
    client_identity TEXT,
    created_at TIMESTAMP NOT NULL,
    last_activity_at TIMESTAMP,
    is_active BOOLEAN NOT NULL DEFAULT TRUE
);

-- Create chat_message table
CREATE TABLE IF NOT EXISTS chat_message (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    chat_session_id INTEGER NOT NULL,
    sender TEXT NOT NULL,
    receiver TEXT NOT NULL,
    content TEXT NOT NULL,
    message_type TEXT,
    sent BOOLEAN NOT NULL DEFAULT FALSE,
    created_at TIMESTAMP NOT NULL,
    sent_at TIMESTAMP,
    FOREIGN KEY (chat_session_id) REFERENCES chat_session(id) ON DELETE CASCADE
);

-- Create indexes for better performance
CREATE INDEX IF NOT EXISTS idx_chat_message_chat_session_id ON chat_message(chat_session_id);
CREATE INDEX IF NOT EXISTS idx_chat_message_sent ON chat_message(sent);
CREATE INDEX IF NOT EXISTS idx_chat_message_created_at ON chat_message(created_at);
CREATE INDEX IF NOT EXISTS idx_chat_session_session_id ON chat_session(session_id);
CREATE INDEX IF NOT EXISTS idx_chat_session_is_active ON chat_session(is_active);