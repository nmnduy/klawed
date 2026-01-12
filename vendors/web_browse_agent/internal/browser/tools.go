package browser

import (
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"
	"regexp"
	"strconv"
	"strings"
	"time"

	"github.com/playwright-community/playwright-go"
)

// resolveSelector converts various selector formats to playwright locators
// Supported formats:
//   - CSS selector (default): div.class, #id, etc.
//   - testid:xxx - data-testid attribute
//   - role:xxx:text - ARIA role with optional text
//   - text:xxx - text content
//   - label:xxx - form label
//   - placeholder:xxx - input placeholder
func resolveSelector(page playwright.Page, selector string) playwright.Locator {
	switch {
	case strings.HasPrefix(selector, "testid:"):
		testID := strings.TrimPrefix(selector, "testid:")
		return page.GetByTestId(testID)

	case strings.HasPrefix(selector, "role:"):
		parts := strings.SplitN(strings.TrimPrefix(selector, "role:"), ":", 2)
		role := playwright.AriaRole(parts[0])
		if len(parts) > 1 {
			return page.GetByRole(role, playwright.PageGetByRoleOptions{
				Name: parts[1],
			})
		}
		return page.GetByRole(role)

	case strings.HasPrefix(selector, "text:"):
		text := strings.TrimPrefix(selector, "text:")
		return page.GetByText(text)

	case strings.HasPrefix(selector, "label:"):
		label := strings.TrimPrefix(selector, "label:")
		return page.GetByLabel(label)

	case strings.HasPrefix(selector, "placeholder:"):
		placeholder := strings.TrimPrefix(selector, "placeholder:")
		return page.GetByPlaceholder(placeholder)

	default:
		// CSS selector
		return page.Locator(selector)
	}
}

// getStringParam extracts a string parameter from params map
func getStringParam(params map[string]interface{}, key string, defaultVal string) string {
	if val, ok := params[key]; ok {
		if s, ok := val.(string); ok {
			return s
		}
	}
	return defaultVal
}

// getFloatParam extracts a float parameter from params map
func getFloatParam(params map[string]interface{}, key string, defaultVal float64) float64 {
	if val, ok := params[key]; ok {
		switch v := val.(type) {
		case float64:
			return v
		case int:
			return float64(v)
		case string:
			if f, err := strconv.ParseFloat(v, 64); err == nil {
				return f
			}
		}
	}
	return defaultVal
}

// getBoolParam extracts a boolean parameter from params map
func getBoolParam(params map[string]interface{}, key string, defaultVal bool) bool {
	if val, ok := params[key]; ok {
		switch v := val.(type) {
		case bool:
			return v
		case string:
			return strings.ToLower(v) == "true" || v == "1"
		}
	}
	return defaultVal
}

// ============================================================================
// NavigateTool - Navigate to URL
// ============================================================================

type NavigateTool struct {
	ctx *Context
}

func NewNavigateTool(ctx *Context) *NavigateTool {
	return &NavigateTool{ctx: ctx}
}

func (t *NavigateTool) Name() string {
	return "browser_navigate"
}

func (t *NavigateTool) Description() string {
	return "Navigate to a URL in the browser. If no tab is open, creates a new one."
}

func (t *NavigateTool) ParametersSchema() map[string]interface{} {
	return map[string]interface{}{
		"type": "object",
		"properties": map[string]interface{}{
			"url": map[string]interface{}{
				"type":        "string",
				"description": "The URL to navigate to",
			},
		},
		"required": []string{"url"},
	}
}

func (t *NavigateTool) Execute(params map[string]interface{}) (string, error) {
	url := getStringParam(params, "url", "")
	if url == "" {
		return "", fmt.Errorf("url parameter is required")
	}

	page, err := t.ctx.EnsureActivePage()
	if err != nil {
		return "", err
	}

	resp, err := page.Goto(url)
	if err != nil {
		return "", fmt.Errorf("failed to navigate to %s: %w", url, err)
	}

	status := 0
	if resp != nil {
		status = resp.Status()
	}

	return fmt.Sprintf("Navigated to %s (status: %d)", url, status), nil
}

// ============================================================================
// ClickTool - Click on an element
// ============================================================================

type ClickTool struct {
	ctx *Context
}

func NewClickTool(ctx *Context) *ClickTool {
	return &ClickTool{ctx: ctx}
}

func (t *ClickTool) Name() string {
	return "browser_click"
}

func (t *ClickTool) Description() string {
	return "Click on an element in the page. Supports CSS selectors, testid:xxx, role:xxx:text, text:xxx, label:xxx, placeholder:xxx"
}

func (t *ClickTool) ParametersSchema() map[string]interface{} {
	return map[string]interface{}{
		"type": "object",
		"properties": map[string]interface{}{
			"selector": map[string]interface{}{
				"type":        "string",
				"description": "Element selector (CSS, testid:xxx, role:xxx:text, text:xxx, label:xxx, placeholder:xxx)",
			},
		},
		"required": []string{"selector"},
	}
}

func (t *ClickTool) Execute(params map[string]interface{}) (string, error) {
	selector := getStringParam(params, "selector", "")
	if selector == "" {
		return "", fmt.Errorf("selector parameter is required")
	}

	page, err := t.ctx.ActivePage()
	if err != nil {
		return "", err
	}

	locator := resolveSelector(page, selector)
	if err := locator.Click(); err != nil {
		return "", fmt.Errorf("failed to click on %s: %w", selector, err)
	}

	return fmt.Sprintf("Clicked on element: %s", selector), nil
}

// ============================================================================
// FillTool - Fill an input field
// ============================================================================

type FillTool struct {
	ctx *Context
}

func NewFillTool(ctx *Context) *FillTool {
	return &FillTool{ctx: ctx}
}

func (t *FillTool) Name() string {
	return "browser_fill"
}

func (t *FillTool) Description() string {
	return "Fill an input field with text. Clears the field first, then fills with the provided text."
}

func (t *FillTool) ParametersSchema() map[string]interface{} {
	return map[string]interface{}{
		"type": "object",
		"properties": map[string]interface{}{
			"selector": map[string]interface{}{
				"type":        "string",
				"description": "Element selector for the input field",
			},
			"text": map[string]interface{}{
				"type":        "string",
				"description": "Text to fill in the input field",
			},
		},
		"required": []string{"selector", "text"},
	}
}

func (t *FillTool) Execute(params map[string]interface{}) (string, error) {
	selector := getStringParam(params, "selector", "")
	text := getStringParam(params, "text", "")
	if selector == "" {
		return "", fmt.Errorf("selector parameter is required")
	}

	page, err := t.ctx.ActivePage()
	if err != nil {
		return "", err
	}

	locator := resolveSelector(page, selector)
	if err := locator.Fill(text); err != nil {
		return "", fmt.Errorf("failed to fill %s: %w", selector, err)
	}

	return fmt.Sprintf("Filled element %s with text", selector), nil
}

// ============================================================================
// TypeTool - Type text character by character
// ============================================================================

type TypeTool struct {
	ctx *Context
}

func NewTypeTool(ctx *Context) *TypeTool {
	return &TypeTool{ctx: ctx}
}

func (t *TypeTool) Name() string {
	return "browser_type"
}

func (t *TypeTool) Description() string {
	return "Type text character by character, simulating real keyboard input. Useful for inputs that respond to keypress events."
}

func (t *TypeTool) ParametersSchema() map[string]interface{} {
	return map[string]interface{}{
		"type": "object",
		"properties": map[string]interface{}{
			"selector": map[string]interface{}{
				"type":        "string",
				"description": "Element selector for the input field",
			},
			"text": map[string]interface{}{
				"type":        "string",
				"description": "Text to type",
			},
			"delay": map[string]interface{}{
				"type":        "number",
				"description": "Delay between keystrokes in milliseconds (default: 50)",
			},
		},
		"required": []string{"selector", "text"},
	}
}

func (t *TypeTool) Execute(params map[string]interface{}) (string, error) {
	selector := getStringParam(params, "selector", "")
	text := getStringParam(params, "text", "")
	delay := getFloatParam(params, "delay", 50)

	if selector == "" {
		return "", fmt.Errorf("selector parameter is required")
	}

	page, err := t.ctx.ActivePage()
	if err != nil {
		return "", err
	}

	locator := resolveSelector(page, selector)
	if err := locator.PressSequentially(text, playwright.LocatorPressSequentiallyOptions{
		Delay: playwright.Float(delay),
	}); err != nil {
		return "", fmt.Errorf("failed to type in %s: %w", selector, err)
	}

	return fmt.Sprintf("Typed text in element: %s", selector), nil
}

// ============================================================================
// PressKeyTool - Press a key or key combination
// ============================================================================

type PressKeyTool struct {
	ctx *Context
}

func NewPressKeyTool(ctx *Context) *PressKeyTool {
	return &PressKeyTool{ctx: ctx}
}

func (t *PressKeyTool) Name() string {
	return "browser_press_key"
}

func (t *PressKeyTool) Description() string {
	return "Press a key or key combination. Examples: Enter, Tab, Escape, Control+c, Shift+Tab, Alt+F4"
}

func (t *PressKeyTool) ParametersSchema() map[string]interface{} {
	return map[string]interface{}{
		"type": "object",
		"properties": map[string]interface{}{
			"key": map[string]interface{}{
				"type":        "string",
				"description": "Key or key combination to press (e.g., Enter, Tab, Control+c)",
			},
			"selector": map[string]interface{}{
				"type":        "string",
				"description": "Optional: Element selector to focus before pressing key",
			},
		},
		"required": []string{"key"},
	}
}

func (t *PressKeyTool) Execute(params map[string]interface{}) (string, error) {
	key := getStringParam(params, "key", "")
	selector := getStringParam(params, "selector", "")

	if key == "" {
		return "", fmt.Errorf("key parameter is required")
	}

	page, err := t.ctx.ActivePage()
	if err != nil {
		return "", err
	}

	if selector != "" {
		locator := resolveSelector(page, selector)
		if err := locator.Press(key); err != nil {
			return "", fmt.Errorf("failed to press %s on %s: %w", key, selector, err)
		}
	} else {
		if err := page.Keyboard().Press(key); err != nil {
			return "", fmt.Errorf("failed to press %s: %w", key, err)
		}
	}

	return fmt.Sprintf("Pressed key: %s", key), nil
}

// ============================================================================
// HoverTool - Hover over an element
// ============================================================================

type HoverTool struct {
	ctx *Context
}

func NewHoverTool(ctx *Context) *HoverTool {
	return &HoverTool{ctx: ctx}
}

func (t *HoverTool) Name() string {
	return "browser_hover"
}

func (t *HoverTool) Description() string {
	return "Hover over an element to trigger hover states or tooltips"
}

func (t *HoverTool) ParametersSchema() map[string]interface{} {
	return map[string]interface{}{
		"type": "object",
		"properties": map[string]interface{}{
			"selector": map[string]interface{}{
				"type":        "string",
				"description": "Element selector to hover over",
			},
		},
		"required": []string{"selector"},
	}
}

func (t *HoverTool) Execute(params map[string]interface{}) (string, error) {
	selector := getStringParam(params, "selector", "")
	if selector == "" {
		return "", fmt.Errorf("selector parameter is required")
	}

	page, err := t.ctx.ActivePage()
	if err != nil {
		return "", err
	}

	locator := resolveSelector(page, selector)
	if err := locator.Hover(); err != nil {
		return "", fmt.Errorf("failed to hover on %s: %w", selector, err)
	}

	return fmt.Sprintf("Hovered over element: %s", selector), nil
}

// ============================================================================
// SelectOptionTool - Select dropdown option
// ============================================================================

type SelectOptionTool struct {
	ctx *Context
}

func NewSelectOptionTool(ctx *Context) *SelectOptionTool {
	return &SelectOptionTool{ctx: ctx}
}

func (t *SelectOptionTool) Name() string {
	return "browser_select_option"
}

func (t *SelectOptionTool) Description() string {
	return "Select an option from a dropdown/select element by value, label, or index"
}

func (t *SelectOptionTool) ParametersSchema() map[string]interface{} {
	return map[string]interface{}{
		"type": "object",
		"properties": map[string]interface{}{
			"selector": map[string]interface{}{
				"type":        "string",
				"description": "Selector for the select element",
			},
			"value": map[string]interface{}{
				"type":        "string",
				"description": "Option value to select",
			},
			"label": map[string]interface{}{
				"type":        "string",
				"description": "Option label/text to select",
			},
			"index": map[string]interface{}{
				"type":        "integer",
				"description": "Option index to select (0-based)",
			},
		},
		"required": []string{"selector"},
	}
}

func (t *SelectOptionTool) Execute(params map[string]interface{}) (string, error) {
	selector := getStringParam(params, "selector", "")
	if selector == "" {
		return "", fmt.Errorf("selector parameter is required")
	}

	page, err := t.ctx.ActivePage()
	if err != nil {
		return "", err
	}

	locator := resolveSelector(page, selector)

	var selectOptions playwright.SelectOptionValues

	if value := getStringParam(params, "value", ""); value != "" {
		selectOptions.Values = &[]string{value}
	} else if label := getStringParam(params, "label", ""); label != "" {
		selectOptions.Labels = &[]string{label}
	} else if idx, ok := params["index"]; ok {
		var index int
		switch v := idx.(type) {
		case float64:
			index = int(v)
		case int:
			index = v
		}
		selectOptions.Indexes = &[]int{index}
	} else {
		return "", fmt.Errorf("one of value, label, or index is required")
	}

	if _, err := locator.SelectOption(selectOptions); err != nil {
		return "", fmt.Errorf("failed to select option in %s: %w", selector, err)
	}

	return fmt.Sprintf("Selected option in: %s", selector), nil
}

// ============================================================================
// SnapshotTool - Get accessibility tree/snapshot of page
// ============================================================================

type SnapshotTool struct {
	ctx *Context
}

func NewSnapshotTool(ctx *Context) *SnapshotTool {
	return &SnapshotTool{ctx: ctx}
}

func (t *SnapshotTool) Name() string {
	return "browser_snapshot"
}

func (t *SnapshotTool) Description() string {
	return "Get an accessibility tree snapshot of the current page. Returns structured information about page elements."
}

func (t *SnapshotTool) ParametersSchema() map[string]interface{} {
	return map[string]interface{}{
		"type":       "object",
		"properties": map[string]interface{}{},
	}
}

func (t *SnapshotTool) Execute(params map[string]interface{}) (string, error) {
	page, err := t.ctx.ActivePage()
	if err != nil {
		return "", err
	}

	// Get ARIA snapshot from the body element
	// This provides a text representation of the accessibility tree
	snapshot, err := page.Locator("body").AriaSnapshot()
	if err != nil {
		return "", fmt.Errorf("failed to get aria snapshot: %w", err)
	}

	if snapshot == "" {
		return "No accessibility tree available for this page", nil
	}

	return snapshot, nil
}

// ============================================================================
// ScreenshotTool - Take screenshot
// ============================================================================

type ScreenshotTool struct {
	ctx *Context
}

func NewScreenshotTool(ctx *Context) *ScreenshotTool {
	return &ScreenshotTool{ctx: ctx}
}

func (t *ScreenshotTool) Name() string {
	return "browser_screenshot"
}

func (t *ScreenshotTool) Description() string {
	return "Take a screenshot of the current page and save it to the output directory"
}

func (t *ScreenshotTool) ParametersSchema() map[string]interface{} {
	return map[string]interface{}{
		"type": "object",
		"properties": map[string]interface{}{
			"filename": map[string]interface{}{
				"type":        "string",
				"description": "Filename for the screenshot (default: screenshot-{timestamp}.png)",
			},
			"full_page": map[string]interface{}{
				"type":        "boolean",
				"description": "Capture full page screenshot (default: false)",
			},
			"selector": map[string]interface{}{
				"type":        "string",
				"description": "Optional: Capture only this element",
			},
		},
	}
}

func (t *ScreenshotTool) Execute(params map[string]interface{}) (string, error) {
	filename := getStringParam(params, "filename", "")
	fullPage := getBoolParam(params, "full_page", false)
	selector := getStringParam(params, "selector", "")

	page, err := t.ctx.ActivePage()
	if err != nil {
		return "", err
	}

	// Generate filename if not provided
	if filename == "" {
		timestamp := time.Now().Format("20060102-150405")
		filename = fmt.Sprintf("screenshot-%s.png", timestamp)
	}

	// Ensure .png extension
	if !strings.HasSuffix(filename, ".png") {
		filename += ".png"
	}

	// Create output directory if it doesn't exist
	outputDir := t.ctx.config.OutputDir
	if err := os.MkdirAll(outputDir, 0755); err != nil {
		return "", fmt.Errorf("failed to create output directory: %w", err)
	}

	filePath := filepath.Join(outputDir, filename)

	if selector != "" {
		// Screenshot of specific element
		locator := resolveSelector(page, selector)
		_, err = locator.Screenshot(playwright.LocatorScreenshotOptions{
			Path: playwright.String(filePath),
		})
	} else {
		// Full page or viewport screenshot
		_, err = page.Screenshot(playwright.PageScreenshotOptions{
			Path:     playwright.String(filePath),
			FullPage: playwright.Bool(fullPage),
		})
	}

	if err != nil {
		return "", fmt.Errorf("failed to take screenshot: %w", err)
	}

	return fmt.Sprintf("Screenshot saved to: %s", filePath), nil
}

// ============================================================================
// EvaluateTool - Execute JavaScript
// ============================================================================

type EvaluateTool struct {
	ctx *Context
}

func NewEvaluateTool(ctx *Context) *EvaluateTool {
	return &EvaluateTool{ctx: ctx}
}

func (t *EvaluateTool) Name() string {
	return "browser_evaluate"
}

func (t *EvaluateTool) Description() string {
	return "Execute JavaScript code in the browser context and return the result"
}

func (t *EvaluateTool) ParametersSchema() map[string]interface{} {
	return map[string]interface{}{
		"type": "object",
		"properties": map[string]interface{}{
			"script": map[string]interface{}{
				"type":        "string",
				"description": "JavaScript code to execute",
			},
		},
		"required": []string{"script"},
	}
}

func (t *EvaluateTool) Execute(params map[string]interface{}) (string, error) {
	script := getStringParam(params, "script", "")
	if script == "" {
		return "", fmt.Errorf("script parameter is required")
	}

	page, err := t.ctx.ActivePage()
	if err != nil {
		return "", err
	}

	result, err := page.Evaluate(script)
	if err != nil {
		return "", fmt.Errorf("failed to evaluate script: %w", err)
	}

	// Convert result to string
	if result == nil {
		return "undefined", nil
	}

	resultBytes, err := json.Marshal(result)
	if err != nil {
		return fmt.Sprintf("%v", result), nil
	}

	return string(resultBytes), nil
}

// ============================================================================
// TabsListTool - List open tabs
// ============================================================================

type TabsListTool struct {
	ctx *Context
}

func NewTabsListTool(ctx *Context) *TabsListTool {
	return &TabsListTool{ctx: ctx}
}

func (t *TabsListTool) Name() string {
	return "browser_tabs_list"
}

func (t *TabsListTool) Description() string {
	return "List all open browser tabs with their IDs, URLs, and titles"
}

func (t *TabsListTool) ParametersSchema() map[string]interface{} {
	return map[string]interface{}{
		"type":       "object",
		"properties": map[string]interface{}{},
	}
}

func (t *TabsListTool) Execute(params map[string]interface{}) (string, error) {
	tabs := t.ctx.ListTabs()

	if len(tabs) == 0 {
		return "No tabs open", nil
	}

	result, err := json.MarshalIndent(tabs, "", "  ")
	if err != nil {
		return "", fmt.Errorf("failed to serialize tabs: %w", err)
	}

	return string(result), nil
}

// ============================================================================
// TabNewTool - Open new tab
// ============================================================================

type TabNewTool struct {
	ctx *Context
}

func NewTabNewTool(ctx *Context) *TabNewTool {
	return &TabNewTool{ctx: ctx}
}

func (t *TabNewTool) Name() string {
	return "browser_tab_new"
}

func (t *TabNewTool) Description() string {
	return "Open a new browser tab, optionally navigating to a URL"
}

func (t *TabNewTool) ParametersSchema() map[string]interface{} {
	return map[string]interface{}{
		"type": "object",
		"properties": map[string]interface{}{
			"url": map[string]interface{}{
				"type":        "string",
				"description": "Optional URL to navigate to in the new tab",
			},
		},
	}
}

func (t *TabNewTool) Execute(params map[string]interface{}) (string, error) {
	url := getStringParam(params, "url", "")

	tabID, err := t.ctx.NewTab(url)
	if err != nil {
		return "", err
	}

	if url != "" {
		return fmt.Sprintf("Opened new tab (ID: %s) and navigated to %s", tabID, url), nil
	}
	return fmt.Sprintf("Opened new tab (ID: %s)", tabID), nil
}

// ============================================================================
// TabSelectTool - Switch to tab
// ============================================================================

type TabSelectTool struct {
	ctx *Context
}

func NewTabSelectTool(ctx *Context) *TabSelectTool {
	return &TabSelectTool{ctx: ctx}
}

func (t *TabSelectTool) Name() string {
	return "browser_tab_select"
}

func (t *TabSelectTool) Description() string {
	return "Switch to a specific browser tab by its ID"
}

func (t *TabSelectTool) ParametersSchema() map[string]interface{} {
	return map[string]interface{}{
		"type": "object",
		"properties": map[string]interface{}{
			"tab_id": map[string]interface{}{
				"type":        "string",
				"description": "ID of the tab to switch to",
			},
		},
		"required": []string{"tab_id"},
	}
}

func (t *TabSelectTool) Execute(params map[string]interface{}) (string, error) {
	tabID := getStringParam(params, "tab_id", "")
	if tabID == "" {
		return "", fmt.Errorf("tab_id parameter is required")
	}

	if err := t.ctx.SelectTab(tabID); err != nil {
		return "", err
	}

	return fmt.Sprintf("Switched to tab: %s", tabID), nil
}

// ============================================================================
// TabCloseTool - Close tab
// ============================================================================

type TabCloseTool struct {
	ctx *Context
}

func NewTabCloseTool(ctx *Context) *TabCloseTool {
	return &TabCloseTool{ctx: ctx}
}

func (t *TabCloseTool) Name() string {
	return "browser_tab_close"
}

func (t *TabCloseTool) Description() string {
	return "Close a browser tab by its ID"
}

func (t *TabCloseTool) ParametersSchema() map[string]interface{} {
	return map[string]interface{}{
		"type": "object",
		"properties": map[string]interface{}{
			"tab_id": map[string]interface{}{
				"type":        "string",
				"description": "ID of the tab to close (closes active tab if not specified)",
			},
		},
	}
}

func (t *TabCloseTool) Execute(params map[string]interface{}) (string, error) {
	tabID := getStringParam(params, "tab_id", "")

	// If no tab ID specified, use active tab
	if tabID == "" {
		tabs := t.ctx.ListTabs()
		for _, tab := range tabs {
			if tab.Active {
				tabID = tab.ID
				break
			}
		}
		if tabID == "" {
			return "", fmt.Errorf("no active tab to close")
		}
	}

	if err := t.ctx.CloseTab(tabID); err != nil {
		return "", err
	}

	return fmt.Sprintf("Closed tab: %s", tabID), nil
}

// ============================================================================
// WaitTool - Wait for element or time
// ============================================================================

type WaitTool struct {
	ctx *Context
}

func NewWaitTool(ctx *Context) *WaitTool {
	return &WaitTool{ctx: ctx}
}

func (t *WaitTool) Name() string {
	return "browser_wait"
}

func (t *WaitTool) Description() string {
	return "Wait for an element to appear, disappear, or for a specified amount of time"
}

func (t *WaitTool) ParametersSchema() map[string]interface{} {
	return map[string]interface{}{
		"type": "object",
		"properties": map[string]interface{}{
			"selector": map[string]interface{}{
				"type":        "string",
				"description": "Element selector to wait for",
			},
			"state": map[string]interface{}{
				"type":        "string",
				"description": "State to wait for: visible, hidden, attached, detached (default: visible)",
				"enum":        []string{"visible", "hidden", "attached", "detached"},
			},
			"timeout": map[string]interface{}{
				"type":        "number",
				"description": "Maximum time to wait in milliseconds",
			},
			"time": map[string]interface{}{
				"type":        "number",
				"description": "Time to wait in milliseconds (if no selector provided)",
			},
		},
	}
}

func (t *WaitTool) Execute(params map[string]interface{}) (string, error) {
	selector := getStringParam(params, "selector", "")
	state := getStringParam(params, "state", "visible")
	timeout := getFloatParam(params, "timeout", 30000)
	waitTime := getFloatParam(params, "time", 0)

	// If just waiting for time
	if selector == "" && waitTime > 0 {
		time.Sleep(time.Duration(waitTime) * time.Millisecond)
		return fmt.Sprintf("Waited for %v ms", waitTime), nil
	}

	if selector == "" {
		return "", fmt.Errorf("selector or time parameter is required")
	}

	page, err := t.ctx.ActivePage()
	if err != nil {
		return "", err
	}

	locator := resolveSelector(page, selector)

	var waitState *playwright.WaitForSelectorState
	switch state {
	case "visible":
		waitState = playwright.WaitForSelectorStateVisible
	case "hidden":
		waitState = playwright.WaitForSelectorStateHidden
	case "attached":
		waitState = playwright.WaitForSelectorStateAttached
	case "detached":
		waitState = playwright.WaitForSelectorStateDetached
	default:
		waitState = playwright.WaitForSelectorStateVisible
	}

	if err := locator.WaitFor(playwright.LocatorWaitForOptions{
		State:   waitState,
		Timeout: playwright.Float(timeout),
	}); err != nil {
		return "", fmt.Errorf("timeout waiting for %s to be %s: %w", selector, state, err)
	}

	return fmt.Sprintf("Element %s is now %s", selector, state), nil
}

// ============================================================================
// NavigateBackTool - Go back in history
// ============================================================================

type NavigateBackTool struct {
	ctx *Context
}

func NewNavigateBackTool(ctx *Context) *NavigateBackTool {
	return &NavigateBackTool{ctx: ctx}
}

func (t *NavigateBackTool) Name() string {
	return "browser_navigate_back"
}

func (t *NavigateBackTool) Description() string {
	return "Navigate back to the previous page in browser history"
}

func (t *NavigateBackTool) ParametersSchema() map[string]interface{} {
	return map[string]interface{}{
		"type":       "object",
		"properties": map[string]interface{}{},
	}
}

func (t *NavigateBackTool) Execute(params map[string]interface{}) (string, error) {
	page, err := t.ctx.ActivePage()
	if err != nil {
		return "", err
	}

	resp, err := page.GoBack()
	if err != nil {
		return "", fmt.Errorf("failed to go back: %w", err)
	}

	url := page.URL()
	status := 0
	if resp != nil {
		status = resp.Status()
	}

	return fmt.Sprintf("Navigated back to %s (status: %d)", url, status), nil
}

// ============================================================================
// HandleDialogTool - Handle JavaScript dialogs
// ============================================================================

type HandleDialogTool struct {
	ctx *Context
}

func NewHandleDialogTool(ctx *Context) *HandleDialogTool {
	return &HandleDialogTool{ctx: ctx}
}

func (t *HandleDialogTool) Name() string {
	return "browser_handle_dialog"
}

func (t *HandleDialogTool) Description() string {
	return "Handle JavaScript dialogs (alert, confirm, prompt). Sets up a handler for the next dialog that appears."
}

func (t *HandleDialogTool) ParametersSchema() map[string]interface{} {
	return map[string]interface{}{
		"type": "object",
		"properties": map[string]interface{}{
			"action": map[string]interface{}{
				"type":        "string",
				"description": "Action to take: accept or dismiss",
				"enum":        []string{"accept", "dismiss"},
			},
			"promptText": map[string]interface{}{
				"type":        "string",
				"description": "Text to enter for prompt dialogs (optional, only used when action is accept)",
			},
		},
		"required": []string{"action"},
	}
}

func (t *HandleDialogTool) Execute(params map[string]interface{}) (string, error) {
	action := getStringParam(params, "action", "")
	promptText := getStringParam(params, "promptText", "")

	if action == "" {
		return "", fmt.Errorf("action parameter is required")
	}

	if action != "accept" && action != "dismiss" {
		return "", fmt.Errorf("action must be 'accept' or 'dismiss'")
	}

	page, err := t.ctx.ActivePage()
	if err != nil {
		return "", err
	}

	// Set up a one-time dialog handler
	page.OnDialog(func(dialog playwright.Dialog) {
		dialogType := dialog.Type()
		if action == "accept" {
			if promptText != "" && dialogType == "prompt" {
				dialog.Accept(promptText)
			} else {
				dialog.Accept()
			}
		} else {
			dialog.Dismiss()
		}
	})

	return fmt.Sprintf("Dialog handler set up to %s dialogs", action), nil
}

// ============================================================================
// FileUploadTool - Upload files to file inputs
// ============================================================================

type FileUploadTool struct {
	ctx *Context
}

func NewFileUploadTool(ctx *Context) *FileUploadTool {
	return &FileUploadTool{ctx: ctx}
}

func (t *FileUploadTool) Name() string {
	return "browser_file_upload"
}

func (t *FileUploadTool) Description() string {
	return "Upload files to a file input element. Provide the selector for the file input and an array of file paths."
}

func (t *FileUploadTool) ParametersSchema() map[string]interface{} {
	return map[string]interface{}{
		"type": "object",
		"properties": map[string]interface{}{
			"selector": map[string]interface{}{
				"type":        "string",
				"description": "Selector for the file input element",
			},
			"files": map[string]interface{}{
				"type":        "array",
				"description": "Array of file paths to upload",
				"items": map[string]interface{}{
					"type": "string",
				},
			},
		},
		"required": []string{"selector", "files"},
	}
}

func (t *FileUploadTool) Execute(params map[string]interface{}) (string, error) {
	selector := getStringParam(params, "selector", "")
	if selector == "" {
		return "", fmt.Errorf("selector parameter is required")
	}

	filesParam, ok := params["files"]
	if !ok {
		return "", fmt.Errorf("files parameter is required")
	}

	// Convert files parameter to []string
	var files []string
	switch v := filesParam.(type) {
	case []interface{}:
		for _, f := range v {
			if s, ok := f.(string); ok {
				files = append(files, s)
			} else {
				return "", fmt.Errorf("files must be an array of strings")
			}
		}
	case []string:
		files = v
	default:
		return "", fmt.Errorf("files must be an array of strings")
	}

	if len(files) == 0 {
		return "", fmt.Errorf("at least one file path is required")
	}

	page, err := t.ctx.ActivePage()
	if err != nil {
		return "", err
	}

	locator := resolveSelector(page, selector)

	// Use SetInputFiles to upload files
	if err := locator.SetInputFiles(files); err != nil {
		return "", fmt.Errorf("failed to upload files to %s: %w", selector, err)
	}

	return fmt.Sprintf("Uploaded %d file(s) to element: %s", len(files), selector), nil
}

// Ensure all tools implement the Tool interface (compile-time check)
var (
	_ toolInterface = (*NavigateTool)(nil)
	_ toolInterface = (*ClickTool)(nil)
	_ toolInterface = (*FillTool)(nil)
	_ toolInterface = (*TypeTool)(nil)
	_ toolInterface = (*PressKeyTool)(nil)
	_ toolInterface = (*HoverTool)(nil)
	_ toolInterface = (*SelectOptionTool)(nil)
	_ toolInterface = (*SnapshotTool)(nil)
	_ toolInterface = (*ScreenshotTool)(nil)
	_ toolInterface = (*EvaluateTool)(nil)
	_ toolInterface = (*TabsListTool)(nil)
	_ toolInterface = (*TabNewTool)(nil)
	_ toolInterface = (*TabSelectTool)(nil)
	_ toolInterface = (*TabCloseTool)(nil)
	_ toolInterface = (*WaitTool)(nil)
	_ toolInterface = (*NavigateBackTool)(nil)
	_ toolInterface = (*HandleDialogTool)(nil)
	_ toolInterface = (*FileUploadTool)(nil)
)

// toolInterface is a local interface to verify tool implementations
type toolInterface interface {
	Name() string
	Description() string
	ParametersSchema() map[string]interface{}
	Execute(params map[string]interface{}) (string, error)
}

// Unused but useful for selector resolution patterns
var selectorPatterns = map[string]*regexp.Regexp{
	"testid":      regexp.MustCompile(`^testid:(.+)$`),
	"role":        regexp.MustCompile(`^role:([^:]+)(?::(.+))?$`),
	"text":        regexp.MustCompile(`^text:(.+)$`),
	"label":       regexp.MustCompile(`^label:(.+)$`),
	"placeholder": regexp.MustCompile(`^placeholder:(.+)$`),
}
