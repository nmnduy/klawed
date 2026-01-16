package com.filesurf.service;

import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.io.TempDir;

import java.io.IOException;
import java.nio.file.Path;
import java.util.List;

import static org.junit.jupiter.api.Assertions.*;

/**
 * Test for SQLiteQueueClient
 */
public class SQLiteQueueClientTest {

    @TempDir
    Path tempDir;

    @Test
    public void testSQLiteQueueClientInitialization() throws IOException {
        // Create a test database file
        Path dbFile = tempDir.resolve("test_messages.db");

        // Create SQLiteQueueClient
        SQLiteQueueClient client = new SQLiteQueueClient(dbFile.toString());

        // Connect to database
        client.connect();

        // Verify connection
        assertTrue(client.isConnected());

        // Get queue stats (should be empty)
        SQLiteQueueClient.QueueStats stats = client.getQueueStats();
        assertNotNull(stats);
        assertEquals(0, stats.getPendingCount());
        assertEquals(0, stats.getTotalCount());
        assertEquals(0, stats.getUnreadCount());

        // Shutdown client
        client.shutdown();
    }

    @Test
    public void testSQLiteQueueClientSendMessage() throws IOException {
        // Create a test database file
        Path dbFile = tempDir.resolve("test_messages.db");

        // Create SQLiteQueueClient
        SQLiteQueueClient client = new SQLiteQueueClient(dbFile.toString());
        client.connect();

        // Send a test message
        String testMessage = "Hello, SQLite queue!";
        client.sendMessage(testMessage);

        // Get queue stats (should have 1 pending message)
        SQLiteQueueClient.QueueStats stats = client.getQueueStats();
        assertNotNull(stats);
        assertEquals(1, stats.getPendingCount());
        assertEquals(1, stats.getTotalCount());

        // Shutdown client
        client.shutdown();
    }

    @Test
    public void testSQLiteQueueClientWithConfiguration() throws IOException {
        // Create a test database file
        Path dbFile = tempDir.resolve("test_messages.db");

        // Create SQLiteQueueClient with custom configuration
        SQLiteQueueClient.Config config = new SQLiteQueueClient.Config(dbFile.toString())
            .withSenderName("test-sender")
            .withReceiverName("test-receiver")
            .withPollIntervalMs(50)
            .withPollTimeoutMs(5000)
            .withMaxMessageSize(512 * 1024); // 512KB

        SQLiteQueueClient client = new SQLiteQueueClient(config);
        client.connect();

        // Verify connection
        assertTrue(client.isConnected());

        // Send a test message
        String testMessage = "Test message with custom configuration";
        client.sendMessage(testMessage);

        // Shutdown client
        client.shutdown();
    }

    @Test
    public void testSQLiteQueueClientReceiveMessages() throws IOException {
        // Create a test database file
        Path dbFile = tempDir.resolve("test_messages.db");

        // Create SQLiteQueueClient
        SQLiteQueueClient client = new SQLiteQueueClient(dbFile.toString());
        client.connect();

        // Send a test message
        String testMessage = "Test message for receiving";
        client.sendMessage(testMessage);

        // Note: In a real test, we would need a klawed agent running
        // to process the message and send a response. This test just
        // verifies the client can send messages without errors.

        // Shutdown client
        client.shutdown();
    }

    @Test
    public void testSQLiteQueueClientSendAndReceive() throws IOException {
        // Create a test database file
        Path dbFile = tempDir.resolve("test_messages.db");

        // Create SQLiteQueueClient
        SQLiteQueueClient client = new SQLiteQueueClient(dbFile.toString());
        client.connect();

        // Try to send and receive (will timeout since no agent is running)
        String testMessage = "Test message for sendAndReceive";

        // This should throw IOException due to timeout
        assertThrows(IOException.class, () -> {
            client.sendAndReceive(testMessage, 1000); // 1 second timeout
        });

        // Shutdown client
        client.shutdown();
    }
}