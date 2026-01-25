#!/usr/bin/env python3
"""
FileSurf WebSocket Bridge Server for Testing

A Python-based testing tool that:
1. Authenticates with the FileSurf app
2. Creates a session
3. Maintains a persistent WebSocket connection
4. Provides HTTP endpoints to send messages and poll for responses

Usage:
    python3 ws_bridge_server.py [port]

Endpoints:
    POST /message - Send a message to the WebSocket
    GET /poll - Poll for responses from the WebSocket
    GET /status - Check connection status
    GET /session - Get current session ID
    DELETE /session - Close connection and create new session
    POST /close - Close the WebSocket connection

Example:
    # Start the server
    python3 ws_bridge_server.py 5000

    # In another terminal, send a message:
    curl -X POST http://localhost:5000/message \
      -H "Content-Type: text/plain" \
      -d "Generate a Solana wallet"

    # Poll for responses:
    curl http://localhost:5000/poll

    # Or in Python:
    import requests
    requests.post("http://localhost:5000/message", data="hello")
    print(requests.get("http://localhost:5000/poll").json())
"""

import asyncio
import json
import threading
import time
import uuid
import os
import sys
import signal
from datetime import datetime
from flask import Flask, request, jsonify, Response
import requests
import websockets
from urllib.parse import urljoin

app = Flask(__name__)

# Configuration
DEFAULT_HOST = "localhost"
DEFAULT_PORT = 9090
WEBSOCKET_PATH = "/app/ws/"

# Global state
state = {
    "cookie": None,
    "user_id": None,
    "email": None,
    "session_id": None,
    "websocket": None,
    "websocket_task": None,
    "responses": [],
    "connected": False,
    "lock": threading.Lock()
}


def signal_handler(sig, frame):
    """Handle shutdown signals gracefully"""
    print("\nShutting down bridge server...")
    close_websocket()
    sys.exit(0)


signal.signal(signal.SIGINT, signal_handler)
signal.signal(signal.SIGTERM, signal_handler)


def authenticate(host: str, port: int, email: str = "test@example.com") -> tuple:
    """
    Authenticate with the FileSurf app and get a session
    
    Returns:
        tuple: (cookie, user_id, session_id)
    """
    base_url = f"http://{host}:{port}"
    
    # Login
    login_response = requests.post(
        f"{base_url}/auth/login",
        data={"email": email},
        allow_redirects=False
    )
    
    if login_response.status_code != 200:
        raise Exception(f"Login failed: {login_response.status_code}")
    
    # Get cookie
    cookies = login_response.cookies
    cookie_str = "; ".join([f"{c.name}={c.value}" for c in cookies])
    user_id = None
    
    # Extract user_id from cookie
    for c in cookies:
        if c.name == "filesurf_userId":
            user_id = c.value
            break
    
    if not user_id:
        raise Exception("Could not extract user_id from login response")
    
    # Generate session
    session_response = requests.get(
        f"{base_url}/session/generate",
        cookies=cookies
    )
    
    if session_response.status_code != 200:
        raise Exception(f"Session generation failed: {session_response.status_code}")
    
    session_data = session_response.json()
    session_id = session_data.get("sessionId")
    
    print(f"✓ Authenticated: {email} (user_id: {user_id})")
    print(f"✓ Session created: {session_id}")
    
    return cookie_str, user_id, session_id


def connect_websocket(host: str, port: int, session_id: str, cookie: str) -> websockets.WebSocketClientProtocol:
    """
    Connect to the FileSurf WebSocket endpoint
    
    Returns:
        WebSocket: Connected WebSocket instance
    """
    ws_url = f"ws://{host}:{port}{WEBSOCKET_PATH}{session_id}"
    
    # Prepare headers with cookie
    headers = {
        "Cookie": cookie
    }
    
    print(f"Connecting to WebSocket: {ws_url}")
    
    websocket = asyncio.get_event_loop().run_until_complete(
        websockets.connect(ws_url, extra_headers=headers)
    )
    
    # Wait for connection and initial status message
    try:
        welcome_msg = asyncio.get_event_loop().run_until_complete(
            asyncio.wait_for(websocket.recv(), timeout=10)
        )
        print(f"✓ WebSocket connected")
        print(f"  Received: {welcome_msg[:100]}...")
    except Exception as e:
        print(f"Warning: Could not receive welcome message: {e}")
    
    return websocket


def close_websocket():
    """Close the WebSocket connection"""
    with state["lock"]:
        if state["websocket"]:
            try:
                asyncio.get_event_loop().run_until_complete(state["websocket"].close())
                print("✓ WebSocket closed")
            except Exception as e:
                print(f"Warning: Error closing WebSocket: {e}")
            
            state["websocket"] = None
            state["websocket_task"] = None
            state["connected"] = False


def websocket_listener():
    """
    Background thread to listen for WebSocket messages
    """
    while True:
        with state["lock"]:
            ws = state["websocket"]
            if not ws:
                time.sleep(0.5)
                continue
        
        try:
            # Use run_until_complete for synchronous context
            loop = asyncio.new_event_loop()
            asyncio.set_event_loop(loop)
            
            message = loop.run_until_complete(
                asyncio.wait_for(ws.recv(), timeout=1.0)
            )
            
            with state["lock"]:
                timestamp = datetime.now().isoformat()
                state["responses"].append({
                    "timestamp": timestamp,
                    "message": message,
                    "received_at": time.time()
                })
                
                # Keep only last 100 responses
                if len(state["responses"]) > 100:
                    state["responses"] = state["responses"][-100:]
            
            print(f"📩 Received: {message[:80]}...")
            
        except asyncio.TimeoutError:
            pass
        except websockets.exceptions.ConnectionClosed:
            print("⚠ WebSocket connection closed")
            with state["lock"]:
                state["websocket"] = None
                state["connected"] = False
            break
        except Exception as e:
            print(f"Error in listener: {e}")
            time.sleep(1)
        finally:
            loop.close()


def initialize_session(host: str, port: int, email: str = "test@example.com"):
    """
    Initialize a new session: authenticate and connect WebSocket
    """
    print("\n" + "="*60)
    print("  FileSurf WebSocket Bridge Server")
    print("="*60)
    print()
    
    # Close existing connection
    close_websocket()
    
    # Clear old responses
    with state["lock"]:
        state["responses"] = []
    
    # Authenticate
    print("Step 1: Authenticating with FileSurf...")
    cookie, user_id, session_id = authenticate(host, port, email)
    
    state["cookie"] = cookie
    state["user_id"] = user_id
    state["email"] = email
    state["session_id"] = session_id
    
    print()
    print("Step 2: Connecting to WebSocket...")
    ws = connect_websocket(host, port, session_id, cookie)
    
    state["websocket"] = ws
    state["connected"] = True
    
    # Start listener thread
    listener_thread = threading.Thread(target=websocket_listener, daemon=True)
    listener_thread.start()
    
    print()
    print("✓ Bridge server ready!")
    print()
    print("HTTP Endpoints:")
    print("  POST /message - Send a message")
    print("  GET  /poll     - Poll for responses")
    print("  GET  /status   - Check connection status")
    print("  GET  /session  - Get session ID")
    print("  DELETE /session - Close and create new session")
    print()
    print("="*60 + "\n")


@app.route("/", methods=["GET"])
def index():
    """Server status and usage info"""
    return f"""
    <h1>FileSurf WebSocket Bridge</h1>
    <p>Status: {'Connected' if state.get('connected') else 'Disconnected'}</p>
    <p>Session: {state.get('session_id', 'None')}</p>
    <p>User: {state.get('email', 'None')}</p>
    <hr>
    <h3>Endpoints:</h3>
    <ul>
        <li>POST /message - Send a message</li>
        <li>GET /poll - Poll for responses</li>
        <li>GET /status - Check status</li>
        <li>GET /session - Get session ID</li>
        <li>DELETE /session - New session</li>
        <li>POST /close - Close connection</li>
    </ul>
    """


@app.route("/message", methods=["POST"])
def send_message():
    """
    Send a message to the WebSocket
    
    Request body: Message text (plain text or JSON with 'content' field)
    """
    content_type = request.content_type or ""
    
    if "application/json" in content_type:
        try:
            data = request.get_json()
            if isinstance(data, dict):
                message = data.get("content", "")
            else:
                message = str(data)
        except Exception:
            message = str(request.get_data(), "utf-8")
    else:
        message = str(request.get_data(), "utf-8")
    
    if not message:
        return jsonify({"error": "No message provided"}), 400
    
    with state["lock"]:
        ws = state["websocket"]
        session_id = state.get("session_id")
    
    if not ws:
        return jsonify({"error": "WebSocket not connected"}), 503
    
    try:
        # Send message through WebSocket
        loop = asyncio.new_event_loop()
        asyncio.set_event_loop(loop)
        loop.run_until_complete(ws.send(message))
        loop.close()
        
        print(f"📤 Sent: {message[:80]}...")
        
        return jsonify({
            "status": "sent",
            "session_id": session_id,
            "message": message[:100] + ("..." if len(message) > 100 else "")
        })
    
    except Exception as e:
        print(f"Error sending message: {e}")
        return jsonify({"error": str(e)}), 500


@app.route("/poll", methods=["GET"])
def poll_responses():
    """
    Poll for responses from the WebSocket
    
    Query params:
        - timeout: seconds to wait (default: 2)
        - clear: if true, clear responses after reading (default: true)
    """
    timeout = min(int(request.args.get("timeout", 2)), 10)
    clear = request.args.get("clear", "true").lower() == "true"
    
    with state["lock"]:
        responses = list(state["responses"])
        session_id = state.get("session_id")
        connected = state.get("connected")
    
    # Wait for new responses if needed
    if clear and not responses:
        time.sleep(timeout)
        with state["lock"]:
            responses = list(state["responses"])
    
    result = {
        "status": "success",
        "session_id": session_id,
        "connected": connected,
        "count": len(responses),
        "messages": []
    }
    
    for resp in responses:
        result["messages"].append({
            "timestamp": resp["timestamp"],
            "content": resp["message"]
        })
    
    # Clear responses if requested
    if clear:
        with state["lock"]:
            state["responses"] = []
    
    return jsonify(result)


@app.route("/poll/stream", methods=["GET"])
def poll_stream():
    """
    Stream responses from the WebSocket (Server-Sent Events)
    
    Query params:
        - timeout: seconds to stream (default: 30)
    """
    timeout = min(int(request.args.get("timeout", 30)), 300)
    end_time = time.time() + timeout
    
    def generate():
        last_idx = 0
        while time.time() < end_time:
            with state["lock"]:
                responses = state["responses"]
            
            # Yield new responses
            for resp in responses[last_idx:]:
                yield f"data: {json.dumps({'timestamp': resp['timestamp'], 'content': resp['message']})}\n\n"
                last_idx = len(responses)
            
            time.sleep(0.1)
        
        yield "data: {\"event\": \"timeout\"}\n\n"
    
    return Response(generate(), mimetype="text/event-stream")


@app.route("/status", methods=["GET"])
def status():
    """Check connection status"""
    with state["lock"]:
        response_count = len(state.get("responses", []))
        connected = state.get("connected")
        session_id = state.get("session_id")
        user_id = state.get("user_id")
    
    return jsonify({
        "connected": connected,
        "session_id": session_id,
        "user_id": user_id,
        "pending_responses": response_count
    })


@app.route("/session", methods=["GET"])
def get_session():
    """Get current session ID"""
    return jsonify({
        "session_id": state.get("session_id"),
        "user_id": state.get("user_id"),
        "email": state.get("email")
    })


@app.route("/session", methods=["DELETE"])
def new_session():
    """Close current session and create a new one"""
    close_websocket()
    
    with state["lock"]:
        state["responses"] = []
        state["session_id"] = None
    
    # Get port from environment or use default
    port = int(os.environ.get("FILE_SURF_PORT", DEFAULT_PORT))
    host = os.environ.get("FILE_SURF_HOST", DEFAULT_HOST)
    
    try:
        initialize_session(host, port, state.get("email"))
    except Exception as e:
        return jsonify({"error": f"Failed to create new session: {e}"}), 500
    
    return jsonify({
        "status": "new_session_created",
        "session_id": state.get("session_id")
    })


@app.route("/close", methods=["POST"])
def close_connection():
    """Close the WebSocket connection"""
    close_websocket()
    
    return jsonify({
        "status": "closed"
    })


@app.route("/close/all", methods=["POST"])
def close_all():
    """Close everything and clean up"""
    close_websocket()
    
    with state["lock"]:
        state["responses"] = []
        state["cookie"] = None
        state["user_id"] = None
        state["email"] = None
        state["session_id"] = None
    
    return jsonify({
        "status": "cleaned_up"
    })


def main():
    """Main entry point"""
    import argparse
    
    parser = argparse.ArgumentParser(description="FileSurf WebSocket Bridge Server")
    parser.add_argument("--port", "-p", type=int, default=5000, help="HTTP server port")
    parser.add_argument("--file-surf-port", type=int, default=9090, help="FileSurf app port")
    parser.add_argument("--file-surf-host", type=str, default="localhost", help="FileSurf app host")
    parser.add_argument("--email", type=str, default="test@example.com", help="Email to authenticate with")
    
    args = parser.parse_args()
    
    # Set environment variables for Flask
    os.environ["FILE_SURF_PORT"] = str(args.file_surf_port)
    os.environ["FILE_SURF_HOST"] = args.file_surf_host
    
    # Initialize session
    try:
        initialize_session(args.file_surf_host, args.file_surf_port, args.email)
    except Exception as e:
        print(f"Error initializing session: {e}")
        print("\nMake sure the FileSurf app is running!")
        sys.exit(1)
    
    # Run Flask server
    print(f"\n🌐 Starting bridge server on http://localhost:{args.port}")
    print(f"📡 FileSurf app: http://{args.file_surf_host}:{args.file_surf_port}")
    print()
    
    app.run(host="0.0.0.0", port=args.port, debug=False, threaded=True)


if __name__ == "__main__":
    main()