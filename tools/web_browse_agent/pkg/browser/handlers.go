package browser

import (
	"encoding/base64"
	"fmt"
	"time"

	"github.com/klawed/tools/web_browse_agent/pkg/ipc"
	"github.com/playwright-community/playwright-go"
)

// handleOpen navigates to a URL
// This is async by default - it starts navigation and returns immediately.
// Use wait-for with type=navigation to wait for the page to fully load.
func (d *Driver) handleOpen(req *ipc.Request) (*ipc.Response, error) {
	args, err := req.ParseArguments()
	if err != nil {
		return ipc.NewResponse(req.ID, false, nil, fmt.Sprintf("failed to parse arguments: %v", err))
	}

	if args.URL == "" {
		return ipc.NewResponse(req.ID, false, nil, "URL is required")
	}

	// Get or create active tab
	tab, ok := d.context.GetActiveTab()
	if !ok {
		// Create new tab
		tab, err = d.context.NewTab()
		if err != nil {
			return ipc.NewResponse(req.ID, false, nil, fmt.Sprintf("failed to create tab: %v", err))
		}
	}

	// Navigate to URL - use WaitUntilStateCommit for fast return
	// This returns as soon as the navigation is committed (response received)
	// but doesn't wait for page resources to load
	_, err = tab.Page.Goto(args.URL, playwright.PageGotoOptions{
		WaitUntil: playwright.WaitUntilStateCommit,
	})
	if err != nil {
		return ipc.NewResponse(req.ID, false, nil, fmt.Sprintf("failed to navigate: %v", err))
	}

	// Update tab info with URL immediately, title may not be available yet
	d.context.UpdateTabInfo(tab.ID, args.URL, "")

	// Try to get title but don't fail if not available
	title, _ := tab.Page.Title()
	if title != "" {
		d.context.UpdateTabInfo(tab.ID, args.URL, title)
	}

	return ipc.NewResponse(req.ID, true, map[string]interface{}{
		"url":    args.URL,
		"title":  title,
		"tab_id": tab.ID,
		"note":   "Navigation started. Use 'wait-for' with type=navigation if you need to wait for page load.",
	}, "")
}

// handleListTabs lists all browser tabs
func (d *Driver) handleListTabs(req *ipc.Request) (*ipc.Response, error) {
	tabs := d.context.ListTabs()
	return ipc.NewResponse(req.ID, true, tabs, "")
}

// handleSwitchTab switches to a different tab
func (d *Driver) handleSwitchTab(req *ipc.Request) (*ipc.Response, error) {
	args, err := req.ParseArguments()
	if err != nil {
		return ipc.NewResponse(req.ID, false, nil, fmt.Sprintf("failed to parse arguments: %v", err))
	}

	if args.TabID == "" {
		return ipc.NewResponse(req.ID, false, nil, "tab_id is required")
	}

	if !d.context.SetActiveTab(args.TabID) {
		return ipc.NewResponse(req.ID, false, nil, fmt.Sprintf("tab not found: %s", args.TabID))
	}

	return ipc.NewResponse(req.ID, true, map[string]string{
		"active_tab": args.TabID,
	}, "")
}

// handleCloseTab closes a browser tab
func (d *Driver) handleCloseTab(req *ipc.Request) (*ipc.Response, error) {
	args, err := req.ParseArguments()
	if err != nil {
		return ipc.NewResponse(req.ID, false, nil, fmt.Sprintf("failed to parse arguments: %v", err))
	}

	if args.TabID == "" {
		return ipc.NewResponse(req.ID, false, nil, "tab_id is required")
	}

	if !d.context.CloseTab(args.TabID) {
		return ipc.NewResponse(req.ID, false, nil, fmt.Sprintf("tab not found: %s", args.TabID))
	}

	return ipc.NewResponse(req.ID, true, map[string]string{
		"closed_tab": args.TabID,
	}, "")
}

// handleEval evaluates JavaScript in the browser
func (d *Driver) handleEval(req *ipc.Request) (*ipc.Response, error) {
	args, err := req.ParseArguments()
	if err != nil {
		return ipc.NewResponse(req.ID, false, nil, fmt.Sprintf("failed to parse arguments: %v", err))
	}

	if args.JavaScript == "" {
		return ipc.NewResponse(req.ID, false, nil, "javascript is required")
	}

	tab, ok := d.context.GetActiveTab()
	if !ok {
		return ipc.NewResponse(req.ID, false, nil, "no active tab")
	}

	result, err := tab.Page.Evaluate(args.JavaScript)
	if err != nil {
		return ipc.NewResponse(req.ID, false, nil, fmt.Sprintf("failed to evaluate: %v", err))
	}

	return ipc.NewResponse(req.ID, true, map[string]interface{}{
		"value": result,
	}, "")
}

// handleClick clicks on an element
func (d *Driver) handleClick(req *ipc.Request) (*ipc.Response, error) {
	args, err := req.ParseArguments()
	if err != nil {
		return ipc.NewResponse(req.ID, false, nil, fmt.Sprintf("failed to parse arguments: %v", err))
	}

	if args.Selector == "" {
		return ipc.NewResponse(req.ID, false, nil, "selector is required")
	}

	tab, ok := d.context.GetActiveTab()
	if !ok {
		return ipc.NewResponse(req.ID, false, nil, "no active tab")
	}

	err = tab.Page.Click(args.Selector)
	if err != nil {
		return ipc.NewResponse(req.ID, false, nil, fmt.Sprintf("failed to click: %v", err))
	}

	return ipc.NewResponse(req.ID, true, map[string]string{
		"clicked": args.Selector,
	}, "")
}

// handleType types text into an element
func (d *Driver) handleType(req *ipc.Request) (*ipc.Response, error) {
	args, err := req.ParseArguments()
	if err != nil {
		return ipc.NewResponse(req.ID, false, nil, fmt.Sprintf("failed to parse arguments: %v", err))
	}

	if args.Selector == "" {
		return ipc.NewResponse(req.ID, false, nil, "selector is required")
	}

	tab, ok := d.context.GetActiveTab()
	if !ok {
		return ipc.NewResponse(req.ID, false, nil, "no active tab")
	}

	// Fill clears and types
	err = tab.Page.Fill(args.Selector, args.Text)
	if err != nil {
		return ipc.NewResponse(req.ID, false, nil, fmt.Sprintf("failed to type: %v", err))
	}

	return ipc.NewResponse(req.ID, true, map[string]string{
		"typed_into": args.Selector,
	}, "")
}

// handleUploadFile uploads files to a file input element
func (d *Driver) handleUploadFile(req *ipc.Request) (*ipc.Response, error) {
	args, err := req.ParseArguments()
	if err != nil {
		return ipc.NewResponse(req.ID, false, nil, fmt.Sprintf("failed to parse arguments: %v", err))
	}

	if args.Selector == "" {
		return ipc.NewResponse(req.ID, false, nil, "selector is required")
	}

	if len(args.FilePaths) == 0 {
		return ipc.NewResponse(req.ID, false, nil, "at least one file path is required")
	}

	tab, ok := d.context.GetActiveTab()
	if !ok {
		return ipc.NewResponse(req.ID, false, nil, "no active tab")
	}

	// Locate the file input element
	locator := tab.Page.Locator(args.Selector)

	// Set the input files
	err = locator.SetInputFiles(args.FilePaths)
	if err != nil {
		return ipc.NewResponse(req.ID, false, nil, fmt.Sprintf("failed to upload files: %v", err))
	}

	return ipc.NewResponse(req.ID, true, map[string]interface{}{
		"selector":    args.Selector,
		"files_count": len(args.FilePaths),
		"files":       args.FilePaths,
	}, "")
}

// handleWaitFor waits for an element or condition
func (d *Driver) handleWaitFor(req *ipc.Request) (*ipc.Response, error) {
	args, err := req.ParseArguments()
	if err != nil {
		return ipc.NewResponse(req.ID, false, nil, fmt.Sprintf("failed to parse arguments: %v", err))
	}

	tab, ok := d.context.GetActiveTab()
	if !ok {
		return ipc.NewResponse(req.ID, false, nil, "no active tab")
	}

	timeout := float64(30000) // 30 seconds default
	if args.Timeout > 0 {
		timeout = float64(args.Timeout)
	}

	switch args.WaitType {
	case "timeout", "":
		if args.Timeout > 0 {
			time.Sleep(time.Duration(args.Timeout) * time.Millisecond)
		}
	case "selector":
		if args.Selector == "" {
			return ipc.NewResponse(req.ID, false, nil, "selector is required for wait type 'selector'")
		}
		_, err = tab.Page.WaitForSelector(args.Selector, playwright.PageWaitForSelectorOptions{
			Timeout: playwright.Float(timeout),
		})
		if err != nil {
			return ipc.NewResponse(req.ID, false, nil, fmt.Sprintf("failed to wait for selector: %v", err))
		}
	case "navigation":
		err = tab.Page.WaitForLoadState(playwright.PageWaitForLoadStateOptions{
			State:   playwright.LoadStateNetworkidle,
			Timeout: playwright.Float(timeout),
		})
		if err != nil {
			return ipc.NewResponse(req.ID, false, nil, fmt.Sprintf("failed to wait for navigation: %v", err))
		}
	default:
		return ipc.NewResponse(req.ID, false, nil, fmt.Sprintf("unknown wait type: %s", args.WaitType))
	}

	return ipc.NewResponse(req.ID, true, map[string]string{
		"waited": args.WaitType,
	}, "")
}

// handleScreenshot takes a screenshot
func (d *Driver) handleScreenshot(req *ipc.Request) (*ipc.Response, error) {
	tab, ok := d.context.GetActiveTab()
	if !ok {
		return ipc.NewResponse(req.ID, false, nil, "no active tab")
	}

	data, err := tab.Page.Screenshot(playwright.PageScreenshotOptions{
		Type: playwright.ScreenshotTypePng,
	})
	if err != nil {
		return ipc.NewResponse(req.ID, false, nil, fmt.Sprintf("failed to take screenshot: %v", err))
	}

	return ipc.NewResponse(req.ID, true, map[string]interface{}{
		"data": base64.StdEncoding.EncodeToString(data),
		"type": "png",
	}, "")
}

// handleHTML gets the page HTML
func (d *Driver) handleHTML(req *ipc.Request) (*ipc.Response, error) {
	tab, ok := d.context.GetActiveTab()
	if !ok {
		return ipc.NewResponse(req.ID, false, nil, "no active tab")
	}

	content, err := tab.Page.Content()
	if err != nil {
		return ipc.NewResponse(req.ID, false, nil, fmt.Sprintf("failed to get HTML: %v", err))
	}

	return ipc.NewResponse(req.ID, true, map[string]string{
		"html": content,
	}, "")
}

// handleSetViewport sets the browser viewport size
func (d *Driver) handleSetViewport(req *ipc.Request) (*ipc.Response, error) {
	args, err := req.ParseArguments()
	if err != nil {
		return ipc.NewResponse(req.ID, false, nil, fmt.Sprintf("failed to parse arguments: %v", err))
	}

	if args.Width <= 0 || args.Height <= 0 {
		return ipc.NewResponse(req.ID, false, nil, "width and height must be positive")
	}

	tab, ok := d.context.GetActiveTab()
	if !ok {
		return ipc.NewResponse(req.ID, false, nil, "no active tab")
	}

	err = tab.Page.SetViewportSize(args.Width, args.Height)
	if err != nil {
		return ipc.NewResponse(req.ID, false, nil, fmt.Sprintf("failed to set viewport: %v", err))
	}

	return ipc.NewResponse(req.ID, true, map[string]int{
		"width":  args.Width,
		"height": args.Height,
	}, "")
}

// handleCookies gets browser cookies
func (d *Driver) handleCookies(req *ipc.Request) (*ipc.Response, error) {
	tab, ok := d.context.GetActiveTab()
	if !ok {
		return ipc.NewResponse(req.ID, false, nil, "no active tab")
	}

	cookies, err := tab.Page.Context().Cookies()
	if err != nil {
		return ipc.NewResponse(req.ID, false, nil, fmt.Sprintf("failed to get cookies: %v", err))
	}

	var cookieInfos []map[string]interface{}
	for _, c := range cookies {
		cookieInfos = append(cookieInfos, map[string]interface{}{
			"name":      c.Name,
			"value":     c.Value,
			"domain":    c.Domain,
			"path":      c.Path,
			"expires":   c.Expires,
			"http_only": c.HttpOnly,
			"secure":    c.Secure,
			"same_site": c.SameSite,
		})
	}

	return ipc.NewResponse(req.ID, true, map[string]interface{}{
		"cookies": cookieInfos,
	}, "")
}

// handleSessionInfo returns information about the session
func (d *Driver) handleSessionInfo(req *ipc.Request) (*ipc.Response, error) {
	tabs := d.context.ListTabs()
	activeTab := ""
	for _, t := range tabs {
		if t.Active {
			activeTab = t.ID
			break
		}
	}

	return ipc.NewResponse(req.ID, true, map[string]interface{}{
		"session_id":    d.sessionID,
		"pid":           d.PID(),
		"socket_path":   d.socketPath,
		"tab_count":     len(tabs),
		"active_tab_id": activeTab,
	}, "")
}

// handleDescribeCommands returns descriptions of all available commands
func (d *Driver) handleDescribeCommands(req *ipc.Request) (*ipc.Response, error) {
	commands := []map[string]interface{}{
		{
			"name":        "open",
			"description": "Navigate to a URL in the browser. Async - returns immediately after HTTP headers received. Always follow with 'wait-for' to ensure page is loaded.",
			"arguments":   []string{"url"},
			"example":     "open https://example.com",
			"notes":       "Does NOT wait for page load. Follow with: wait-for '#main-content'",
		},
		{
			"name":        "list-tabs",
			"description": "List all open browser tabs with ID, URL, and title",
			"arguments":   []string{},
			"example":     "list-tabs",
		},
		{
			"name":        "switch-tab",
			"description": "Switch to a different browser tab by ID",
			"arguments":   []string{"tab_id"},
			"example":     "switch-tab tab_123456789",
		},
		{
			"name":        "close-tab",
			"description": "Close a browser tab by ID",
			"arguments":   []string{"tab_id"},
			"example":     "close-tab tab_123456789",
		},
		{
			"name":        "eval",
			"description": "Execute JavaScript code in the browser context and return result in {\"value\": ...} format",
			"arguments":   []string{"javascript"},
			"example":     "eval document.title",
			"notes":       "Returns JSON: {\"value\": <result>}. Use --json flag for machine parsing.",
		},
		{
			"name":        "click",
			"description": "Click on an element using a CSS or Playwright selector",
			"arguments":   []string{"selector"},
			"example":     "click button#submit",
			"notes":       "Supports CSS (#id, .class), text selectors (text=Sign In), role selectors (role=button[name='Submit'])",
		},
		{
			"name":        "type",
			"description": "Type text into an input element. Clears existing content first.",
			"arguments":   []string{"selector", "text"},
			"example":     "type input#email user@example.com",
			"notes":       "Spaces in text are supported: type #input hello world",
		},
		{
			"name":        "upload-file",
			"description": "Upload one or more files to a file input element",
			"arguments":   []string{"selector", "file_path..."},
			"example":     "upload-file input[type=file] /path/to/file.pdf",
			"notes":       "Multiple files: upload-file #input /file1.pdf /file2.jpg",
		},
		{
			"name":        "wait-for",
			"description": "Wait for an element matching a CSS/Playwright selector to appear",
			"arguments":   []string{"selector"},
			"example":     "wait-for .loaded",
			"notes":       "Common patterns: wait-for '#app', wait-for 'body', wait-for text='Welcome'",
		},
		{
			"name":        "screenshot",
			"description": "Take a screenshot of the current page as base64-encoded PNG",
			"arguments":   []string{},
			"example":     "screenshot",
			"notes":       "Use --json to get base64 data. Without --json, only shows summary.",
		},
		{
			"name":        "html",
			"description": "Get the full HTML content of the current page",
			"arguments":   []string{},
			"example":     "html",
		},
		{
			"name":        "set-viewport",
			"description": "Set the browser viewport size in pixels",
			"arguments":   []string{"width", "height"},
			"example":     "set-viewport 1920 1080",
			"notes":       "Recommended before screenshots for consistent dimensions",
		},
		{
			"name":        "cookies",
			"description": "Get browser cookies for the current page (read-only)",
			"arguments":   []string{},
			"example":     "cookies",
			"notes":       "To set cookies, use: eval \"document.cookie = 'key=value; path=/'\"",
		},
		{
			"name":        "session-info",
			"description": "Get information about the current session including PID, socket path, tab count",
			"arguments":   []string{},
			"example":     "session-info",
		},
		{
			"name":        "describe-commands",
			"description": "List all available commands with detailed descriptions",
			"arguments":   []string{},
			"example":     "describe-commands",
		},
		{
			"name":        "end-session",
			"description": "End the browser session and clean up resources",
			"arguments":   []string{},
			"example":     "end-session",
			"notes":       "Sessions auto-cleanup when parent process exits, but explicit cleanup is recommended",
		},
		{
			"name":        "ping",
			"description": "Check if the session is alive and responding",
			"arguments":   []string{},
			"example":     "ping",
		},
	}

	return ipc.NewResponse(req.ID, true, map[string]interface{}{
		"commands": commands,
	}, "")
}
