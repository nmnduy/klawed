package browser

import (
	"encoding/base64"
	"fmt"
	"strings"
	"time"

	"github.com/klawed/tools/web_browse_agent/pkg/ipc"
	"github.com/playwright-community/playwright-go"
)

// frameChainSeparator splits nested frame selectors for multi-level iframe targeting.
// e.g. "iframe.outer >> iframe.nested" targets an element inside a nested iframe.
const frameChainSeparator = " >> "

// splitFrameSelectors splits a frame path into individual frame CSS selectors.
func splitFrameSelectors(frameSelector string) []string {
	if frameSelector == "" {
		return nil
	}
	return strings.Split(frameSelector, frameChainSeparator)
}

// getLocator returns a Playwright Locator for the given selector.
// If a frame CSS selector is provided, it locates the element inside that iframe
// using Playwright's FrameLocator API for each level in the chain.
// Supports nested iframes via " >> " separator.
func getLocator(page playwright.Page, frameSelector, elementSelector string) playwright.Locator {
	if frameSelector == "" {
		return page.Locator(elementSelector)
	}
	frames := splitFrameSelectors(frameSelector)
	fl := page.FrameLocator(frames[0])
	for _, f := range frames[1:] {
		fl = fl.FrameLocator(f)
	}
	return fl.Locator(elementSelector)
}

// evaluateInFrame evaluates JavaScript inside a specific iframe on the page.
// It uses Playwright's ContentFrame() API (via CDP) to access the iframe's
// JavaScript context. This works for both same-origin and cross-origin iframes.
func evaluateInFrame(page playwright.Page, frameSelector, js string) (interface{}, error) {
	// Find the iframe element and get its content frame via CDP
	frame, err := getContentFrame(page, frameSelector)
	if err != nil {
		return nil, err
	}

	// Evaluate JavaScript in the frame's context (works for all origins via CDP)
	result, err := frame.Evaluate(js)
	if err != nil {
		return nil, fmt.Errorf("failed to evaluate in frame: %w", err)
	}

	return result, nil
}

// getContentFrame finds an iframe (or nested iframe chain) by CSS selector(s)
// and returns its content Frame via Playwright's CDP-based ContentFrame() API,
// which works for cross-origin frames.
// Supports nested iframes via " >> " separator in the selector.
func getContentFrame(page playwright.Page, frameSelector string) (playwright.Frame, error) {
	frames := splitFrameSelectors(frameSelector)

	// First level: find the iframe on the page
	locator := page.Locator(frames[0])
	element, err := locator.ElementHandle()
	if err != nil {
		return nil, fmt.Errorf("frame element not found: %s (%w)", frames[0], err)
	}
	if element == nil {
		return nil, fmt.Errorf("frame element not found: %s", frames[0])
	}

	// ContentFrame() returns the frame hosted by this iframe element.
	// This works through CDP and is not subject to same-origin restrictions.
	frame, err := element.ContentFrame()
	if err != nil {
		return nil, fmt.Errorf("failed to get content frame for %s: %w", frames[0], err)
	}
	if frame == nil {
		return nil, fmt.Errorf("element %s exists but has no content frame (not an iframe?)", frames[0])
	}

	// For nested frames, navigate deeper into the frame hierarchy
	for _, fs := range frames[1:] {
		nestedLocator := frame.Locator(fs)
		nestedElement, err := nestedLocator.ElementHandle()
		if err != nil {
			return nil, fmt.Errorf("nested frame element not found: %s (%w)", fs, err)
		}
		if nestedElement == nil {
			return nil, fmt.Errorf("nested frame element not found: %s", fs)
		}
		nestedFrame, err := nestedElement.ContentFrame()
		if err != nil {
			return nil, fmt.Errorf("failed to get nested content frame for %s: %w", fs, err)
		}
		if nestedFrame == nil {
			return nil, fmt.Errorf("nested element %s exists but has no content frame", fs)
		}
		frame = nestedFrame
	}

	return frame, nil
}

// getFrameContent returns the outerHTML of the document inside a specific iframe.
// Uses Playwright's ContentFrame() API (via CDP), works for cross-origin frames.
func getFrameContent(page playwright.Page, frameSelector string) (string, error) {
	frame, err := getContentFrame(page, frameSelector)
	if err != nil {
		return "", err
	}

	// Get the full page HTML inside the iframe
	content, err := frame.Content()
	if err != nil {
		return "", fmt.Errorf("failed to get frame HTML: %w", err)
	}

	return content, nil
}

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
// Supports an optional 'frame' parameter to evaluate JS inside a specific iframe.
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

	var result interface{}
	if args.Frame != "" {
		result, err = evaluateInFrame(tab.Page, args.Frame, args.JavaScript)
	} else {
		result, err = tab.Page.Evaluate(args.JavaScript)
	}
	if err != nil {
		return ipc.NewResponse(req.ID, false, nil, fmt.Sprintf("failed to evaluate: %v", err))
	}

	return ipc.NewResponse(req.ID, true, map[string]interface{}{
		"value": result,
	}, "")
}

// handleClick clicks on an element
// Supports an optional 'frame' parameter to click inside a specific iframe.
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

	locator := getLocator(tab.Page, args.Frame, args.Selector)
	err = locator.Click()
	if err != nil {
		return ipc.NewResponse(req.ID, false, nil, fmt.Sprintf("failed to click: %v", err))
	}

	target := args.Selector
	if args.Frame != "" {
		target = fmt.Sprintf("%s inside iframe %s", args.Selector, args.Frame)
	}

	return ipc.NewResponse(req.ID, true, map[string]string{
		"clicked": target,
	}, "")
}

// handleType types text into an element
// Supports an optional 'frame' parameter to type inside a specific iframe.
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
	locator := getLocator(tab.Page, args.Frame, args.Selector)
	err = locator.Fill(args.Text)
	if err != nil {
		return ipc.NewResponse(req.ID, false, nil, fmt.Sprintf("failed to type: %v", err))
	}

	target := args.Selector
	if args.Frame != "" {
		target = fmt.Sprintf("%s inside iframe %s", args.Selector, args.Frame)
	}

	return ipc.NewResponse(req.ID, true, map[string]string{
		"typed_into": target,
	}, "")
}

// handleUploadFile uploads files to a file input element
// Supports an optional 'frame' parameter to upload inside a specific iframe.
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

	// Locate the file input element inside the frame (or page)
	locator := getLocator(tab.Page, args.Frame, args.Selector)

	// Set the input files
	err = locator.SetInputFiles(args.FilePaths)
	if err != nil {
		return ipc.NewResponse(req.ID, false, nil, fmt.Sprintf("failed to upload files: %v", err))
	}

	return ipc.NewResponse(req.ID, true, map[string]interface{}{
		"selector":    args.Selector,
		"frame":       args.Frame,
		"files_count": len(args.FilePaths),
		"files":       args.FilePaths,
	}, "")
}

// handleWaitFor waits for an element or condition
// Supports an optional 'frame' parameter to wait for elements inside a specific iframe.
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
		locator := getLocator(tab.Page, args.Frame, args.Selector)
		err = locator.WaitFor(playwright.LocatorWaitForOptions{
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
// Supports an optional 'frame' parameter to get HTML from a specific iframe.
func (d *Driver) handleHTML(req *ipc.Request) (*ipc.Response, error) {
	args, err := req.ParseArguments()
	if err != nil {
		return ipc.NewResponse(req.ID, false, nil, fmt.Sprintf("failed to parse arguments: %v", err))
	}

	tab, ok := d.context.GetActiveTab()
	if !ok {
		return ipc.NewResponse(req.ID, false, nil, "no active tab")
	}

	var content string
	if args.Frame != "" {
		content, err = getFrameContent(tab.Page, args.Frame)
	} else {
		content, err = tab.Page.Content()
	}
	if err != nil {
		return ipc.NewResponse(req.ID, false, nil, fmt.Sprintf("failed to get HTML: %v", err))
	}

	return ipc.NewResponse(req.ID, true, map[string]string{
		"html":  content,
		"frame": args.Frame,
	}, "")
}

// handleListFrames lists all iframes on the current page with their selectors
func (d *Driver) handleListFrames(req *ipc.Request) (*ipc.Response, error) {
	tab, ok := d.context.GetActiveTab()
	if !ok {
		return ipc.NewResponse(req.ID, false, nil, "no active tab")
	}

	// Use JavaScript to enumerate all iframes on the page with useful metadata
	js := `(function() {
		var frames = document.querySelectorAll('iframe, frame');
		var result = [];
		var seen = new Set();
		frames.forEach(function(f, i) {
			var info = {
				index: i,
				selector: '',
				id: f.id || '',
				name: f.name || '',
				src: f.src || '',
				title: f.title || '',
				width: f.width || f.getAttribute('width') || '',
				height: f.height || f.getAttribute('height') || '',
				sandbox: f.getAttribute('sandbox') || '',
				visible: f.offsetWidth > 0 && f.offsetHeight > 0
			};
			// Build an optimal CSS selector for the iframe
			if (f.id) {
				info.selector = '#' + CSS.escape(f.id);
			} else if (f.name) {
				// name attribute selector is reliable
				info.selector = 'iframe[name="' + f.name.replace(/"/g, '\\"') + '"], frame[name="' + f.name.replace(/"/g, '\\"') + '"]';
			} else {
				// Use nth-of-type based on parent
				var parent = f.parentElement;
				var tag = f.tagName.toLowerCase();
				var siblings = parent.querySelectorAll(tag);
				var nth = 1;
				for (var j = 0; j < siblings.length; j++) {
					if (siblings[j] === f) {
						nth = j + 1;
						break;
					}
				}
				// Build a contextual selector from parent
				var parentSel = '';
				var p = f;
				while (p.parentElement && p.parentElement !== document.body && p.parentElement !== document.documentElement) {
					p = p.parentElement;
					if (p.id) {
						parentSel = '#' + CSS.escape(p.id) + ' ';
						break;
					}
					// Max 2 levels up
					if (p.tagName === 'BODY' || p.tagName === 'HTML') break;
				}
				info.selector = parentSel + tag + ':nth-of-type(' + nth + ')';
			}
			// Add full page overview info
			info.frame_count = frames.length;
			result.push(info);
		});
		return result;
	})()`

	result, err := tab.Page.Evaluate(js)
	if err != nil {
		return ipc.NewResponse(req.ID, false, nil, fmt.Sprintf("failed to list frames: %v", err))
	}

	// Also get frames from Playwright's own frame tree (includes cross-origin frames)
	frames := tab.Page.Frames()
	var frameURLs []map[string]interface{}
	for _, f := range frames {
		frameURLs = append(frameURLs, map[string]interface{}{
			"name": f.Name,
			"url":  f.URL,
		})
	}

	return ipc.NewResponse(req.ID, true, map[string]interface{}{
		"iframes":     result,
		"frame_count": len(frames),
		"frame_tree":  frameURLs,
		"hint":        "Use the 'selector' value from any iframe entry as the 'frame' parameter in click/type/wait-for/eval/html commands to target that iframe's contents.",
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
			"name":        "list-frames",
			"description": "List all iframes on the current page with their CSS selectors, IDs, names, and src attributes. Use the 'selector' value from any iframe entry as the '--frame' parameter in click/type/wait-for/eval/html commands to interact with content inside that iframe.",
			"arguments":   []string{},
			"example":     "list-frames",
			"notes":       "Returns frame selectors that can be used with --frame parameter on other commands",
		},
		{
			"name":        "eval",
			"description": "Execute JavaScript code in the browser context and return result in {\"value\": ...} format. Use --frame to evaluate inside an iframe.",
			"arguments":   []string{"javascript"},
			"example":     "eval document.title",
			"notes":       "Use --frame #my-iframe to evaluate in that iframe's context. For cross-origin iframes, use the page-level eval with contentWindow.eval().",
		},
		{
			"name":        "click",
			"description": "Click on an element using a CSS or Playwright selector. Use --frame to click inside an iframe.",
			"arguments":   []string{"selector"},
			"example":     "click button#submit",
			"notes":       "Supports CSS (#id, .class), text selectors (text=Sign In), role selectors (role=button[name='Submit']). Use --frame #my-iframe to click inside an iframe.",
		},
		{
			"name":        "type",
			"description": "Type text into an input element. Clears existing content first. Use --frame to type inside an iframe.",
			"arguments":   []string{"selector", "text"},
			"example":     "type input#email user@example.com",
			"notes":       "Spaces in text are supported: type #input hello world. Use --frame #my-iframe to type inside an iframe.",
		},
		{
			"name":        "upload-file",
			"description": "Upload one or more files to a file input element. Use --frame to select a file input inside an iframe.",
			"arguments":   []string{"selector", "file_path..."},
			"example":     "upload-file input[type=file] /path/to/file.pdf",
			"notes":       "Multiple files: upload-file #input /file1.pdf /file2.jpg. Use --frame #my-iframe for file inputs inside iframes.",
		},
		{
			"name":        "wait-for",
			"description": "Wait for an element matching a CSS/Playwright selector to appear. Use --frame to wait inside an iframe.",
			"arguments":   []string{"selector"},
			"example":     "wait-for .loaded",
			"notes":       "Common patterns: wait-for '#app', wait-for 'body', wait-for text='Welcome'. Use --frame #my-iframe to wait inside iframe.",
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
			"description": "Get the full HTML content of the current page. Use --frame to get an iframe's HTML content instead.",
			"arguments":   []string{},
			"example":     "html",
			"notes":       "Use html --frame #my-iframe to get the HTML content of a specific iframe.",
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
