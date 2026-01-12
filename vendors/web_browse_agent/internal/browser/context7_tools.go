package browser

import (
	"encoding/json"
	"fmt"
	"io"
	"net/http"
	"net/url"
	"strings"
	"time"
)

const (
	context7APIURL = "https://context7.com/api"
)

// Context7SearchResult represents a library search result from Context7
type Context7SearchResult struct {
	ID             string   `json:"id"`
	Title          string   `json:"title"`
	Description    string   `json:"description"`
	TotalSnippets  int      `json:"totalSnippets"`
	BenchmarkScore float64  `json:"benchmarkScore,omitempty"`
	TrustScore     float64  `json:"trustScore,omitempty"`
	Versions       []string `json:"versions,omitempty"`
}

// Context7SearchResponse represents the API response for library search
type Context7SearchResponse struct {
	Results []Context7SearchResult `json:"results"`
	Error   string                 `json:"error,omitempty"`
}

// ============================================================================
// Context7SearchTool - Search for library documentation
// ============================================================================

type Context7SearchTool struct {
	ctx    *Context
	apiKey string
}

func NewContext7SearchTool(ctx *Context, apiKey string) *Context7SearchTool {
	return &Context7SearchTool{ctx: ctx, apiKey: apiKey}
}

func (t *Context7SearchTool) Name() string {
	return "context7_search"
}

func (t *Context7SearchTool) Description() string {
	return `Search for library/package documentation using Context7.
This tool finds up-to-date documentation for programming libraries and frameworks.
Use this before context7_docs to find the correct library ID.

Example: Search for "react hooks" to find React documentation about hooks.`
}

func (t *Context7SearchTool) ParametersSchema() map[string]interface{} {
	return map[string]interface{}{
		"type": "object",
		"properties": map[string]interface{}{
			"query": map[string]interface{}{
				"type":        "string",
				"description": "What you're trying to accomplish (used for relevance ranking)",
			},
			"library_name": map[string]interface{}{
				"type":        "string",
				"description": "The library/package name to search for",
			},
		},
		"required": []string{"query", "library_name"},
	}
}

func (t *Context7SearchTool) Execute(params map[string]interface{}) (string, error) {
	query := getStringParam(params, "query", "")
	libraryName := getStringParam(params, "library_name", "")

	if query == "" || libraryName == "" {
		return "", fmt.Errorf("both query and library_name parameters are required")
	}

	// Build the API URL
	apiURL := fmt.Sprintf("%s/v2/libs/search?query=%s&libraryName=%s",
		context7APIURL,
		url.QueryEscape(query),
		url.QueryEscape(libraryName))

	// Make HTTP request
	client := &http.Client{Timeout: 30 * time.Second}
	req, err := http.NewRequest("GET", apiURL, nil)
	if err != nil {
		return "", fmt.Errorf("failed to create request: %w", err)
	}

	// Add API key if available
	if t.apiKey != "" {
		req.Header.Set("X-Context7-API-Key", t.apiKey)
	}

	resp, err := client.Do(req)
	if err != nil {
		return "", fmt.Errorf("failed to search Context7: %w", err)
	}
	defer resp.Body.Close()

	body, err := io.ReadAll(resp.Body)
	if err != nil {
		return "", fmt.Errorf("failed to read response: %w", err)
	}

	if resp.StatusCode != http.StatusOK {
		return "", fmt.Errorf("Context7 API error (status %d): %s", resp.StatusCode, string(body))
	}

	var searchResp Context7SearchResponse
	if err := json.Unmarshal(body, &searchResp); err != nil {
		return "", fmt.Errorf("failed to parse response: %w", err)
	}

	if len(searchResp.Results) == 0 {
		return "No libraries found matching the search query.", nil
	}

	// Format results
	var output strings.Builder
	output.WriteString(fmt.Sprintf("Found %d libraries:\n\n", len(searchResp.Results)))

	for i, lib := range searchResp.Results {
		if i >= 10 {
			output.WriteString(fmt.Sprintf("\n... and %d more results", len(searchResp.Results)-10))
			break
		}
		output.WriteString(fmt.Sprintf("## %s\n", lib.Title))
		output.WriteString(fmt.Sprintf("- **Library ID**: %s\n", lib.ID))
		output.WriteString(fmt.Sprintf("- **Description**: %s\n", lib.Description))
		output.WriteString(fmt.Sprintf("- **Code Snippets**: %d\n", lib.TotalSnippets))
		if lib.BenchmarkScore > 0 {
			output.WriteString(fmt.Sprintf("- **Benchmark Score**: %.0f\n", lib.BenchmarkScore))
		}
		if len(lib.Versions) > 0 {
			output.WriteString(fmt.Sprintf("- **Versions**: %v\n", lib.Versions[:min(5, len(lib.Versions))]))
		}
		output.WriteString("\n")
	}

	output.WriteString("\nUse context7_docs with the library ID to fetch documentation.")

	return output.String(), nil
}

// ============================================================================
// Context7DocsTool - Fetch library documentation
// ============================================================================

type Context7DocsTool struct {
	ctx    *Context
	apiKey string
}

func NewContext7DocsTool(ctx *Context, apiKey string) *Context7DocsTool {
	return &Context7DocsTool{ctx: ctx, apiKey: apiKey}
}

func (t *Context7DocsTool) Name() string {
	return "context7_docs"
}

func (t *Context7DocsTool) Description() string {
	return `Fetch up-to-date documentation for a library using Context7.
You must first use context7_search to find the correct library ID, unless you already know it.

Example library IDs:
- /vercel/next.js - Next.js documentation
- /facebook/react - React documentation
- /mongodb/docs - MongoDB documentation

The query parameter should describe what you want to learn about the library.`
}

func (t *Context7DocsTool) ParametersSchema() map[string]interface{} {
	return map[string]interface{}{
		"type": "object",
		"properties": map[string]interface{}{
			"library_id": map[string]interface{}{
				"type":        "string",
				"description": "Context7-compatible library ID (e.g., '/vercel/next.js', '/facebook/react')",
			},
			"query": map[string]interface{}{
				"type":        "string",
				"description": "What you want to learn about the library (e.g., 'routing', 'authentication', 'hooks')",
			},
		},
		"required": []string{"library_id", "query"},
	}
}

func (t *Context7DocsTool) Execute(params map[string]interface{}) (string, error) {
	libraryID := getStringParam(params, "library_id", "")
	query := getStringParam(params, "query", "")

	if libraryID == "" || query == "" {
		return "", fmt.Errorf("both library_id and query parameters are required")
	}

	// Build the API URL
	apiURL := fmt.Sprintf("%s/v2/context?libraryId=%s&query=%s",
		context7APIURL,
		url.QueryEscape(libraryID),
		url.QueryEscape(query))

	// Make HTTP request
	client := &http.Client{Timeout: 60 * time.Second}
	req, err := http.NewRequest("GET", apiURL, nil)
	if err != nil {
		return "", fmt.Errorf("failed to create request: %w", err)
	}

	// Add API key if available
	if t.apiKey != "" {
		req.Header.Set("X-Context7-API-Key", t.apiKey)
	}

	resp, err := client.Do(req)
	if err != nil {
		return "", fmt.Errorf("failed to fetch Context7 docs: %w", err)
	}
	defer resp.Body.Close()

	body, err := io.ReadAll(resp.Body)
	if err != nil {
		return "", fmt.Errorf("failed to read response: %w", err)
	}

	if resp.StatusCode == http.StatusNotFound {
		return "Library not found. Please verify the library ID using context7_search.", nil
	}

	if resp.StatusCode != http.StatusOK {
		return "", fmt.Errorf("Context7 API error (status %d): %s", resp.StatusCode, string(body))
	}

	content := string(body)
	if content == "" {
		return "No documentation found for this query. Try a different query or verify the library ID.", nil
	}

	// Add source attribution
	header := fmt.Sprintf("# Documentation: %s\nQuery: %s\nSource: Context7 (https://context7.com%s)\n\n---\n\n",
		libraryID, query, libraryID)

	return header + content, nil
}

// Ensure Context7 tools implement the Tool interface
var (
	_ toolInterface = (*Context7SearchTool)(nil)
	_ toolInterface = (*Context7DocsTool)(nil)
)

// RegisterContext7Tools registers Context7 tools with the given registry
func RegisterContext7Tools(registry interface{ Register(tool toolInterface) }, ctx *Context, apiKey string) {
	registry.Register(NewContext7SearchTool(ctx, apiKey))
	registry.Register(NewContext7DocsTool(ctx, apiKey))
}

// AllContext7Tools returns all Context7 tools
func AllContext7Tools(ctx *Context, apiKey string) []toolInterface {
	return []toolInterface{
		NewContext7SearchTool(ctx, apiKey),
		NewContext7DocsTool(ctx, apiKey),
	}
}

// Context7ToolNames returns the names of all Context7 tools
func Context7ToolNames() []string {
	return []string{
		"context7_search",
		"context7_docs",
	}
}

// Helper function for minimum
func min(a, b int) int {
	if a < b {
		return a
	}
	return b
}
