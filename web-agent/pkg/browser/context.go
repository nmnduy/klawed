package browser

import (
	"fmt"
	"sync"
	"time"

	"github.com/playwright-community/playwright-go"
)

// BrowserContext manages a Playwright browser instance and its tabs
type BrowserContext struct {
	// Playwright instances
	browser playwright.Browser
	context playwright.BrowserContext

	// Tab management
	tabs      map[string]*Tab
	activeTab string
	tabMu     sync.RWMutex

	// Configuration
	headless bool
}

// Tab represents a browser tab/page
type Tab struct {
	ID    string
	Page  playwright.Page
	URL   string
	Title string
}

// NewBrowserContext creates a new browser context
func NewBrowserContext(headless bool) (*BrowserContext, error) {
	// Initialize Playwright
	pw, err := playwright.Run()
	if err != nil {
		return nil, fmt.Errorf("failed to start Playwright: %w", err)
	}

	// Launch browser
	var browser playwright.Browser
	if headless {
		browser, err = pw.Chromium.Launch(playwright.BrowserTypeLaunchOptions{
			Headless: playwright.Bool(true),
		})
	} else {
		browser, err = pw.Chromium.Launch(playwright.BrowserTypeLaunchOptions{
			Headless: playwright.Bool(false),
		})
	}
	if err != nil {
		pw.Stop()
		return nil, fmt.Errorf("failed to launch browser: %w", err)
	}

	// Create browser context
	context, err := browser.NewContext()
	if err != nil {
		browser.Close()
		pw.Stop()
		return nil, fmt.Errorf("failed to create browser context: %w", err)
	}

	return &BrowserContext{
		browser:  browser,
		context:  context,
		tabs:     make(map[string]*Tab),
		headless: headless,
	}, nil
}

// Close closes the browser context and all resources
func (bc *BrowserContext) Close() error {
	bc.tabMu.Lock()
	defer bc.tabMu.Unlock()

	// Close all tabs
	for _, tab := range bc.tabs {
		if tab.Page != nil {
			tab.Page.Close()
		}
	}
	bc.tabs = make(map[string]*Tab)

	// Close browser context
	if bc.context != nil {
		bc.context.Close()
	}

	// Close browser
	if bc.browser != nil {
		bc.browser.Close()
	}

	return nil
}

// NewTab creates a new browser tab
func (bc *BrowserContext) NewTab() (*Tab, error) {
	page, err := bc.context.NewPage()
	if err != nil {
		return nil, fmt.Errorf("failed to create new page: %w", err)
	}

	tab := &Tab{
		ID:   generateTabID(),
		Page: page,
	}

	bc.tabMu.Lock()
	bc.tabs[tab.ID] = tab
	if bc.activeTab == "" {
		bc.activeTab = tab.ID
	}
	bc.tabMu.Unlock()

	return tab, nil
}

// GetTab returns a tab by ID
func (bc *BrowserContext) GetTab(tabID string) (*Tab, bool) {
	bc.tabMu.RLock()
	defer bc.tabMu.RUnlock()

	tab, ok := bc.tabs[tabID]
	return tab, ok
}

// GetActiveTab returns the active tab
func (bc *BrowserContext) GetActiveTab() (*Tab, bool) {
	bc.tabMu.RLock()
	defer bc.tabMu.RUnlock()

	if bc.activeTab == "" {
		return nil, false
	}

	tab, ok := bc.tabs[bc.activeTab]
	return tab, ok
}

// SetActiveTab sets the active tab
func (bc *BrowserContext) SetActiveTab(tabID string) bool {
	bc.tabMu.Lock()
	defer bc.tabMu.Unlock()

	if _, ok := bc.tabs[tabID]; !ok {
		return false
	}

	bc.activeTab = tabID
	return true
}

// CloseTab closes a tab by ID
func (bc *BrowserContext) CloseTab(tabID string) bool {
	bc.tabMu.Lock()
	defer bc.tabMu.Unlock()

	tab, ok := bc.tabs[tabID]
	if !ok {
		return false
	}

	// Close the page
	if tab.Page != nil {
		tab.Page.Close()
	}

	// Remove from tabs map
	delete(bc.tabs, tabID)

	// Update active tab if needed
	if bc.activeTab == tabID {
		bc.activeTab = ""
		// Set another tab as active if available
		for id := range bc.tabs {
			bc.activeTab = id
			break
		}
	}

	return true
}

// ListTabs returns information about all tabs
func (bc *BrowserContext) ListTabs() []TabInfo {
	bc.tabMu.RLock()
	defer bc.tabMu.RUnlock()

	var tabs []TabInfo
	for id, tab := range bc.tabs {
		tabs = append(tabs, TabInfo{
			ID:     id,
			URL:    tab.URL,
			Title:  tab.Title,
			Active: id == bc.activeTab,
		})
	}

	return tabs
}

// UpdateTabInfo updates tab URL and title
func (bc *BrowserContext) UpdateTabInfo(tabID, url, title string) {
	bc.tabMu.Lock()
	defer bc.tabMu.Unlock()

	if tab, ok := bc.tabs[tabID]; ok {
		tab.URL = url
		tab.Title = title
	}
}

// TabInfo represents tab information for IPC
type TabInfo struct {
	ID     string `json:"id"`
	URL    string `json:"url"`
	Title  string `json:"title"`
	Active bool   `json:"active"`
}

// generateTabID generates a unique tab ID
func generateTabID() string {
	// Simple implementation - in production, use UUID or similar
	return fmt.Sprintf("tab_%d", time.Now().UnixNano())
}

// Helper function to get current time
func timeNow() time.Time {
	return time.Now()
}