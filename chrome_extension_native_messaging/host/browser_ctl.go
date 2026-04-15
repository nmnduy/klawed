// browser_ctl — Klawed Browser Control CLI
//
// A thin client that speaks to the Klawed Browser Controller extension.
// Philosophy: AI agents write JavaScript. This binary only does what
// JavaScript inside a page cannot do (tabs, screenshots, navigation).
//
// Usage:
//   browser_ctl eval "document.querySelector('h1').innerText"
//   browser_ctl navigate https://example.com
//   browser_ctl screenshot
//   browser_ctl list-tabs
//   browser_ctl ping
package main

import (
	"encoding/base64"
	"encoding/json"
	"flag"
	"fmt"
	"net"
	"os"
	"path/filepath"
	"strconv"
	"strings"
	"time"
)

const version = "2.1.0"

const helpText = `Klawed Browser Controller

Philosophy: AI agents write JavaScript. This binary only exposes what
JavaScript running inside a page cannot do on its own.

QUICK START
  browser_ctl eval "document.title"
  browser_ctl eval "document.querySelector('#search').value = 'klawed'"
  browser_ctl eval "document.querySelector('#search').click()"
  browser_ctl navigate https://example.com
  browser_ctl screenshot
  browser_ctl list-tabs
  browser_ctl ping

COMMANDS
  eval <javascript>
      Execute JavaScript in the active tab and return the result.
      This is the primary interface. Use it to click, type, scroll,
      extract data, fill forms, and wait for elements.
      Examples:
        browser_ctl eval "document.title"
        browser_ctl eval "document.querySelector('button').click()"
        browser_ctl eval "document.querySelector('input').value = 'hello'"
        browser_ctl eval "window.scrollTo(0, 500)"
        browser_ctl eval "document.querySelector('.result')?.innerText"
        browser_ctl eval "new Promise(r => setTimeout(r, 1000))"

  navigate <url>
      Navigate the active tab to <url>.
      Example: browser_ctl navigate https://github.com

  new-tab [url]
      Open a new tab. Optionally navigate to [url].

  switch-tab <tabId>
      Focus the tab with the given ID.

  list-tabs
      List all open tabs.

  screenshot
      Capture the visible area as PNG. Saves to a temp file and prints
      the path.

  ping
      Health check. Returns quickly when the extension is connected.

FLAGS
  -socket string   Unix socket path (default: /tmp/klawed-browser.sock)
  -json            Print raw JSON response
  -timeout int     Timeout in seconds (default: 35)
  -help            Show this help text
  -version         Show version

LEGACY MODE
  If the first argument starts with '{' it is treated as raw JSON:
    browser_ctl '{"command":"eval","params":{"code":"document.title"}}'

TROUBLESHOOTING
  "socket not found"
      → The Chrome extension is not connected. Click the 🤖 icon in Chrome
        and click Connect until the dot is green.

  "connection refused"
      → The native host may not be running. Open Chrome with the extension
        loaded and click Connect.

ENVIRONMENT
  KLAWED_BROWSER_SOCKET    Override the default Unix socket path.
`

var (
	socketPath string
	jsonMode   bool
	timeoutSec int
)

func main() {
	flag.StringVar(&socketPath, "socket", defaultSocket(), "Unix socket path")
	flag.BoolVar(&jsonMode, "json", false, "Print raw JSON response")
	flag.IntVar(&timeoutSec, "timeout", 35, "Timeout in seconds")
	flag.Usage = func() { fmt.Fprint(os.Stderr, helpText) }
	flag.Parse()

	for _, a := range os.Args[1:] {
		if a == "-help" || a == "--help" || a == "-h" {
			fmt.Fprint(os.Stdout, helpText)
			os.Exit(0)
		}
		if a == "-version" || a == "--version" || a == "-v" {
			fmt.Println("browser_ctl", version)
			os.Exit(0)
		}
	}

	args := flag.Args()
	if len(args) == 0 {
		fmt.Fprint(os.Stderr, helpText)
		os.Exit(1)
	}

	var payload string
	if strings.HasPrefix(args[0], "{") {
		payload = args[0]
		if err := validateJSON(payload); err != nil {
			printErr("invalid JSON: %v", err)
		}
	} else {
		payload = buildPayload(args)
	}

	resp, err := sendPayload(payload)
	if err != nil {
		printErr("%v", err)
	}

	if jsonMode {
		fmt.Println(resp)
		return
	}
	prettyPrint(resp)
}

func defaultSocket() string {
	if s := os.Getenv("KLAWED_BROWSER_SOCKET"); s != "" {
		return s
	}
	return "/tmp/klawed-browser.sock"
}

func validateJSON(s string) error {
	var v any
	return json.Unmarshal([]byte(s), &v)
}

func printErr(format string, v ...any) {
	e := map[string]string{"error": fmt.Sprintf(format, v...)}
	b, _ := json.MarshalIndent(e, "", "  ")
	fmt.Fprintln(os.Stderr, string(b))
	os.Exit(1)
}

func sendPayload(payload string) (string, error) {
	conn, err := net.DialTimeout("unix", socketPath, time.Duration(timeoutSec)*time.Second)
	if err != nil {
		msg := err.Error()
		if strings.Contains(msg, "no such file or directory") {
			return "", fmt.Errorf("socket not found: %s (hint: is the Klawed Browser Controller extension connected in Chrome?)", socketPath)
		}
		if strings.Contains(msg, "connection refused") {
			return "", fmt.Errorf("connection refused: %s (hint: the native host may not be running — open Chrome with the extension and click Connect)", socketPath)
		}
		return "", err
	}
	defer conn.Close()

	_ = conn.SetDeadline(time.Now().Add(time.Duration(timeoutSec) * time.Second))
	fmt.Fprintln(conn, payload)

	buf := make([]byte, 0, 65536)
	tmp := make([]byte, 4096)
	for {
		n, err := conn.Read(tmp)
		if n > 0 {
			buf = append(buf, tmp[:n]...)
			if bytesContains(buf, '\n') {
				break
			}
		}
		if err != nil {
			break
		}
	}

	line := string(buf)
	if idx := strings.Index(line, "\n"); idx >= 0 {
		line = line[:idx]
	}
	return line, nil
}

func bytesContains(b []byte, c byte) bool {
	for i := 0; i < len(b); i++ {
		if b[i] == c {
			return true
		}
	}
	return false
}

func prettyPrint(line string) {
	var m map[string]any
	if err := json.Unmarshal([]byte(line), &m); err != nil {
		fmt.Println(line)
		return
	}

	// Save screenshots to temp files instead of dumping base64.
	// The dataUrl may be at the top level or nested inside "result".
	extractAndSaveScreenshot := func(obj map[string]any) bool {
		for _, key := range []string{"dataUrl", "data_url"} {
			if dataURL, ok := obj[key].(string); ok && strings.HasPrefix(dataURL, "data:image/") {
				saveScreenshot(dataURL)
				delete(obj, key)
				return true
			}
		}
		return false
	}

	extractAndSaveScreenshot(m)
	if res, ok := m["result"].(map[string]any); ok {
		extractAndSaveScreenshot(res)
	}

	out, err := json.MarshalIndent(m, "", "  ")
	if err != nil {
		fmt.Println(line)
		return
	}
	fmt.Println(string(out))
}

func saveScreenshot(dataURL string) {
	parts := strings.SplitN(dataURL, ",", 2)
	if len(parts) != 2 {
		return
	}
	ext := "png"
	if strings.Contains(parts[0], "jpeg") || strings.Contains(parts[0], "jpg") {
		ext = "jpg"
	}
	b, err := base64.StdEncoding.DecodeString(parts[1])
	if err != nil {
		return
	}
	fname := filepath.Join(os.TempDir(), fmt.Sprintf("klawed_screenshot_%d.%s", time.Now().Unix(), ext))
	if err := os.WriteFile(fname, b, 0644); err != nil {
		return
	}
	fmt.Printf("Screenshot saved to: %s\n", fname)
}

func buildPayload(args []string) string {
	cmd := args[0]
	params := make(map[string]any)

	switch cmd {
	case "navigate":
		if len(args) > 1 {
			params["url"] = args[1]
		}
	case "new-tab":
		if len(args) > 1 {
			params["url"] = args[1]
		}
	case "switch-tab":
		if len(args) > 1 {
			if id, err := strconv.Atoi(args[1]); err == nil {
				params["tabId"] = id
			}
		}
	case "eval":
		if len(args) > 1 {
			params["code"] = strings.Join(args[1:], " ")
		}
	}

	// Map CLI names to camelCase extension commands
	commandMap := map[string]string{
		"eval":       "evaluate",
		"new-tab":    "newTab",
		"switch-tab": "switchTab",
		"list-tabs":  "listTabs",
	}
	if mapped, ok := commandMap[cmd]; ok {
		cmd = mapped
	}

	msg := map[string]any{"command": cmd}
	if len(params) > 0 {
		msg["params"] = params
	}
	b, _ := json.Marshal(msg)
	return string(b)
}
