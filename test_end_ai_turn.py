#!/usr/bin/env python3
"""
Test script for END_AI_TURN event in SQLite queue mode.

This script:
1. Starts klawed in sqlite-queue mode
2. Sends a simple message
3. Watches for END_AI_TURN event
4. Verifies the event is received
"""

import sqlite3
import json
import time
import subprocess
import os
import signal
import sys

DB_PATH = "/tmp/test_end_ai_turn.db"
SENDER = "test_client"
RECEIVER = "klawed"

class KlawedTestClient:
    def __init__(self, db_path, sender="test_client", receiver="klawed"):
        self.db_path = db_path
        self.sender = sender
        self.receiver = receiver
        self.conn = sqlite3.connect(db_path)
    
    def send_message(self, message_type, content):
        """Send a message to klawed."""
        message = {"messageType": message_type, "content": content}
        message_str = json.dumps(message)
        
        cursor = self.conn.cursor()
        cursor.execute(
            "INSERT INTO messages (sender, receiver, message, sent) VALUES (?, ?, ?, 0)",
            (self.sender, self.receiver, message_str)
        )
        self.conn.commit()
        print(f"[SENT] {message_type}: {content[:50]}...")
    
    def receive_messages(self):
        """Poll for messages from klawed."""
        cursor = self.conn.cursor()
        cursor.execute(
            "SELECT id, message FROM messages "
            "WHERE sender = ? AND sent = 0 "
            "ORDER BY created_at ASC LIMIT 10",
            (self.receiver,)
        )
        return cursor.fetchall()
    
    def acknowledge(self, msg_id):
        """Mark a message as read."""
        cursor = self.conn.cursor()
        cursor.execute("UPDATE messages SET sent = 1 WHERE id = ?", (msg_id,))
        self.conn.commit()
    
    def wait_for_completion(self, timeout=30):
        """Wait for END_AI_TURN event."""
        print(f"\n[TEST] Waiting for messages (timeout: {timeout}s)...")
        start_time = time.time()
        received_messages = []
        
        while time.time() - start_time < timeout:
            messages = self.receive_messages()
            for msg_id, message_json in messages:
                message = json.loads(message_json)
                msg_type = message.get("messageType")
                
                print(f"[RECV] {msg_type}")
                received_messages.append(msg_type)
                
                if msg_type == "TEXT":
                    content = message.get("content", "")
                    print(f"  Content: {content[:100]}...")
                elif msg_type == "TOOL":
                    tool_name = message.get("toolName")
                    print(f"  Tool: {tool_name}")
                elif msg_type == "TOOL_RESULT":
                    tool_name = message.get("toolName")
                    is_error = message.get("isError", False)
                    print(f"  Tool result: {tool_name} (error: {is_error})")
                elif msg_type == "API_CALL":
                    model = message.get("model", "unknown")
                    print(f"  API call for model: {model}")
                elif msg_type == "END_AI_TURN":
                    print(f"  ✓ END_AI_TURN received!")
                    self.acknowledge(msg_id)
                    return True, received_messages
                elif msg_type == "ERROR":
                    content = message.get("content", "")
                    print(f"  ERROR: {content}")
                    self.acknowledge(msg_id)
                    return False, received_messages
                
                self.acknowledge(msg_id)
            
            time.sleep(0.1)
        
        print(f"[TEST] Timeout after {timeout}s")
        return False, received_messages
    
    def close(self):
        self.conn.close()

def main():
    # Clean up old database
    if os.path.exists(DB_PATH):
        os.remove(DB_PATH)
        print(f"[TEST] Removed old database: {DB_PATH}")
    
    # Check if OPENAI_API_KEY is set
    if not os.environ.get("OPENAI_API_KEY"):
        print("[TEST] ERROR: OPENAI_API_KEY not set")
        print("[TEST] Please set OPENAI_API_KEY environment variable")
        sys.exit(1)
    
    # Start klawed in sqlite-queue mode
    print(f"[TEST] Starting klawed in sqlite-queue mode...")
    print(f"[TEST] Database: {DB_PATH}")
    
    klawed_process = subprocess.Popen(
        ["./build/klawed", "--sqlite-queue", DB_PATH],
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True
    )
    
    # Wait for klawed to initialize and create the database
    print("[TEST] Waiting for klawed to initialize...")
    max_wait = 10
    for i in range(max_wait):
        time.sleep(1)
        if os.path.exists(DB_PATH):
            # Wait a bit more for schema to be created
            time.sleep(1)
            break
        print(f"[TEST] Waiting for database... ({i+1}/{max_wait})")
    
    if not os.path.exists(DB_PATH):
        print("[TEST] ERROR: Database not created")
        klawed_process.kill()
        sys.exit(1)
    
    try:
        # Create client
        client = KlawedTestClient(DB_PATH, sender=SENDER, receiver=RECEIVER)
        
        # Send a simple test message
        print("\n[TEST] Sending test message...")
        client.send_message("TEXT", "What is 2+2? Just give me the number.")
        
        # Wait for completion
        success, messages = client.wait_for_completion(timeout=30)
        
        # Check results
        print("\n[TEST] ========================================")
        print(f"[TEST] Test Results:")
        print(f"[TEST] - Success: {success}")
        print(f"[TEST] - Messages received: {len(messages)}")
        print(f"[TEST] - Message types: {messages}")
        
        if "END_AI_TURN" in messages:
            print("[TEST] ✓ END_AI_TURN event was received!")
            exit_code = 0
        else:
            print("[TEST] ✗ END_AI_TURN event was NOT received")
            exit_code = 1
        
        print("[TEST] ========================================\n")
        
        # Clean up
        client.close()
        
    finally:
        # Stop klawed
        print("[TEST] Stopping klawed...")
        klawed_process.send_signal(signal.SIGTERM)
        try:
            klawed_process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            print("[TEST] Force killing klawed...")
            klawed_process.kill()
            klawed_process.wait()
        
        # Clean up database
        if os.path.exists(DB_PATH):
            os.remove(DB_PATH)
            print(f"[TEST] Cleaned up database: {DB_PATH}")
    
    sys.exit(exit_code)

if __name__ == "__main__":
    main()
