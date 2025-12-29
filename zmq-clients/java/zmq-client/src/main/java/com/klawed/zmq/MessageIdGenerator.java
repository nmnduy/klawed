package com.klawed.zmq;

import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;
import java.security.SecureRandom;
import java.time.Instant;
import java.util.HexFormat;

/**
 * Generates unique message IDs for reliable message delivery.
 * Based on the C implementation in Klawed's zmq_socket.c.
 */
class MessageIdGenerator {
    private static final int MESSAGE_ID_HEX_LENGTH = 33; // 32 hex chars + null terminator
    private static final int HASH_SAMPLE_SIZE = 256;
    private static final long FNV_PRIME_64_1 = 1099511628211L;
    private static final long FNV_PRIME_64_2 = 16777619L;
    private static final long MIX_CONSTANT_1 = 0xff51afd7ed558ccdL;
    private static final long MIX_CONSTANT_2 = 0xc4ceb9fe1a85ec53L;
    
    private final long salt;
    private final SecureRandom random;
    
    /**
     * Create a new message ID generator with a random salt.
     */
    public MessageIdGenerator() {
        this.random = new SecureRandom();
        this.salt = random.nextLong();
    }
    
    /**
     * Create a new message ID generator with a specific salt.
     * @param salt The salt value to use
     */
    public MessageIdGenerator(long salt) {
        this.random = new SecureRandom();
        this.salt = salt;
    }
    
    /**
     * Generate a unique message ID based on timestamp, message content, and salt.
     * @param message The message content
     * @param messageLen Length of the message content
     * @return A 32-character hex string message ID
     */
    public String generateMessageId(String message, int messageLen) {
        long timestampMs = Instant.now().toEpochMilli();
        
        // Combine timestamp and salt into 128-bit seed
        long seed1 = timestampMs ^ (salt << 32);
        long seed2 = timestampMs * salt;
        
        // Initialize with seeds
        long h1 = seed1;
        long h2 = seed2;
        
        // Sample first HASH_SAMPLE_SIZE characters (or entire message if shorter)
        int sampleLen = Math.min(messageLen, HASH_SAMPLE_SIZE);
        
        // Process message sample
        for (int i = 0; i < sampleLen; i++) {
            char c = message.charAt(i);
            
            // FNV-1a style mixing on both 64-bit parts
            h1 ^= (long) c;
            h1 *= FNV_PRIME_64_1;
            
            // Mix into second half with different pattern
            h2 ^= (long) c << (i % 8);
            h2 *= FNV_PRIME_64_2;
        }
        
        // Add some additional mixing using salt
        h1 ^= salt;
        h2 ^= salt << 16;
        
        // Final mixing
        h1 ^= h1 >>> 33;
        h1 *= MIX_CONSTANT_1;
        h1 ^= h1 >>> 33;
        
        h2 ^= h2 >>> 33;
        h2 *= MIX_CONSTANT_2;
        h2 ^= h2 >>> 33;
        
        // Combine into 128-bit output (16 bytes)
        byte[] hash = new byte[16];
        
        // Convert h1 to bytes (little-endian)
        for (int i = 0; i < 8; i++) {
            hash[i] = (byte) (h1 & 0xFF);
            h1 >>>= 8;
        }
        
        // Convert h2 to bytes (little-endian)
        for (int i = 0; i < 8; i++) {
            hash[8 + i] = (byte) (h2 & 0xFF);
            h2 >>>= 8;
        }
        
        // Convert to hex string
        return HexFormat.of().formatHex(hash);
    }
    
    /**
     * Generate a unique message ID for a message string.
     * @param message The message content
     * @return A 32-character hex string message ID
     */
    public String generateMessageId(String message) {
        return generateMessageId(message, message.length());
    }
    
    /**
     * Get the salt value used by this generator.
     * @return The salt value
     */
    public long getSalt() {
        return salt;
    }
    
    /**
     * Get the expected length of message ID strings.
     * @return The length in characters (32)
     */
    public static int getMessageIdLength() {
        return MESSAGE_ID_HEX_LENGTH - 1; // Exclude null terminator
    }
}