import org.zeromq.SocketType;
import org.zeromq.ZMQ;
import org.zeromq.ZContext;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.JsonNode;
import java.util.Map;
import java.util.concurrent.*;

/**
 * Robust Java ZMQ client with basic reconnection and error handling.
 * 
 * Features:
 * - Automatic reconnection on failure
 * - Timeout handling
 * - Basic error recovery
 * - Simple message queuing
 */
public class RobustKlawedClient {
    private static final String ENDPOINT = "tcp://127.0.0.1:5555";
    private static final int MAX_RETRIES = 3;
    private static final int RETRY_DELAY_MS = 1000;
    private static final int SOCKET_TIMEOUT_MS = 30000;
    
    private final ObjectMapper mapper = new ObjectMapper();
    private ZContext context;
    private ZMQ.Socket socket;
    private boolean connected = false;
    
    public RobustKlawedClient() {
        this.context = new ZContext();
    }
    
    /**
     * Connect to Klawed with retry logic
     */
    public boolean connect() {
        System.out.println("Connecting to Klawed at " + ENDPOINT);
        
        for (int attempt = 1; attempt <= MAX_RETRIES; attempt++) {
            try {
                if (socket != null) {
                    socket.close();
                }
                
                socket = context.createSocket(SocketType.PAIR);
                socket.setReceiveTimeOut(SOCKET_TIMEOUT_MS);
                socket.setSendTimeOut(10000);
                socket.connect(ENDPOINT);
                
                // Test connection with a simple ping
                if (testConnection()) {
                    connected = true;
                    System.out.println("Connected successfully on attempt " + attempt);
                    return true;
                }
            } catch (Exception e) {
                System.err.println("Connection attempt " + attempt + " failed: " + e.getMessage());
            }
            
            if (attempt < MAX_RETRIES) {
                System.out.println("Retrying in " + RETRY_DELAY_MS + "ms...");
                try {
                    Thread.sleep(RETRY_DELAY_MS);
                } catch (InterruptedException ie) {
                    Thread.currentThread().interrupt();
                    break;
                }
            }
        }
        
        System.err.println("Failed to connect after " + MAX_RETRIES + " attempts");
        return false;
    }
    
    /**
     * Test connection by sending a heartbeat ping
     */
    private boolean testConnection() {
        try {
            String ping = mapper.writeValueAsString(Map.of(
                "messageType", "HEARTBEAT_PING",
                "timestamp", System.currentTimeMillis()
            ));
            
            if (socket.send(ping, ZMQ.DONTWAIT)) {
                byte[] response = socket.recv(ZMQ.DONTWAIT);
                if (response != null) {
                    JsonNode json = mapper.readTree(new String(response));
                    return "HEARTBEAT_PONG".equals(json.get("messageType").asText());
                }
            }
        } catch (Exception e) {
            // Ignore test failures
        }
        return false;
    }
    
    /**
     * Send a message with retry logic
     */
    public String sendMessage(String content) throws Exception {
        if (!connected && !connect()) {
            throw new RuntimeException("Not connected to Klawed");
        }
        
        String request = mapper.writeValueAsString(Map.of(
            "messageType", "TEXT",
            "content", content
        ));
        
        System.out.println("Sending: " + content.substring(0, Math.min(50, content.length())) + "...");
        
        for (int attempt = 1; attempt <= MAX_RETRIES; attempt++) {
            try {
                if (!socket.send(request)) {
                    throw new RuntimeException("Send failed");
                }
                
                // Wait for response
                byte[] responseBytes = socket.recv();
                if (responseBytes == null) {
                    throw new RuntimeException("Receive timeout");
                }
                
                String responseStr = new String(responseBytes);
                JsonNode response = mapper.readTree(responseStr);
                String messageType = response.get("messageType").asText();
                
                if ("TEXT".equals(messageType)) {
                    return response.get("content").asText();
                } else if ("ERROR".equals(messageType)) {
                    throw new RuntimeException("Klawed error: " + response.get("content").asText());
                } else {
                    throw new RuntimeException("Unexpected message type: " + messageType);
                }
                
            } catch (Exception e) {
                System.err.println("Attempt " + attempt + " failed: " + e.getMessage());
                
                if (attempt < MAX_RETRIES) {
                    // Reconnect and retry
                    connected = false;
                    if (!connect()) {
                        continue;
                    }
                    System.out.println("Retrying request...");
                } else {
                    throw new RuntimeException("Failed after " + MAX_RETRIES + " attempts: " + e.getMessage(), e);
                }
            }
        }
        
        throw new RuntimeException("Unexpected error");
    }
    
    /**
     * Close connection and cleanup
     */
    public void close() {
        if (socket != null) {
            socket.close();
            socket = null;
        }
        if (context != null) {
            context.close();
            context = null;
        }
        connected = false;
    }
    
    /**
     * Example usage
     */
    public static void main(String[] args) throws Exception {
        RobustKlawedClient client = new RobustKlawedClient();
        
        try {
            if (!client.connect()) {
                System.err.println("Failed to connect, exiting");
                return;
            }
            
            // Example 1: Simple request
            System.out.println("\n=== Example 1: Simple Request ===");
            String response1 = client.sendMessage("Write a hello world program in Java");
            System.out.println("Response:\n" + response1);
            
            // Example 2: Follow-up request
            System.out.println("\n=== Example 2: Follow-up Request ===");
            String response2 = client.sendMessage("Now write a function to calculate factorial in Java");
            System.out.println("Response:\n" + response2);
            
            // Example 3: Request that might trigger tools
            System.out.println("\n=== Example 3: Request with Tools ===");
            String response3 = client.sendMessage("What files are in the current directory?");
            System.out.println("Response:\n" + response3);
            
        } catch (Exception e) {
            System.err.println("Error: " + e.getMessage());
            e.printStackTrace();
        } finally {
            client.close();
        }
    }
}