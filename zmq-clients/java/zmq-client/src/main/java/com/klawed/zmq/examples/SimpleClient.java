package com.klawed.zmq.examples;

import com.klawed.zmq.KlawedZMQClient;
import com.klawed.zmq.Message;
import com.klawed.zmq.ZMQException;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

/**
 * Simple example demonstrating basic usage of the Klawed ZMQ client.
 * This example sends a text request and prints the response.
 */
public class SimpleClient {
    private static final Logger logger = LoggerFactory.getLogger(SimpleClient.class);
    
    public static void main(String[] args) {
        if (args.length < 1) {
            System.err.println("Usage: SimpleClient <zmq-endpoint> [prompt]");
            System.err.println("Example: SimpleClient tcp://127.0.0.1:5555 \"What is 2+2?\"");
            System.exit(1);
        }
        
        String endpoint = args[0];
        String prompt = args.length > 1 ? args[1] : "What is 2+2?";
        
        try (KlawedZMQClient client = new KlawedZMQClient(endpoint)) {
            logger.info("Connected to {}", endpoint);
            logger.info("Sending prompt: {}", prompt);
            
            // Send text request
            Message response = client.sendText(prompt);
            
            if (response != null) {
                logger.info("Response received:");
                System.out.println(response.getContent());
            } else {
                logger.warn("No response received");
            }
            
        } catch (ZMQException e) {
            logger.error("ZMQ error: {}", e.getMessage(), e);
            System.exit(1);
        } catch (Exception e) {
            logger.error("Unexpected error: {}", e.getMessage(), e);
            System.exit(1);
        }
    }
}