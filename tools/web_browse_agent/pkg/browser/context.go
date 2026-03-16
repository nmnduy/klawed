package browser

import (
	"fmt"
	"log"
	"os"
	"sync"
	"time"

	"github.com/playwright-community/playwright-go"
)

// logger is a package-level logger for browser operations
var logger *log.Logger

func init() {
	// Initialize logger - will log to stderr by default
	// Can be redirected to file via WEB_AGENT_LOG_FILE env var
	logFile := os.Getenv("WEB_AGENT_LOG_FILE")
	if logFile != "" {
		f, err := os.OpenFile(logFile, os.O_APPEND|os.O_CREATE|os.O_WRONLY, 0644)
		if err == nil {
			logger = log.New(f, "[web-agent] ", log.LstdFlags|log.Lmicroseconds)
		}
	}
	if logger == nil {
		logger = log.New(os.Stderr, "[web-agent] ", log.LstdFlags|log.Lmicroseconds)
	}
}

// BrowserContext manages a Playwright browser instance and its tabs
type BrowserContext struct {
	// Playwright instances
	browser playwright.Browser
	context playwright.BrowserContext
	pw      *playwright.Playwright // Keep reference to stop it on cleanup

	// Tab management
	tabs      map[string]*Tab
	activeTab string
	tabMu     sync.RWMutex

	// Configuration
	headless          bool
	userDataDir       string
	browserExecutable string
	proxy             string
}

// Tab represents a browser tab/page
type Tab struct {
	ID    string
	Page  playwright.Page
	URL   string
	Title string
}

// BrowserContextConfig holds configuration for creating a new browser context
type BrowserContextConfig struct {
	Headless          bool
	UserDataDir       string // Path to persistent user data directory (empty = no persistence)
	BrowserExecutable string // Path to browser executable (empty = auto-detect)
	Proxy             string // HTTP/SOCKS proxy URL, e.g. "http://host:8080" (empty = no proxy)
}

// NewBrowserContext creates a new browser context
func NewBrowserContext(headless bool) (*BrowserContext, error) {
	return NewBrowserContextWithConfig(BrowserContextConfig{
		Headless: headless,
	})
}

// findChromiumExecutable looks for Chromium browser in common locations
func findChromiumExecutable() string {
	// Check environment variable first
	if envPath := os.Getenv("CHROMIUM_EXECUTABLE"); envPath != "" {
		logger.Printf("Using CHROMIUM_EXECUTABLE from env: %s", envPath)
		if _, err := os.Stat(envPath); err == nil {
			return envPath
		}
		logger.Printf("Warning: CHROMIUM_EXECUTABLE points to non-existent file: %s", envPath)
	}

	// Common Chromium/Chrome paths on different systems
	candidates := []string{
		// Debian/Ubuntu system chromium
		"/usr/bin/chromium",
		"/usr/bin/chromium-browser",
		// Chrome paths
		"/usr/bin/google-chrome",
		"/usr/bin/google-chrome-stable",
		// macOS paths
		"/Applications/Google Chrome.app/Contents/MacOS/Google Chrome",
		"/Applications/Chromium.app/Contents/MacOS/Chromium",
		// Additional Linux paths
		"/snap/bin/chromium",
		"/usr/lib/chromium/chromium",
	}

	for _, path := range candidates {
		if _, err := os.Stat(path); err == nil {
			logger.Printf("Found Chromium at: %s", path)
			return path
		}
	}

	logger.Printf("No system Chromium found, will use Playwright's bundled browser")
	return ""
}

// NewBrowserContextWithConfig creates a new browser context with full configuration
func NewBrowserContextWithConfig(config BrowserContextConfig) (*BrowserContext, error) {
	logger.Printf("Creating browser context with config: Headless=%v, UserDataDir=%s, BrowserExecutable=%s, Proxy=%s",
		config.Headless, config.UserDataDir, config.BrowserExecutable, config.Proxy)

	stealth := stealthEnabled()
	if stealth {
		logger.Printf("Stealth mode enabled (set WEB_AGENT_STEALTH=0 to disable)")
	}

	// Initialize Playwright
	logger.Printf("Starting Playwright...")
	pw, err := playwright.Run()
	if err != nil {
		logger.Printf("ERROR: Failed to start Playwright: %v", err)
		return nil, fmt.Errorf("failed to start Playwright: %w", err)
	}
	logger.Printf("Playwright started successfully")

	var browser playwright.Browser
	var context playwright.BrowserContext

	// Use explicit browser executable if provided, otherwise find system Chromium
	var chromiumPath string
	if config.BrowserExecutable != "" {
		chromiumPath = config.BrowserExecutable
		logger.Printf("Using explicit browser executable: %s", chromiumPath)
	} else {
		chromiumPath = findChromiumExecutable()
	}

	// Build a *playwright.Proxy if a proxy URL was provided
	var playwrightProxy *playwright.Proxy
	if config.Proxy != "" {
		playwrightProxy = &playwright.Proxy{Server: config.Proxy}
		logger.Printf("Using proxy: %s", config.Proxy)
	}

	// Use launchPersistentContext if userDataDir is specified for persistent storage
	if config.UserDataDir != "" {
		logger.Printf("Launching persistent context with user data dir: %s", config.UserDataDir)
		var launchOptions playwright.BrowserTypeLaunchPersistentContextOptions
		if stealth {
			launchOptions = stealthPersistentContextOptions(config.Headless, chromiumPath, playwrightProxy)
		} else {
			launchOptions = playwright.BrowserTypeLaunchPersistentContextOptions{
				Headless: playwright.Bool(config.Headless),
			}
			if chromiumPath != "" {
				launchOptions.ExecutablePath = playwright.String(chromiumPath)
			}
			if playwrightProxy != nil {
				launchOptions.Proxy = playwrightProxy
			}
		}
		if chromiumPath != "" {
			logger.Printf("Using Chromium executable: %s", chromiumPath)
		}
		context, err = pw.Chromium.LaunchPersistentContext(config.UserDataDir, launchOptions)
		if err != nil {
			logger.Printf("ERROR: Failed to launch persistent context: %v", err)
			pw.Stop()
			return nil, fmt.Errorf("failed to launch persistent context: %w", err)
		}
		logger.Printf("Persistent context launched successfully")
		// Note: browser is nil when using persistent context
		browser = nil
	} else {
		logger.Printf("Launching regular browser context")
		var launchOptions playwright.BrowserTypeLaunchOptions
		if stealth {
			launchOptions = stealthLaunchOptions(config.Headless, chromiumPath, playwrightProxy)
		} else {
			launchOptions = playwright.BrowserTypeLaunchOptions{
				Headless: playwright.Bool(config.Headless),
			}
			if chromiumPath != "" {
				launchOptions.ExecutablePath = playwright.String(chromiumPath)
			}
			if playwrightProxy != nil {
				launchOptions.Proxy = playwrightProxy
			}
		}
		if chromiumPath != "" {
			logger.Printf("Using Chromium executable: %s", chromiumPath)
		}
		browser, err = pw.Chromium.Launch(launchOptions)
		if err != nil {
			logger.Printf("ERROR: Failed to launch browser: %v", err)
			pw.Stop()
			return nil, fmt.Errorf("failed to launch browser: %w", err)
		}
		logger.Printf("Browser launched successfully")

		// Create browser context
		logger.Printf("Creating new browser context")
		var ctxOptions playwright.BrowserNewContextOptions
		if stealth {
			ctxOptions = stealthContextOptions(playwrightProxy)
		} else {
			ctxOptions = playwright.BrowserNewContextOptions{}
			if playwrightProxy != nil {
				ctxOptions.Proxy = playwrightProxy
			}
		}
		context, err = browser.NewContext(ctxOptions)
		if err != nil {
			logger.Printf("ERROR: Failed to create browser context: %v", err)
			browser.Close()
			pw.Stop()
			return nil, fmt.Errorf("failed to create browser context: %w", err)
		}
		logger.Printf("Browser context created successfully")
	}

	// Inject stealth init script into every page before any other script
	if stealth {
		if err := context.AddInitScript(playwright.Script{
			Content: playwright.String(stealthInitScript),
		}); err != nil {
			logger.Printf("WARNING: Failed to add stealth init script: %v", err)
		} else {
			logger.Printf("Stealth init script injected into browser context")
		}
	}

	logger.Printf("Browser context ready")
	return &BrowserContext{
		browser:           browser,
		context:           context,
		pw:                pw,
		tabs:              make(map[string]*Tab),
		headless:          config.Headless,
		userDataDir:       config.UserDataDir,
		browserExecutable: config.BrowserExecutable,
		proxy:             config.Proxy,
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

	// Close browser (may be nil if using persistent context)
	if bc.browser != nil {
		bc.browser.Close()
	}

	// Stop Playwright
	if bc.pw != nil {
		if err := bc.pw.Stop(); err != nil {
			return fmt.Errorf("failed to stop Playwright: %w", err)
		}
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
