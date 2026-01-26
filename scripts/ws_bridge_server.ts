/**
 * FileSurf WebSocket Bridge Server (Bun version)
 *
 * A testing tool that:
 * 1. Authenticates with the FileSurf app
 * 2. Creates a session
 * 3. Maintains a persistent WebSocket connection with proper headers
 * 4. Provides HTTP endpoints to send messages and poll for responses
 *
 * Usage:
 *   bun run ws_bridge_server.ts [bridge_port] [email] [filesurf_port]
 *
 * Arguments:
 *   bridge_port   - Port for the bridge server (default: 5000)
 *   email         - Email for authentication (default: test@example.com)
 *   filesurf_port - FileSurf application port (default: 9090)
 *
 * Endpoints:
 *   POST /message - Send a message to the WebSocket
 *   GET  /poll    - Poll for responses from the WebSocket
 *   GET  /status  - Check connection status
 *   GET  /session - Get current session ID
 *   DELETE /session - Close connection and create new session
 *   POST /close - Close the WebSocket connection
 *
 * Examples:
 *   # Start bridge on port 5000, connect to FileSurf on port 9090 (default)
 *   bun run ws_bridge_server.ts 5000
 *
 *   # Start bridge on port 5000 with custom email
 *   bun run ws_bridge_server.ts 5000 user@example.com
 *
 *   # Start bridge on port 5000 with custom email and FileSurf port 8080
 *   bun run ws_bridge_server.ts 5000 user@example.com 8080
 *
 *   # Send a message
 *   curl -X POST http://localhost:5000/message -d "List files"
 *
 *   # Poll for responses
 *   curl http://localhost:5000/poll
 */

import { Server } from "node:http";
import type { IncomingMessage, ServerResponse } from "node:http";
import { WebSocket, WebSocketServer } from "ws";
import type { WebSocket as WSWebSocket } from "ws";

// Configuration
const DEFAULT_HOST = "localhost";
const DEFAULT_FILESURF_PORT = 9090;
const DEFAULT_BRIDGE_PORT = 5000;
const DEFAULT_EMAIL = "test@example.com";

// Parse command-line arguments
const BRIDGE_PORT = parseInt(process.argv[2] || String(DEFAULT_BRIDGE_PORT));
const EMAIL = process.argv[3] || DEFAULT_EMAIL;
const FILESURF_PORT = process.argv[4] ? parseInt(process.argv[4]) : DEFAULT_FILESURF_PORT;

const WEBSOCKET_PATH = "/app/ws/";

// Global state
const state = {
  cookie: "",
  userId: "",
  email: "",
  sessionId: "",
  websocket: null as WSWebSocket | null,
  connected: false,
  responses: [] as Array<{ timestamp: string; message: string }>,
};

// Helper function to make HTTP requests
async function httpRequest(
  method: string,
  url: string,
  options?: {
    data?: Record<string, string> | string;
    cookies?: Record<string, string>;
    asJson?: boolean;
  }
): Promise<{
  status: number;
  cookies?: Record<string, string>;
  data?: any;
  text?: string;
}> {
  const headers: Record<string, string> = {};

  if (options?.cookies) {
    headers["Cookie"] = Object.entries(options.cookies)
      .map(([k, v]) => `${k}=${v}`)
      .join("; ");
  }

  let body: string | undefined;
  if (options?.data) {
    if (typeof options.data === "string") {
      body = options.data;
    } else {
      body = JSON.stringify(options.data);
      headers["Content-Type"] = "application/json";
    }
  }

  try {
    const response = await fetch(url, {
      method,
      headers,
      body,
      redirect: "manual",
    });

    const cookies: Record<string, string> = {};
    response.headers.forEach((value, key) => {
      if (key.toLowerCase() === "set-cookie") {
        // Parse set-cookie header
        const parts = value.split(";");
        const [nameVal] = parts[0].split("=");
        if (nameVal) {
          cookies[nameVal.trim()] = parts[0].split("=")[1]?.trim() || "";
        }
      }
    });

    const text = await response.text();

    return {
      status: response.status,
      cookies,
      data: options?.asJson ? (text ? JSON.parse(text) : null) : undefined,
      text,
    };
  } catch (error) {
    throw new Error(`HTTP request failed: ${error}`);
  }
}

function authenticate(host: string, port: number, email: string): Promise<{
  cookie: string;
  userId: string;
  sessionId: string;
}> {
  return new Promise(async (resolve, reject) => {
    const baseUrl = `http://${host}:${port}`;

    console.log(`\nStep 1: Authenticating with FileSurf...`);

    // Login
    const loginResponse = await httpRequest("POST", `${baseUrl}/auth/login`, {
      data: { email },
      asJson: true,
    });

    if (loginResponse.status !== 200) {
      reject(new Error(`Login failed: ${loginResponse.status}`));
      return;
    }

    // Extract user_id from login response
    const loginData = loginResponse.data;
    if (!loginData || !loginData.userId) {
      reject(new Error("Could not extract userId from login response"));
      return;
    }

    const userId = loginData.userId;
    const cookie = `filesurf_userId=${userId}`;

    console.log(`✓ Authenticated: ${email} (user_id: ${userId})`);

    // Generate session
    const sessionResponse = await httpRequest(
      "GET",
      `${baseUrl}/session/generate`,
      { cookies: { filesurf_userId: userId }, asJson: true }
    );

    if (sessionResponse.status !== 200) {
      reject(new Error(`Session generation failed: ${sessionResponse.status}`));
      return;
    }

    const sessionData = sessionResponse.data;
    const sessionId = sessionData.sessionId;

    console.log(`✓ Session created: ${sessionId}`);

    resolve({ cookie, userId, sessionId });
  });
}

function connectWebSocket(
  host: string,
  port: number,
  sessionId: string,
  cookie: string
): Promise<WSWebSocket> {
  return new Promise((resolve, reject) => {
    const wsUrl = `ws://${host}:${port}${WEBSOCKET_PATH}${sessionId}`;

    console.log(`\nStep 2: Connecting to WebSocket...`);
    console.log(`Connecting to: ${wsUrl}`);

    const ws = new WebSocket(wsUrl, {
      headers: {
        Cookie: cookie,
      },
    });

    ws.on("open", () => {
      console.log("✓ WebSocket connected");
      resolve(ws);
    });

    ws.on("error", (error) => {
      console.error(`✗ WebSocket connection error: ${error.message}`);
      reject(error);
    });

    ws.on("message", (data) => {
      const message = data.toString();
      const timestamp = new Date().toISOString();

      // Store response
      state.responses.push({ timestamp, message });

      // Keep only last 100 responses
      if (state.responses.length > 100) {
        state.responses = state.responses.slice(-100);
      }

      console.log(`📩 Received: ${message.substring(0, 80)}...`);
    });

    ws.on("close", () => {
      console.log("⚠ WebSocket connection closed");
      state.connected = false;
    });
  });
}

async function initializeSession(
  host: string,
  port: number,
  email: string
): Promise<void> {
  console.log("\n" + "=".repeat(60));
  console.log("  FileSurf WebSocket Bridge Server (Bun)");
  console.log("=".repeat(60));
  console.log();

  // Close existing connection
  if (state.websocket) {
    try {
      state.websocket.close();
    } catch (e) {
      // Ignore
    }
    state.websocket = null;
    state.connected = false;
  }

  // Clear old responses
  state.responses = [];

  // Authenticate
  try {
    const { cookie, userId, sessionId } = await authenticate(host, port, email);

    state.cookie = cookie;
    state.userId = userId;
    state.email = email;
    state.sessionId = sessionId;

    // Connect WebSocket
    const ws = await connectWebSocket(host, port, sessionId, cookie);
    state.websocket = ws;
    state.connected = true;

    console.log();
    console.log("✓ Bridge server ready!");
    console.log();
    console.log("HTTP Endpoints:");
    console.log("  POST /message - Send a message");
    console.log("  GET  /poll     - Poll for responses");
    console.log("  GET  /status   - Check connection status");
    console.log("  GET  /session  - Get session ID");
    console.log("  DELETE /session - Close and create new session");
    console.log("  POST /close - Close connection");
    console.log();
    console.log("=".repeat(60) + "\n");
  } catch (error) {
    console.error(`\nError initializing session: ${error}`);
    console.log("\nMake sure the FileSurf app is running!");
    process.exit(1);
  }
}

function sendMessage(message: string): { success: boolean; error?: string } {
  if (!state.websocket || !state.connected) {
    return { success: false, error: "WebSocket not connected" };
  }

  try {
    state.websocket.send(message);
    console.log(`📤 Sent: ${message.substring(0, 80)}...`);
    return { success: true };
  } catch (error) {
    console.error(`Error sending message: ${error}`);
    return { success: false, error: String(error) };
  }
}

function pollResponses(clear: boolean = true): {
  count: number;
  messages: Array<{ timestamp: string; content: string }>;
  connected: boolean;
} {
  const messages = state.responses.map((r) => ({
    timestamp: r.timestamp,
    content: r.message,
  }));

  if (clear) {
    state.responses = [];
  }

  return {
    count: messages.length,
    messages,
    connected: state.connected,
  };
}

// Parse request body
async function parseRequestBody(req: IncomingMessage): Promise<any> {
  return new Promise((resolve, reject) => {
    let body = "";
    req.on("data", (chunk) => {
      body += chunk.toString();
    });
    req.on("end", () => {
      const contentType = req.headers["content-type"] || "";

      if (contentType.includes("application/json")) {
        try {
          resolve(body ? JSON.parse(body) : null);
        } catch (e) {
          reject(new Error("Invalid JSON"));
        }
      } else {
        resolve(body);
      }
    });
    req.on("error", reject);
  });
}

// HTTP Server
const server = new Server(async (req: IncomingMessage, res: ServerResponse) => {
  const url = new URL(req.url || "/", `http://${req.headers.host}`);
  const path = url.pathname;

  // CORS headers
  res.setHeader("Access-Control-Allow-Origin", "*");
  res.setHeader("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
  res.setHeader("Access-Control-Allow-Headers", "Content-Type");

  if (req.method === "OPTIONS") {
    res.writeHead(204);
    res.end();
    return;
  }

  try {
    // POST /message - Send a message
    if (path === "/message" && req.method === "POST") {
      const body = await parseRequest(req);
      let message: string;

      if (typeof body === "string") {
        message = body;
      } else if (body && typeof body.content === "string") {
        message = body.content;
      } else {
        res.writeHead(400, { "Content-Type": "application/json" });
        res.end(JSON.stringify({ error: "Invalid message format" }));
        return;
      }

      if (!message) {
        res.writeHead(400, { "Content-Type": "application/json" });
        res.end(JSON.stringify({ error: "No message provided" }));
        return;
      }

      const result = sendMessage(message);

      if (result.success) {
        res.writeHead(200, { "Content-Type": "application/json" });
        res.end(
          JSON.stringify({
            status: "sent",
            session_id: state.sessionId,
            message: message.length > 100 ? message.substring(0, 100) + "..." : message,
          })
        );
      } else {
        res.writeHead(503, { "Content-Type": "application/json" });
        res.end(JSON.stringify({ error: result.error }));
      }
      return;
    }

    // GET /poll - Poll for responses
    if (path === "/poll" && req.method === "GET") {
      const timeout = Math.min(parseInt(url.searchParams.get("timeout") || "2"), 10);
      const clear = url.searchParams.get("clear") !== "false";

      // Wait for responses if needed
      if (clear && state.responses.length === 0) {
        await new Promise((resolve) => setTimeout(resolve, timeout * 1000));
      }

      const result = pollResponses(clear);

      res.writeHead(200, { "Content-Type": "application/json" });
      res.end(
        JSON.stringify({
          status: "success",
          session_id: state.sessionId,
          connected: result.connected,
          count: result.count,
          messages: result.messages,
        })
      );
      return;
    }

    // GET /status - Check connection status
    if (path === "/status" && req.method === "GET") {
      res.writeHead(200, { "Content-Type": "application/json" });
      res.end(
        JSON.stringify({
          connected: state.connected,
          session_id: state.sessionId,
          user_id: state.userId,
          pending_responses: state.responses.length,
        })
      );
      return;
    }

    // GET /session - Get current session ID
    if (path === "/session" && req.method === "GET") {
      res.writeHead(200, { "Content-Type": "application/json" });
      res.end(
        JSON.stringify({
          session_id: state.sessionId,
          user_id: state.userId,
          email: state.email,
        })
      );
      return;
    }

    // DELETE /session - Close and create new session
    if (path === "/session" && req.method === "DELETE") {
      if (state.websocket) {
        state.websocket.close();
        state.websocket = null;
        state.connected = false;
      }

      state.responses = [];
      state.sessionId = "";

      const host = process.env.FILE_SURF_HOST || DEFAULT_HOST;
      const port = process.env.FILE_SURF_PORT 
        ? parseInt(process.env.FILE_SURF_PORT) 
        : FILESURF_PORT;

      await initializeSession(host, port, state.email);

      res.writeHead(200, { "Content-Type": "application/json" });
      res.end(
        JSON.stringify({
          status: "new_session_created",
          session_id: state.sessionId,
        })
      );
      return;
    }

    // POST /close - Close the WebSocket connection
    if (path === "/close" && req.method === "POST") {
      if (state.websocket) {
        state.websocket.close();
        state.websocket = null;
        state.connected = false;
      }

      res.writeHead(200, { "Content-Type": "application/json" });
      res.end(JSON.stringify({ status: "closed" }));
      return;
    }

    // GET / - Server status
    if (path === "/" && req.method === "GET") {
      res.writeHead(200, { "Content-Type": "text/html" });
      res.end(`
        <h1>FileSurf WebSocket Bridge (Bun)</h1>
        <p>Status: ${state.connected ? "Connected" : "Disconnected"}</p>
        <p>Session: ${state.sessionId || "None"}</p>
        <p>User: ${state.email || "None"}</p>
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
      `);
      return;
    }

    // 404
    res.writeHead(404);
    res.end("Not Found");
  } catch (error) {
    console.error(`Error handling request: ${error}`);
    res.writeHead(500, { "Content-Type": "application/json" });
    res.end(JSON.stringify({ error: String(error) }));
  }
});

// Parse request body helper
async function parseRequest(req: IncomingMessage): Promise<any> {
  return new Promise((resolve, reject) => {
    let body = "";
    req.on("data", (chunk) => {
      body += chunk.toString();
    });
    req.on("end", () => {
      const contentType = req.headers["content-type"] || "";

      if (contentType.includes("application/json")) {
        try {
          resolve(body ? JSON.parse(body) : null);
        } catch (e) {
          reject(new Error("Invalid JSON"));
        }
      } else {
        resolve(body);
      }
    });
    req.on("error", reject);
  });
}

// Graceful shutdown
process.on("SIGINT", () => {
  console.log("\nShutting down bridge server...");
  if (state.websocket) {
    state.websocket.close();
  }
  server.close(() => {
    process.exit(0);
  });
});

process.on("SIGTERM", () => {
  console.log("\nShutting down bridge server...");
  if (state.websocket) {
    state.websocket.close();
  }
  server.close(() => {
    process.exit(0);
  });
});

// Start the server
async function main() {
  // Use environment variable if set, otherwise use command-line argument or default
  const filesurfPort = process.env.FILE_SURF_PORT 
    ? parseInt(process.env.FILE_SURF_PORT) 
    : FILESURF_PORT;
  const host = process.env.FILE_SURF_HOST || DEFAULT_HOST;

  await initializeSession(host, filesurfPort, EMAIL);

  server.listen(BRIDGE_PORT, "0.0.0.0", () => {
    console.log(`\n🌐 Starting bridge server on http://localhost:${BRIDGE_PORT}`);
    console.log(`📡 FileSurf app: http://${host}:${filesurfPort}`);
    console.log();
  });
}

main().catch((error) => {
  console.error("Failed to start bridge server:", error);
  process.exit(1);
});
