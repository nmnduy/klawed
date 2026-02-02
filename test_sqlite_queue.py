#!/usr/bin/env python3
"""
Test script for SQLite queue mode in Klawed.

This script tests the SQLite queue IPC mechanism by:
1. Starting klawed in SQLite queue daemon mode
2. Sending messages via the SQLite database
3. Receiving and verifying responses
"""

import sqlite3
import json
import time
import subprocess
import sys
import os
import signal
import tempfile


class KlawedSQLiteClient:
    """Client for communicating with klawed via SQLite queue."""

    def __init__(self, db_path, sender="client", receiver="klawed"):
        self.db_path = db_path
        self.sender = sender
        self.receiver = receiver
        self.conn = sqlite3.connect(db_path, check_same_thread=False)
        self.conn.row_factory = sqlite3.Row
        self._init_schema()

    def _init_schema(self):
        """Initialize the database schema if not exists."""
        cursor = self.conn.cursor()
        cursor.execute("""
            CREATE TABLE IF NOT EXISTS messages (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                sender TEXT NOT NULL,
                receiver TEXT NOT NULL,
                message TEXT NOT NULL,
                sent INTEGER DEFAULT 0,
                created_at INTEGER DEFAULT (strftime('%s', 'now')),
                updated_at INTEGER DEFAULT (strftime('%s', 'now'))
            )
        """)
        cursor.execute("""
            CREATE INDEX IF NOT EXISTS idx_messages_sender ON messages(sender, sent)
        """)
        cursor.execute("""
            CREATE INDEX IF NOT EXISTS idx_messages_receiver ON messages(receiver, sent)
        """)
        self.conn.commit()

    def send_message(self, message_type, content=None, **extra):
        """Send a message to klawed."""
        message = {"messageType": message_type}
        if content is not None:
            message["content"] = content
        message.update(extra)

        cursor = self.conn.cursor()
        cursor.execute(
            "INSERT INTO messages (sender, receiver, message, sent) VALUES (?, ?, ?, 0)",
            (self.sender, self.receiver, json.dumps(message))
        )
        self.conn.commit()
        msg_id = cursor.lastrowid
        print(f"[SEND] {message_type} (id: {msg_id})")
        return msg_id

    def receive_messages(self, max_messages=10, timeout_sec=30):
        """Poll for messages from klawed with timeout."""
        start_time = time.time()
        cursor = self.conn.cursor()

        while time.time() - start_time < timeout_sec:
            cursor.execute(
                "SELECT id, message FROM messages "
                "WHERE sender = ? AND sent = 0 "
                "ORDER BY created_at ASC LIMIT ?",
                (self.receiver, max_messages)
            )
            rows = cursor.fetchall()
            if rows:
                results = []
                for row in rows:
                    msg_id = row[0]
                    message = json.loads(row[1])
                    results.append((msg_id, message))
                return results
            time.sleep(0.1)  # Short poll interval

        return []  # Timeout

    def acknowledge(self, msg_id):
        """Mark a message as read."""
        cursor = self.conn.cursor()
        cursor.execute("UPDATE messages SET sent = 1 WHERE id = ?", (msg_id,))
        self.conn.commit()

    def send_and_wait(self, prompt, timeout=60):
        """Send a prompt and wait for END_AI_TURN."""
        print(f"\n{'='*60}")
        print(f"Sending: {prompt[:80]}...")
        print(f"{'='*60}")

        self.send_message("TEXT", prompt)

        start_time = time.time()
        all_messages = []

        while time.time() - start_time < timeout:
            messages = self.receive_messages(timeout_sec=1)
            for msg_id, message in messages:
                msg_type = message.get("messageType")
                content = message.get("content", "")

                if msg_type == "TEXT":
                    print(f"[TEXT] {content[:200]}...")
                    all_messages.append(("text", content))
                elif msg_type == "TOOL":
                    tool_name = message.get("toolName")
                    tool_id = message.get("toolId")
                    print(f"[TOOL REQUEST] {tool_name} (id: {tool_id})")
                    all_messages.append(("tool", tool_name))
                elif msg_type == "TOOL_RESULT":
                    tool_name = message.get("toolName")
                    is_error = message.get("isError", False)
                    if is_error:
                        print(f"[TOOL RESULT] {tool_name} - ERROR")
                    else:
                        print(f"[TOOL RESULT] {tool_name} - OK")
                    all_messages.append(("tool_result", tool_name, is_error))
                elif msg_type == "END_AI_TURN":
                    print(f"[END_AI_TURN] AI turn completed")
                    self.acknowledge(msg_id)
                    return True, all_messages
                elif msg_type == "ERROR":
                    print(f"[ERROR] {content}")
                    self.acknowledge(msg_id)
                    return False, all_messages
                elif msg_type == "API_CALL":
                    model = message.get("model", "unknown")
                    print(f"[API_CALL] Waiting for {model}...")

                self.acknowledge(msg_id)

        print(f"\nTimeout after {timeout} seconds")
        return False, all_messages

    def close(self):
        self.conn.close()


def test_basic_functionality():
    """Test basic sqlite-queue functionality."""
    # Create temporary database
    with tempfile.NamedTemporaryFile(suffix=".db", delete=False) as f:
        db_path = f.name

    print(f"Test database: {db_path}")

    try:
        # Initialize client and create schema
        client = KlawedSQLiteClient(db_path)

        # Check if klawed binary exists
        klawed_path = os.path.join(os.path.dirname(__file__), "build", "klawed")
        if not os.path.exists(klawed_path):
            print(f"ERROR: klawed binary not found at {klawed_path}")
            return False

        # Start klawed in daemon mode
        print(f"Starting klawed daemon...")
        env = os.environ.copy()
        env["OPENAI_API_KEY"] = env.get("OPENAI_API_KEY", "test-key")
        env["KLAWED_LOG_LEVEL"] = "DEBUG"

        proc = subprocess.Popen(
            [klawed_path, "--sqlite-queue", db_path],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            env=env
        )

        # Wait for daemon to start
        time.sleep(2)

        try:
            # Test 1: Simple greeting
            print("\n--- Test 1: Simple greeting ---")
            success, messages = client.send_and_wait("Hello! Please respond with a short greeting.", timeout=30)
            if not success:
                print("FAIL: Test 1 - No response received")
                return False
            print("PASS: Test 1 - Received response")

            # Test 2: Tool execution (Glob)
            print("\n--- Test 2: Tool execution ---")
            success, messages = client.send_and_wait(
                "List all C files in the current directory using the Glob tool.",
                timeout=60
            )
            if not success:
                print("FAIL: Test 2 - No response or timeout")
                return False

            # Check if tool was called
            tool_calls = [m for m in messages if m[0] == "tool"]
            if tool_calls:
                print(f"PASS: Test 2 - Tool was called: {tool_calls}")
            else:
                print("INFO: Test 2 - No tool calls (AI may have responded directly)")

            print("\n" + "="*60)
            print("All tests passed!")
            print("="*60)
            return True

        finally:
            # Cleanup
            print("\nStopping klawed daemon...")
            proc.send_signal(signal.SIGTERM)
            try:
                proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                proc.kill()
                proc.wait()

            client.close()

    finally:
        # Clean up database file
        try:
            os.unlink(db_path)
        except:
            pass


def test_pending_message_injection():
    """Test that pending messages during tool execution are handled correctly."""
    with tempfile.NamedTemporaryFile(suffix=".db", delete=False) as f:
        db_path = f.name

    print(f"\nTest database: {db_path}")

    try:
        client = KlawedSQLiteClient(db_path)

        klawed_path = os.path.join(os.path.dirname(__file__), "build", "klawed")
        if not os.path.exists(klawed_path):
            print(f"ERROR: klawed binary not found at {klawed_path}")
            return False

        print(f"Starting klawed daemon for pending message test...")
        env = os.environ.copy()
        env["OPENAI_API_KEY"] = env.get("OPENAI_API_KEY", "test-key")
        env["KLAWED_LOG_LEVEL"] = "DEBUG"

        proc = subprocess.Popen(
            [klawed_path, "--sqlite-queue", db_path],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            env=env
        )

        time.sleep(2)

        try:
            # Send a message that will trigger tool execution
            print("\n--- Sending message that triggers tool execution ---")
            client.send_message("TEXT", "Read the file README.md and tell me what it contains.")

            # Wait a bit for tool execution to start
            time.sleep(1)

            # Send a second message while the first is still processing
            # This should be queued and processed after the first completes
            print("--- Sending second message during tool execution ---")
            client.send_message("TEXT", "Also, what files are in the src directory?")

            # Now wait for all responses
            print("--- Waiting for all responses ---")
            start_time = time.time()
            timeout = 60
            turn_count = 0

            while time.time() - start_time < timeout and turn_count < 2:
                messages = client.receive_messages(timeout_sec=1)
                for msg_id, message in messages:
                    msg_type = message.get("messageType")
                    content = message.get("content", "")

                    if msg_type == "TEXT":
                        print(f"[TEXT] {content[:100]}...")
                    elif msg_type == "TOOL":
                        tool_name = message.get("toolName")
                        print(f"[TOOL] {tool_name}")
                    elif msg_type == "END_AI_TURN":
                        turn_count += 1
                        print(f"[END_AI_TURN] Turn {turn_count} completed")

                    client.acknowledge(msg_id)

            if turn_count >= 1:
                print(f"PASS: Completed {turn_count} turn(s)")
                return True
            else:
                print("FAIL: No turns completed")
                return False

        finally:
            print("\nStopping klawed daemon...")
            proc.send_signal(signal.SIGTERM)
            try:
                proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                proc.kill()
                proc.wait()

            client.close()

    finally:
        try:
            os.unlink(db_path)
        except:
            pass


if __name__ == "__main__":
    print("SQLite Queue Mode Test for Klawed")
    print("=" * 60)

    if not os.environ.get("OPENAI_API_KEY"):
        print("WARNING: OPENAI_API_KEY not set. Tests will likely fail.")
        print("Set it with: export OPENAI_API_KEY=your-key")
        print()

    # Run tests
    success = test_basic_functionality()

    if success:
        print("\nRunning pending message injection test...")
        success = test_pending_message_injection()

    sys.exit(0 if success else 1)
