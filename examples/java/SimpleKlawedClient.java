import org.zeromq.SocketType;
import org.zeromq.ZMQ;
import org.zeromq.ZContext;
import com.fasterxml.jackson.databind.ObjectMapper;
import java.util.Map;

/**
 * Simple Java ZMQ client for Klawed - minimal implementation.
 * 
 * Dependencies (Maven):
 * <dependency>
 *   <groupId>org.zeromq</groupId>
 *   <artifactId>jeromq</artifactId>
 *   <version>0.5.3</version>
 * </dependency>
 * <dependency>
 *   <groupId>com.fasterxml.jackson.core</groupId>
 *   <artifactId>jackson-databind</artifactId>
 *   <version>2.15.2</version>
 * </dependency>
 */
public class SimpleKlawedClient {
    public static void main(String[] args) throws Exception {
        String endpoint = "tcp://127.0.0.1:5555";
        ObjectMapper mapper = new ObjectMapper();
        
        System.out.println("Connecting to Klawed at " + endpoint);
        
        try (ZContext context = new ZContext()) {
            ZMQ.Socket socket = context.createSocket(SocketType.PAIR);
            socket.setReceiveTimeOut(30000); // 30 second timeout
            socket.connect(endpoint);
            
            // Send text request
            String request = mapper.writeValueAsString(Map.of(
                "messageType", "TEXT",
                "content", "Write a hello world program in Java"
            ));
            
            System.out.println("Sending request: " + request);
            
            if (!socket.send(request)) {
                System.err.println("Failed to send message");
                return;
            }
            
            System.out.println("Waiting for response...");
            
            // Receive response
            byte[] responseBytes = socket.recv();
            if (responseBytes == null) {
                System.err.println("No response received (timeout)");
                return;
            }
            
            String responseStr = new String(responseBytes);
            System.out.println("Received response: " + responseStr);
            
            Map<String, Object> response = mapper.readValue(responseStr, Map.class);
            
            String messageType = (String) response.get("messageType");
            if ("TEXT".equals(messageType)) {
                System.out.println("\n=== AI Response ===\n");
                System.out.println(response.get("content"));
            } else if ("ERROR".equals(messageType)) {
                System.err.println("Error: " + response.get("content"));
            } else {
                System.err.println("Unknown message type: " + messageType);
            }
        }
    }
}