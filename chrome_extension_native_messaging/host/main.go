// Klawed Browser Controller - Go Native Messaging Host
//
// This program bridges the Chrome Native Messaging Protocol (stdin/stdout)
// with a Unix domain socket that klawed connects to, enabling klawed to
// control a real Chrome/Chromium browser.
//
// Multi-agent support: each agent process (identified by PID+hostname hash)
// gets its own browser tab. Commands are automatically routed to the agent's
// assigned tab. Multiple klawed processes (main + subagents) can share a
// single browser without interfering with each other.
//
// Architecture:
//
//   klawed agent(s) ──┐
//   klawed subagent ──┤  JSON over Unix socket (/tmp/klawed-browser.sock)
//   explore agent ────┘
//       │
//       ▼
//   This Go host process (agent session manager)
//       │  Chrome Native Messaging (4-byte LE length + JSON on stdin/stdout)
//       ▼
//   Chrome Extension (background service worker)
//       │  Chrome APIs (tabs, scripting, etc.)
//       ▼
//   Real Chrome browser (one tab per agent)
//
// The Go host is launched by Chrome when the extension calls
// chrome.runtime.connectNative("com.klawed.browser_controller").
//
// Environment variables:
//   KLAWED_BROWSER_SOCKET  - Unix socket path (default: /tmp/klawed-browser.sock)
//   KLAWED_BROWSER_LOG     - Log file path (default: /tmp/klawed-browser-host.log)
package main

import (
	"bufio"
	"encoding/binary"
	"encoding/json"
	"fmt"
	"io"
	"log"
	"math/rand"
	"net"
	"os"
	"os/signal"
	"sync"
	"syscall"
	"time"
)

const (
	defaultSocketPath    = "/tmp/klawed-browser.sock"
	defaultLogPath       = "/tmp/klawed-browser-host.log"
	responseTimeout      = 30 * time.Second
	maxMessageSize       = 4 * 1024 * 1024 // 4MB
	agentSessionTTL      = 10 * time.Minute // Close orphaned agent tabs after 10min of inactivity
	cleanupInterval      = 2 * time.Minute  // How often to check for stale sessions
)

// Message is the common structure for all messages between components.
type Message struct {
	ID         string          `json:"id,omitempty"`
	Command    string          `json:"command,omitempty"`
	Params     json.RawMessage `json:"params,omitempty"`
	Result     json.RawMessage `json:"result,omitempty"`
	Error      string          `json:"error,omitempty"`
	Type       string          `json:"type,omitempty"`
	AgentID    string          `json:"agentId,omitempty"`
	NewSession bool            `json:"newSession,omitempty"`
}

// agentSession tracks one agent's assigned browser tab.
type agentSession struct {
	tabID    int
	lastSeen time.Time
}

var (
	// pending maps message IDs to response channels
	pending   = make(map[string]chan Message)
	pendingMu sync.Mutex

	// agentSessions maps agentId → tab assignment
	agentSessions   = make(map[string]*agentSession)
	agentSessionsMu sync.Mutex

	// writeMu serializes writes to Chrome stdout
	writeMu sync.Mutex

	logger *log.Logger
)

// ─── Commands that are tab-aware (routed to agent's assigned tab) ────────────
var tabAwareCommands = map[string]bool{
	"navigate":         true,
	"navigateTab":      true,
	"goBack":           true,
	"goForward":        true,
	"reload":           true,
	"click":            true,
	"type":             true,
	"pressKey":         true,
	"getText":          true,
	"getHtml":          true,
	"getAttribute":     true,
	"getPageInfo":      true,
	"getPageSource":    true,
	"getReadableText":  true,
	"scroll":           true,
	"scrollBy":         true,
	"scrollToElement":  true,
	"evaluate":         true,
	"findElements":     true,
	"getLinks":         true,
	"getForms":         true,
	"fillForm":         true,
	"submitForm":       true,
	"waitForElement":   true,
	"uploadFile":       true,
	"screenshot":       true,
}

// ─── Commands that don't need tab routing (shared/global) ────────────────────
var sharedCommands = map[string]bool{
	"listTabs":    true,
	"getActiveTab": true,
	"newTab":      true,
	"closeTab":    true,
	"switchTab":   true,
	"ping":        true,
	"getInfo":     true,
}

func initLogger() {
	logPath := os.Getenv("KLAWED_BROWSER_LOG")
	if logPath == "" {
		logPath = defaultLogPath
	}
	f, err := os.OpenFile(logPath, os.O_APPEND|os.O_CREATE|os.O_WRONLY, 0644)
	if err != nil {
		logger = log.New(io.Discard, "", 0)
		return
	}
	logger = log.New(f, "[klawed-browser-host] ", log.LstdFlags)
}

func readNativeMessage(r io.Reader) ([]byte, error) {
	var length uint32
	if err := binary.Read(r, binary.LittleEndian, &length); err != nil {
		return nil, err
	}
	if length > maxMessageSize {
		return nil, fmt.Errorf("message too large: %d bytes", length)
	}
	buf := make([]byte, length)
	if _, err := io.ReadFull(r, buf); err != nil {
		return nil, err
	}
	return buf, nil
}

func writeNativeMessage(w io.Writer, data []byte) error {
	writeMu.Lock()
	defer writeMu.Unlock()
	length := uint32(len(data))
	if err := binary.Write(w, binary.LittleEndian, length); err != nil {
		return err
	}
	_, err := w.Write(data)
	return err
}

func sendToChrome(msg Message) error {
	data, err := json.Marshal(msg)
	if err != nil {
		return fmt.Errorf("marshal error: %w", err)
	}
	logger.Printf("→ Chrome: %s", string(data))
	return writeNativeMessage(os.Stdout, data)
}

func generateID() string {
	return fmt.Sprintf("%d-%d", time.Now().UnixNano(), rand.Int63())
}

// ─── Agent Session Management ────────────────────────────────────────────────

// getOrCreateAgentTab returns the tab ID for an agent, creating a new tab if needed.
// Returns the tab ID and a message string (empty if tab already existed).
func getOrCreateAgentTab(agentID string, forceNew bool) (int, string, error) {
	agentSessionsMu.Lock()
	defer agentSessionsMu.Unlock()

	// Check if agent already has a valid session
	if !forceNew {
		if sess, exists := agentSessions[agentID]; exists {
			// Verify the tab still exists by trying to get it
			tabExists := verifyTabExists(sess.tabID)
			if tabExists {
				sess.lastSeen = time.Now()
				return sess.tabID, "", nil
			}
			// Tab was closed externally — remove stale session
			logger.Printf("agent %s tab %d was closed externally, creating new tab", agentID, sess.tabID)
			delete(agentSessions, agentID)
		}
	} else {
		logger.Printf("agent %s requested new session (force)", agentID)
	}

	// Create a new tab for this agent
	tabID, err := createAgentTab(agentID)
	if err != nil {
		return 0, "", fmt.Errorf("failed to create tab for agent %s: %w", agentID, err)
	}

	msg := fmt.Sprintf("New browser session created for agent %s (tab %d)", agentID, tabID)
	agentSessions[agentID] = &agentSession{
		tabID:    tabID,
		lastSeen: time.Now(),
	}
	logger.Printf("created tab %d for agent %s", tabID, agentID)
	return tabID, msg, nil
}

// verifyTabExists checks whether a tab ID is still valid by sending a ping
// through the extension. Returns true if the tab exists.
func verifyTabExists(tabID int) bool {
	// We use a special "verifyTab" command that the extension handles.
	// Avoids the overhead of a full listTabs call.
	msg := Message{
		ID:      generateID(),
		Command: "verifyTab",
		Params:  json.RawMessage(fmt.Sprintf(`{"tabId":%d}`, tabID)),
	}
	data, err := json.Marshal(msg)
	if err != nil {
		return false
	}

	ch := make(chan Message, 1)
	pendingMu.Lock()
	pending[msg.ID] = ch
	pendingMu.Unlock()
	defer func() {
		pendingMu.Lock()
		delete(pending, msg.ID)
		pendingMu.Unlock()
	}()

	if err := writeNativeMessage(os.Stdout, data); err != nil {
		return false
	}

	select {
	case resp := <-ch:
		var result struct {
			Exists bool `json:"exists"`
		}
		if resp.Result != nil {
			json.Unmarshal(resp.Result, &result)
		}
		return result.Exists
	case <-time.After(3 * time.Second):
		return false
	}
}

// createAgentTab creates a new browser tab via the Chrome extension.
func createAgentTab(agentID string) (int, error) {
	url := "about:blank"
	title := fmt.Sprintf("Klawed Agent [%s]", agentID)

	// Use newTab command with a special title
	// The extension will create the tab and set its title
	paramsJSON := fmt.Sprintf(`{"url":"%s","title":"%s"}`, url, title)
	msg := Message{
		ID:      generateID(),
		Command: "newTab",
		Params:  json.RawMessage(paramsJSON),
	}
	data, err := json.Marshal(msg)
	if err != nil {
		return 0, err
	}

	ch := make(chan Message, 1)
	pendingMu.Lock()
	pending[msg.ID] = ch
	pendingMu.Unlock()
	defer func() {
		pendingMu.Lock()
		delete(pending, msg.ID)
		pendingMu.Unlock()
	}()

	logger.Printf("→ Chrome (create tab for %s): %s", agentID, string(data))
	writeMu.Lock()
	writeErr := writeNativeMessage(os.Stdout, data)
	writeMu.Unlock()
	if writeErr != nil {
		return 0, writeErr
	}

	select {
	case resp := <-ch:
		if resp.Error != "" {
			return 0, fmt.Errorf("Chrome error: %s", resp.Error)
		}
		var result struct {
			TabID int    `json:"tabId"`
			URL   string `json:"url"`
		}
		if resp.Result != nil {
			if err := json.Unmarshal(resp.Result, &result); err != nil {
				return 0, fmt.Errorf("failed to parse tab result: %w", err)
			}
		}
		if result.TabID == 0 {
			return 0, fmt.Errorf("no tab ID in response")
		}
		return result.TabID, nil
	case <-time.After(10 * time.Second):
		return 0, fmt.Errorf("timeout creating tab for agent %s", agentID)
	}
}

// injectTabIDIntoParams adds the agent's tabId to the params JSON.
// If params already has a tabId, it's overridden (agent's tab takes priority).
func injectTabIDIntoParams(params json.RawMessage, tabID int) (json.RawMessage, error) {
	if len(params) == 0 || string(params) == "{}" {
		return json.RawMessage(fmt.Sprintf(`{"tabId":%d}`, tabID)), nil
	}

	var m map[string]any
	if err := json.Unmarshal(params, &m); err != nil {
		return nil, fmt.Errorf("failed to parse params: %w", err)
	}
	m["tabId"] = tabID
	b, err := json.Marshal(m)
	if err != nil {
		return nil, fmt.Errorf("failed to marshal params: %w", err)
	}
	return json.RawMessage(b), nil
}

// touchAgentSession updates the lastSeen timestamp for an agent.
func touchAgentSession(agentID string) {
	agentSessionsMu.Lock()
	defer agentSessionsMu.Unlock()
	if sess, exists := agentSessions[agentID]; exists {
		sess.lastSeen = time.Now()
	}
}

// cleanupStaleSessions periodically closes tabs for agents that haven't
// been active for longer than agentSessionTTL.
func cleanupStaleSessions() {
	ticker := time.NewTicker(cleanupInterval)
	defer ticker.Stop()

	for range ticker.C {
		agentSessionsMu.Lock()
		var staleAgents []string
		now := time.Now()
		for agentID, sess := range agentSessions {
			if now.Sub(sess.lastSeen) > agentSessionTTL {
				staleAgents = append(staleAgents, agentID)
			}
		}
		// Don't hold lock while closing tabs (network calls)
		agentSessionsMu.Unlock()

		for _, agentID := range staleAgents {
			logger.Printf("cleaning up stale session for agent %s", agentID)
			closeAgentTab(agentID)
		}
	}
}

// closeAgentTab closes the tab and removes the session.
func closeAgentTab(agentID string) {
	agentSessionsMu.Lock()
	sess, exists := agentSessions[agentID]
	if !exists {
		agentSessionsMu.Unlock()
		return
	}
	tabID := sess.tabID
	delete(agentSessions, agentID)
	agentSessionsMu.Unlock()

	// Close the tab via Chrome
	paramsJSON := fmt.Sprintf(`{"tabId":%d}`, tabID)
	msg := Message{
		ID:      generateID(),
		Command: "closeTab",
		Params:  json.RawMessage(paramsJSON),
	}
	data, err := json.Marshal(msg)
	if err != nil {
		logger.Printf("error marshaling closeTab for agent %s: %v", agentID, err)
		return
	}

	ch := make(chan Message, 1)
	pendingMu.Lock()
	pending[msg.ID] = ch
	pendingMu.Unlock()
	defer func() {
		pendingMu.Lock()
		delete(pending, msg.ID)
		pendingMu.Unlock()
	}()

	writeMu.Lock()
	writeErr := writeNativeMessage(os.Stdout, data)
	writeMu.Unlock()
	if writeErr != nil {
		logger.Printf("error closing tab for agent %s: %v", agentID, writeErr)
		return
	}

	select {
	case resp := <-ch:
		if resp.Error != "" {
			logger.Printf("error response closing tab for agent %s: %s", agentID, resp.Error)
		} else {
			logger.Printf("closed tab %d for agent %s", tabID, agentID)
		}
	case <-time.After(5 * time.Second):
		logger.Printf("timeout closing tab for agent %s", agentID)
	}
}

// isTabAwareCommand returns true if the command should be routed to the agent's tab.
func isTabAwareCommand(cmd string) bool {
	return tabAwareCommands[cmd]
}

// ─── Chrome Reader ───────────────────────────────────────────────────────────

func chromeReader(done chan struct{}) {
	stdin := bufio.NewReader(os.Stdin)
	for {
		data, err := readNativeMessage(stdin)
		if err != nil {
			if err != io.EOF {
				logger.Printf("error reading from Chrome: %v", err)
			}
			logger.Printf("Chrome stdin closed, triggering shutdown")
			close(done)
			return
		}
		logger.Printf("← Chrome: %s", string(data))

		var msg Message
		if err := json.Unmarshal(data, &msg); err != nil {
			logger.Printf("error parsing Chrome message: %v", err)
			continue
		}

		if msg.ID == "" {
			logger.Printf("Chrome message has no ID, ignoring")
			continue
		}

		pendingMu.Lock()
		ch, ok := pending[msg.ID]
		if ok {
			delete(pending, msg.ID)
		}
		pendingMu.Unlock()

		if ok {
			select {
			case ch <- msg:
			default:
				logger.Printf("response channel full or closed for id %s", msg.ID)
			}
		} else {
			logger.Printf("no pending request for id %s", msg.ID)
		}
	}
}

// ─── Klawed Connection Handler ───────────────────────────────────────────────

func handleKlawedConn(conn net.Conn) {
	defer conn.Close()
	logger.Printf("klawed client connected")

	scanner := bufio.NewScanner(conn)
	scanner.Buffer(make([]byte, maxMessageSize), maxMessageSize)

	for scanner.Scan() {
		line := scanner.Bytes()
		if len(line) == 0 {
			continue
		}

		var req Message
		if err := json.Unmarshal(line, &req); err != nil {
			logger.Printf("error parsing klawed request: %v", err)
			resp := Message{Error: fmt.Sprintf("invalid JSON: %v", err)}
			writeJSONLine(conn, resp)
			continue
		}

		if req.Command == "" {
			resp := Message{Error: "missing 'command' field"}
			writeJSONLine(conn, resp)
			continue
		}

		// ── Agent session routing ──────────────────────────────────────────
		agentMsg := ""
		if req.AgentID != "" && isTabAwareCommand(req.Command) {
			tabID, msg, err := getOrCreateAgentTab(req.AgentID, req.NewSession)
			if err != nil {
				logger.Printf("error creating agent tab: %v", err)
				resp := Message{Error: fmt.Sprintf("agent session error: %v", err)}
				writeJSONLine(conn, resp)
				continue
			}
			agentMsg = msg

			newParams, err := injectTabIDIntoParams(req.Params, tabID)
			if err != nil {
				logger.Printf("error injecting tabId: %v", err)
				resp := Message{Error: fmt.Sprintf("param error: %v", err)}
				writeJSONLine(conn, resp)
				continue
			}
			req.Params = newParams
			logger.Printf("agent %s → tab %d, command %s", req.AgentID, tabID, req.Command)
		} else if req.AgentID != "" {
			// Shared command — just touch the session timestamp
			touchAgentSession(req.AgentID)
		}

		// Assign a unique ID for this request
		req.ID = generateID()
		ch := make(chan Message, 1)

		pendingMu.Lock()
		pending[req.ID] = ch
		pendingMu.Unlock()

		// Forward command to Chrome extension (strip agentId before sending)
		chromeMsg := Message{
			ID:      req.ID,
			Command: req.Command,
			Params:  req.Params,
		}
		if err := sendToChrome(chromeMsg); err != nil {
			logger.Printf("error sending to Chrome: %v", err)
			pendingMu.Lock()
			delete(pending, req.ID)
			pendingMu.Unlock()
			resp := Message{Error: fmt.Sprintf("failed to send to Chrome: %v", err)}
			writeJSONLine(conn, resp)
			continue
		}

		// Wait for Chrome's response with a timeout
		var resp Message
		select {
		case resp = <-ch:
			logger.Printf("got Chrome response for id %s", req.ID)
		case <-time.After(responseTimeout):
			pendingMu.Lock()
			delete(pending, req.ID)
			pendingMu.Unlock()
			resp = Message{Error: "timeout waiting for Chrome response (30s)"}
			logger.Printf("timeout waiting for Chrome response for id %s", req.ID)
		}

		// Add agent info to response
		if agentMsg != "" {
			respData, _ := json.Marshal(resp)
			var respMap map[string]any
			json.Unmarshal(respData, &respMap)
			if respMap == nil {
				respMap = make(map[string]any)
			}
			respMap["agentMessage"] = agentMsg
			if req.AgentID != "" && isTabAwareCommand(req.Command) {
				agentSessionsMu.Lock()
				if sess, exists := agentSessions[req.AgentID]; exists {
					respMap["agentTab"] = sess.tabID
				}
				agentSessionsMu.Unlock()
			}
			respData, _ = json.Marshal(respMap)
			json.Unmarshal(respData, &resp)
		}

		if err := writeJSONLine(conn, resp); err != nil {
			logger.Printf("error writing response to klawed: %v", err)
			return
		}
	}

	if err := scanner.Err(); err != nil {
		logger.Printf("scanner error: %v", err)
	}
	logger.Printf("klawed client disconnected")
}

func writeJSONLine(w io.Writer, msg Message) error {
	data, err := json.Marshal(msg)
	if err != nil {
		logger.Printf("error marshaling message: %v", err)
		return err
	}
	_, err = w.Write(append(data, '\n'))
	return err
}

// ─── Main ────────────────────────────────────────────────────────────────────

func main() {
	initLogger()
	logger.Printf("starting (pid=%d) with multi-agent support", os.Getpid())

	socketPath := os.Getenv("KLAWED_BROWSER_SOCKET")
	if socketPath == "" {
		socketPath = defaultSocketPath
	}

	os.Remove(socketPath)

	ln, err := net.Listen("unix", socketPath)
	if err != nil {
		logger.Fatalf("failed to listen on socket %s: %v", socketPath, err)
	}
	defer func() {
		ln.Close()
		os.Remove(socketPath)
		logger.Printf("socket removed, bye")
	}()

	logger.Printf("listening for klawed on Unix socket: %s", socketPath)

	sigCh := make(chan os.Signal, 1)
	signal.Notify(sigCh, syscall.SIGTERM, syscall.SIGINT)

	chromeDone := make(chan struct{})
	go chromeReader(chromeDone)

	// Start stale session cleanup goroutine
	go cleanupStaleSessions()

	acceptDone := make(chan struct{})
	go func() {
		defer close(acceptDone)
		for {
			conn, err := ln.Accept()
			if err != nil {
				select {
				case <-chromeDone:
					return
				default:
					logger.Printf("accept error: %v", err)
					return
				}
			}
			go handleKlawedConn(conn)
		}
	}()

	select {
	case sig := <-sigCh:
		logger.Printf("received signal %v, shutting down", sig)
	case <-chromeDone:
		logger.Printf("Chrome disconnected, shutting down")
	}
}
