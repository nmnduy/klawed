# Issue: "Connecting..." Message Won't Complete

## Root Cause
The application was being accessed at the **wrong URL**.

## Problem
- Accessing: `http://localhost:8080/file-chat`
- Server actually running on: **port 9090**
- Correct endpoint: `/app` (not `/file-chat`)

## Solution
Access the application at the correct URL:
```
http://localhost:9090/app
```

## Why This Happened
1. The `application.properties` file has `quarkus.http.port=9090`
2. The server is running via `mvn quarkus:dev` on port 9090
3. The WebSocket endpoint is `/app/ws/{sessionId}` (not `/file-chat/ws/...`)
4. When accessing the wrong port/path, the JavaScript tries to connect to a non-existent WebSocket server

## Verification
```bash
# Check what port the server is running on
netstat -tlnp | grep java

# Expected output: port 9090 is listening
tcp6       0      0 :::9090                 :::*                    LISTEN

# Test the correct URL
curl -s -o /dev/null -w "%{http_code}" http://localhost:9090/app
# Should return: 303 (redirect) or 200
```

## Server Configuration
From `src/main/resources/application.properties`:
```properties
quarkus.http.port=9090
```

From `FileChatWebSocket.java`:
```java
@WebSocket(path = "/app/ws/{sessionId}")
```

## Login Credentials
Email: nmnduy@gmail.com
