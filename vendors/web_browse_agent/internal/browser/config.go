package browser

import (
	"os"
	"strconv"
	"strings"
)

// Config holds browser configuration settings
type Config struct {
	BrowserType       string  // CHROMIUM, FIREFOX, WEBKIT
	Headless          bool
	UserDataDir       string
	ViewportWidth     int
	ViewportHeight    int
	ActionTimeout     float64 // milliseconds
	NavigationTimeout float64 // milliseconds
	OutputDir         string  // for screenshots
}

// DefaultConfig returns a Config with default values
func DefaultConfig() *Config {
	return &Config{
		BrowserType:       "CHROMIUM",
		Headless:          false,
		UserDataDir:       "",
		ViewportWidth:     1280,
		ViewportHeight:    720,
		ActionTimeout:     5000,
		NavigationTimeout: 30000,
		OutputDir:         ".",
	}
}

// NewConfigFromEnv creates a Config from environment variables
// Environment variables:
//   - BROWSER_TYPE: Browser type (CHROMIUM, FIREFOX, WEBKIT). Default: CHROMIUM
//   - BROWSER_HEADLESS: Run in headless mode (true/false). Default: false
//   - BROWSER_USER_DATA_DIR: Path to user data directory for persistent sessions
//   - BROWSER_VIEWPORT_WIDTH: Viewport width in pixels. Default: 1280
//   - BROWSER_VIEWPORT_HEIGHT: Viewport height in pixels. Default: 720
//   - BROWSER_ACTION_TIMEOUT: Timeout for actions in milliseconds. Default: 5000
//   - BROWSER_NAVIGATION_TIMEOUT: Timeout for navigation in milliseconds. Default: 30000
//   - BROWSER_OUTPUT_DIR: Directory for screenshots and other outputs. Default: current directory
func NewConfigFromEnv() *Config {
	config := DefaultConfig()

	if browserType := os.Getenv("BROWSER_TYPE"); browserType != "" {
		config.BrowserType = strings.ToUpper(browserType)
	}

	if headless := os.Getenv("BROWSER_HEADLESS"); headless != "" {
		config.Headless = strings.ToLower(headless) == "true" || headless == "1"
	}

	if userDataDir := os.Getenv("BROWSER_USER_DATA_DIR"); userDataDir != "" {
		config.UserDataDir = userDataDir
	}

	if width := os.Getenv("BROWSER_VIEWPORT_WIDTH"); width != "" {
		if w, err := strconv.Atoi(width); err == nil && w > 0 {
			config.ViewportWidth = w
		}
	}

	if height := os.Getenv("BROWSER_VIEWPORT_HEIGHT"); height != "" {
		if h, err := strconv.Atoi(height); err == nil && h > 0 {
			config.ViewportHeight = h
		}
	}

	if actionTimeout := os.Getenv("BROWSER_ACTION_TIMEOUT"); actionTimeout != "" {
		if t, err := strconv.ParseFloat(actionTimeout, 64); err == nil && t > 0 {
			config.ActionTimeout = t
		}
	}

	if navTimeout := os.Getenv("BROWSER_NAVIGATION_TIMEOUT"); navTimeout != "" {
		if t, err := strconv.ParseFloat(navTimeout, 64); err == nil && t > 0 {
			config.NavigationTimeout = t
		}
	}

	if outputDir := os.Getenv("BROWSER_OUTPUT_DIR"); outputDir != "" {
		config.OutputDir = outputDir
	}

	return config
}

// Validate checks if the configuration is valid
func (c *Config) Validate() error {
	validBrowserTypes := map[string]bool{
		"CHROMIUM": true,
		"FIREFOX":  true,
		"WEBKIT":   true,
	}
	if !validBrowserTypes[c.BrowserType] {
		c.BrowserType = "CHROMIUM" // default to chromium if invalid
	}
	return nil
}
