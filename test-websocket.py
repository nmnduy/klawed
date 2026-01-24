#!/usr/bin/env python3
"""
Test WebSocket connection with persisted session
"""
import sys
import json
import asyncio
import websockets

async def test_websocket(session_id, cookie_value):
    uri = f"ws://localhost:9090/app/ws/{session_id}"
    headers = [
        ("Cookie", f"filesurf_userId={cookie_value}")
    ]
    
    print(f"Connecting to WebSocket: {uri}")
    print(f"Using cookie: filesurf_userId={cookie_value}")
    print(f"Session ID: {session_id}")
    print()
    
    try:
        async with websockets.connect(uri, additional_headers=headers) as websocket:
            # Wait for initial message
            response = await asyncio.wait_for(websocket.recv(), timeout=5.0)
            print(f"Connected! Initial response:")
            print(response)
            print()
            
            # Try sending a simple message
            test_message = {
                "messageType": "USER_MESSAGE",
                "content": "Hello from test script!"
            }
            await websocket.send(json.dumps(test_message))
            print("Sent test message")
            
            # Wait for response
            response = await asyncio.wait_for(websocket.recv(), timeout=5.0)
            print(f"Response received:")
            print(response)
            
            return True
    except websockets.exceptions.ConnectionClosed as e:
        print(f"ERROR: Connection closed: {e}")
        return False
    except asyncio.TimeoutError:
        print("ERROR: Timeout waiting for response")
        return False
    except Exception as e:
        print(f"ERROR: {type(e).__name__}: {e}")
        return False

if __name__ == "__main__":
    if len(sys.argv) != 3:
        print("Usage: python3 test-websocket.py <session_id> <user_id>")
        sys.exit(1)
    
    session_id = sys.argv[1]
    user_id = sys.argv[2]
    
    success = asyncio.run(test_websocket(session_id, user_id))
    sys.exit(0 if success else 1)
