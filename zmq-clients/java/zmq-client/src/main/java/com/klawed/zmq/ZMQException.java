package com.klawed.zmq;

/**
 * Exception thrown for ZMQ-related errors in the Klawed client.
 */
public class ZMQException extends Exception {
    
    public ZMQException(String message) {
        super(message);
    }
    
    public ZMQException(String message, Throwable cause) {
        super(message, cause);
    }
    
    public ZMQException(Throwable cause) {
        super(cause);
    }
}