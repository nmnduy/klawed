package browser

import (
	"fmt"
	"sync"

	"github.com/google/uuid"
	"github.com/playwright-community/playwright-go"
)

// TabInfo holds information about a browser tab
type TabInfo struct {
	ID     string `json:"id"`
	URL    string `json:"url"`
	Title  string `json:"title"`
	Active bool   `json:"active"`
}

// Context manages the browser instance and tabs
type Context struct {
	pw      *playwright.Playwright
	browser playwright.Browser
	context playwright.BrowserContext
	pages   map[string]playwright.Page // tab ID -> page
	active  string                     // active tab ID
	config  *Config
	mu      sync.RWMutex
}

// NewContext creates a new browser context with the given configuration
func NewContext(config *Config) (*Context, error) {
	if config == nil {
		config = DefaultConfig()
	}

	if err := config.Validate(); err != nil {
		return nil, fmt.Errorf("invalid config: %w", err)
	}

	// Install playwright browsers if needed
	if err := playwright.Install(); err != nil {
		return nil, fmt.Errorf("failed to install playwright: %w", err)
	}

	// Start playwright
	pw, err := playwright.Run()
	if err != nil {
		return nil, fmt.Errorf("failed to start playwright: %w", err)
	}

	// Select browser type
	var browserType playwright.BrowserType
	switch config.BrowserType {
	case "FIREFOX":
		browserType = pw.Firefox
	case "WEBKIT":
		browserType = pw.WebKit
	default:
		browserType = pw.Chromium
	}

	// Launch browser options
	launchOptions := playwright.BrowserTypeLaunchOptions{
		Headless: playwright.Bool(config.Headless),
	}

	browser, err := browserType.Launch(launchOptions)
	if err != nil {
		pw.Stop()
		return nil, fmt.Errorf("failed to launch browser: %w", err)
	}

	// Create browser context options
	contextOptions := playwright.BrowserNewContextOptions{
		Viewport: &playwright.Size{
			Width:  config.ViewportWidth,
			Height: config.ViewportHeight,
		},
	}

	// If user data dir is specified, use persistent context instead
	var ctx playwright.BrowserContext
	if config.UserDataDir != "" {
		// Close the non-persistent browser and use persistent context
		browser.Close()
		persistentOptions := playwright.BrowserTypeLaunchPersistentContextOptions{
			Headless: playwright.Bool(config.Headless),
			Viewport: &playwright.Size{
				Width:  config.ViewportWidth,
				Height: config.ViewportHeight,
			},
		}
		ctx, err = browserType.LaunchPersistentContext(config.UserDataDir, persistentOptions)
		if err != nil {
			pw.Stop()
			return nil, fmt.Errorf("failed to create persistent context: %w", err)
		}
		browser = nil // persistent context doesn't have a separate browser
	} else {
		ctx, err = browser.NewContext(contextOptions)
		if err != nil {
			browser.Close()
			pw.Stop()
			return nil, fmt.Errorf("failed to create browser context: %w", err)
		}
	}

	// Set default timeouts
	ctx.SetDefaultTimeout(config.ActionTimeout)
	ctx.SetDefaultNavigationTimeout(config.NavigationTimeout)

	return &Context{
		pw:      pw,
		browser: browser,
		context: ctx,
		pages:   make(map[string]playwright.Page),
		config:  config,
	}, nil
}

// Close closes the browser context and all resources
func (c *Context) Close() error {
	c.mu.Lock()
	defer c.mu.Unlock()

	var errs []error

	// Close all pages
	for _, page := range c.pages {
		if err := page.Close(); err != nil {
			errs = append(errs, err)
		}
	}
	c.pages = make(map[string]playwright.Page)
	c.active = ""

	// Close context
	if c.context != nil {
		if err := c.context.Close(); err != nil {
			errs = append(errs, err)
		}
	}

	// Close browser (if not persistent context)
	if c.browser != nil {
		if err := c.browser.Close(); err != nil {
			errs = append(errs, err)
		}
	}

	// Stop playwright
	if c.pw != nil {
		if err := c.pw.Stop(); err != nil {
			errs = append(errs, err)
		}
	}

	if len(errs) > 0 {
		return fmt.Errorf("errors during close: %v", errs)
	}
	return nil
}

// ActivePage returns the currently active page
func (c *Context) ActivePage() (playwright.Page, error) {
	c.mu.RLock()
	defer c.mu.RUnlock()

	if c.active == "" {
		return nil, fmt.Errorf("no active tab")
	}

	page, ok := c.pages[c.active]
	if !ok {
		return nil, fmt.Errorf("active tab %s not found", c.active)
	}

	return page, nil
}

// NewTab opens a new tab and navigates to the given URL
// Returns the tab ID
func (c *Context) NewTab(url string) (string, error) {
	c.mu.Lock()
	defer c.mu.Unlock()

	page, err := c.context.NewPage()
	if err != nil {
		return "", fmt.Errorf("failed to create new page: %w", err)
	}

	// Navigate to URL if provided
	if url != "" {
		if _, err := page.Goto(url); err != nil {
			page.Close()
			return "", fmt.Errorf("failed to navigate to %s: %w", url, err)
		}
	}

	// Generate tab ID
	tabID := uuid.New().String()[:8]
	c.pages[tabID] = page
	c.active = tabID

	return tabID, nil
}

// CloseTab closes the tab with the given ID
func (c *Context) CloseTab(tabID string) error {
	c.mu.Lock()
	defer c.mu.Unlock()

	page, ok := c.pages[tabID]
	if !ok {
		return fmt.Errorf("tab %s not found", tabID)
	}

	if err := page.Close(); err != nil {
		return fmt.Errorf("failed to close tab: %w", err)
	}

	delete(c.pages, tabID)

	// If we closed the active tab, switch to another one
	if c.active == tabID {
		c.active = ""
		for id := range c.pages {
			c.active = id
			break
		}
	}

	return nil
}

// SelectTab switches to the tab with the given ID
func (c *Context) SelectTab(tabID string) error {
	c.mu.Lock()
	defer c.mu.Unlock()

	if _, ok := c.pages[tabID]; !ok {
		return fmt.Errorf("tab %s not found", tabID)
	}

	c.active = tabID

	// Bring page to front
	if page := c.pages[tabID]; page != nil {
		page.BringToFront()
	}

	return nil
}

// ListTabs returns information about all open tabs
func (c *Context) ListTabs() []TabInfo {
	c.mu.RLock()
	defer c.mu.RUnlock()

	tabs := make([]TabInfo, 0, len(c.pages))
	for id, page := range c.pages {
		title, _ := page.Title()
		tabs = append(tabs, TabInfo{
			ID:     id,
			URL:    page.URL(),
			Title:  title,
			Active: id == c.active,
		})
	}

	return tabs
}

// GetPage returns the page for the given tab ID
func (c *Context) GetPage(tabID string) (playwright.Page, error) {
	c.mu.RLock()
	defer c.mu.RUnlock()

	page, ok := c.pages[tabID]
	if !ok {
		return nil, fmt.Errorf("tab %s not found", tabID)
	}
	return page, nil
}

// Config returns the browser configuration
func (c *Context) Config() *Config {
	return c.config
}

// EnsureActivePage ensures there is an active page, creating one if necessary
func (c *Context) EnsureActivePage() (playwright.Page, error) {
	page, err := c.ActivePage()
	if err == nil {
		return page, nil
	}

	// No active page, create one
	_, err = c.NewTab("")
	if err != nil {
		return nil, err
	}

	return c.ActivePage()
}
