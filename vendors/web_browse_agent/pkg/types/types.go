// Package types provides shared type definitions for the web-browse-agent
package types

// AgentConfig holds configuration for the agent
type AgentConfig struct {
	// Provider is the LLM provider to use (openai, anthropic)
	Provider string
	// Verbose enables verbose output
	Verbose bool
	// MaxIterations is the maximum number of agentic loop iterations
	MaxIterations int
}

// DefaultAgentConfig returns default agent configuration
func DefaultAgentConfig() *AgentConfig {
	return &AgentConfig{
		Provider:      "openai",
		Verbose:       false,
		MaxIterations: 50,
	}
}

// ToolCallResult represents the result of a tool execution
type ToolCallResult struct {
	// Name is the tool name
	Name string `json:"name"`
	// Success indicates if the tool execution was successful
	Success bool `json:"success"`
	// Output is the tool output (if successful)
	Output string `json:"output,omitempty"`
	// Error is the error message (if failed)
	Error string `json:"error,omitempty"`
}

// BrowserTab represents a browser tab
type BrowserTab struct {
	// ID is the unique identifier for the tab
	ID string `json:"id"`
	// URL is the current URL of the tab
	URL string `json:"url"`
	// Title is the page title
	Title string `json:"title"`
	// Active indicates if this is the active tab
	Active bool `json:"active"`
}

// SelectorType represents different selector strategies
type SelectorType string

const (
	// SelectorCSS is a standard CSS selector
	SelectorCSS SelectorType = "css"
	// SelectorTestID is a data-testid selector
	SelectorTestID SelectorType = "testid"
	// SelectorRole is an ARIA role selector
	SelectorRole SelectorType = "role"
	// SelectorText is a text content selector
	SelectorText SelectorType = "text"
	// SelectorLabel is a form label selector
	SelectorLabel SelectorType = "label"
	// SelectorPlaceholder is a placeholder selector
	SelectorPlaceholder SelectorType = "placeholder"
)
