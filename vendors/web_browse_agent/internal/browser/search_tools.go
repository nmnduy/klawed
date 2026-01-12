package browser

import (
	"encoding/json"
	"fmt"
	"net/url"
	"time"
)

// SearchResult represents a single search result
type SearchResult struct {
	Title   string `json:"title"`
	URL     string `json:"url"`
	Snippet string `json:"snippet"`
}

// ============================================================================
// DuckDuckGoSearchTool - Search the web using DuckDuckGo
// ============================================================================

type DuckDuckGoSearchTool struct {
	ctx *Context
}

func NewDuckDuckGoSearchTool(ctx *Context) *DuckDuckGoSearchTool {
	return &DuckDuckGoSearchTool{ctx: ctx}
}

func (t *DuckDuckGoSearchTool) Name() string {
	return "web_search"
}

func (t *DuckDuckGoSearchTool) Description() string {
	return `Search the web using DuckDuckGo. Returns a list of search results with titles, URLs, and snippets.
Use this to find relevant web pages, then use browser_navigate to visit specific results.
Best practices:
- Use specific, targeted search queries
- Review snippets to identify the most relevant results
- Navigate to promising results for detailed information`
}

func (t *DuckDuckGoSearchTool) ParametersSchema() map[string]interface{} {
	return map[string]interface{}{
		"type": "object",
		"properties": map[string]interface{}{
			"query": map[string]interface{}{
				"type":        "string",
				"description": "The search query",
			},
			"max_results": map[string]interface{}{
				"type":        "integer",
				"description": "Maximum number of results to return (default: 10, max: 30)",
			},
		},
		"required": []string{"query"},
	}
}

func (t *DuckDuckGoSearchTool) Execute(params map[string]interface{}) (string, error) {
	query := getStringParam(params, "query", "")
	if query == "" {
		return "", fmt.Errorf("query parameter is required")
	}

	maxResults := int(getFloatParam(params, "max_results", 10))
	if maxResults <= 0 {
		maxResults = 10
	}
	if maxResults > 30 {
		maxResults = 30
	}

	page, err := t.ctx.EnsureActivePage()
	if err != nil {
		return "", fmt.Errorf("failed to get browser page: %w", err)
	}

	// Navigate to DuckDuckGo with the search query
	searchURL := fmt.Sprintf("https://duckduckgo.com/?q=%s&t=h_&ia=web", url.QueryEscape(query))
	if _, err := page.Goto(searchURL); err != nil {
		return "", fmt.Errorf("failed to navigate to DuckDuckGo: %w", err)
	}

	// Wait for results to load
	if err := page.Locator("[data-testid='result']").First().WaitFor(); err != nil {
		// Try alternative selector for results
		time.Sleep(2 * time.Second) // Give it time to load
	}

	// Extract search results using JavaScript
	results, err := page.Evaluate(`() => {
		const results = [];
		// Try multiple selectors for DuckDuckGo results
		const resultElements = document.querySelectorAll('[data-testid="result"], .result, .results_links_deep, article[data-nrn="result"]');
		
		for (const el of resultElements) {
			if (results.length >= ` + fmt.Sprintf("%d", maxResults) + `) break;
			
			// Try to find the title and URL
			const titleEl = el.querySelector('h2 a, a[data-testid="result-title-a"], .result__title a, .result__a');
			const snippetEl = el.querySelector('[data-result="snippet"], .result__snippet, .result__body');
			
			if (titleEl) {
				const title = titleEl.textContent?.trim() || '';
				const url = titleEl.href || '';
				const snippet = snippetEl?.textContent?.trim() || '';
				
				// Skip DuckDuckGo internal links
				if (url && !url.includes('duckduckgo.com') && title) {
					results.push({ title, url, snippet });
				}
			}
		}
		
		return results;
	}`)

	if err != nil {
		return "", fmt.Errorf("failed to extract search results: %w", err)
	}

	// Convert to SearchResult slice
	var searchResults []SearchResult
	if resultsArray, ok := results.([]interface{}); ok {
		for _, r := range resultsArray {
			if rMap, ok := r.(map[string]interface{}); ok {
				searchResults = append(searchResults, SearchResult{
					Title:   fmt.Sprintf("%v", rMap["title"]),
					URL:     fmt.Sprintf("%v", rMap["url"]),
					Snippet: fmt.Sprintf("%v", rMap["snippet"]),
				})
			}
		}
	}

	if len(searchResults) == 0 {
		return "No search results found. Try a different query or check if there's a CAPTCHA.", nil
	}

	// Format results as JSON
	output, err := json.MarshalIndent(searchResults, "", "  ")
	if err != nil {
		return "", fmt.Errorf("failed to format results: %w", err)
	}

	return fmt.Sprintf("Found %d results:\n%s", len(searchResults), string(output)), nil
}

// ============================================================================
// GetPageContentTool - Extract main content from the current page
// ============================================================================

type GetPageContentTool struct {
	ctx *Context
}

func NewGetPageContentTool(ctx *Context) *GetPageContentTool {
	return &GetPageContentTool{ctx: ctx}
}

func (t *GetPageContentTool) Name() string {
	return "get_page_content"
}

func (t *GetPageContentTool) Description() string {
	return `Extract the main text content from the current page. 
This removes navigation, ads, and other non-content elements to give you the article/main content.
Useful for reading articles, documentation, or any text-heavy page.
Use browser_snapshot for a full accessibility tree view instead.`
}

func (t *GetPageContentTool) ParametersSchema() map[string]interface{} {
	return map[string]interface{}{
		"type": "object",
		"properties": map[string]interface{}{
			"selector": map[string]interface{}{
				"type":        "string",
				"description": "Optional: CSS selector to extract content from a specific element. If not provided, attempts to find main content automatically.",
			},
			"max_length": map[string]interface{}{
				"type":        "integer",
				"description": "Maximum character length of content to return (default: 50000)",
			},
			"include_links": map[string]interface{}{
				"type":        "boolean",
				"description": "Include links in markdown format (default: false)",
			},
		},
	}
}

func (t *GetPageContentTool) Execute(params map[string]interface{}) (string, error) {
	selector := getStringParam(params, "selector", "")
	maxLength := int(getFloatParam(params, "max_length", 50000))
	includeLinks := getBoolParam(params, "include_links", false)

	page, err := t.ctx.ActivePage()
	if err != nil {
		return "", err
	}

	// Get the page title and URL for citation
	title, _ := page.Title()
	pageURL := page.URL()

	// Use string concatenation to avoid backtick issues in raw strings
	backtick := "`"
	tripleBacktick := "```"

	// JavaScript to extract content
	script := fmt.Sprintf(`(() => {
		const selector = %q;
		const includeLinks = %v;
		const backtick = %q;
		const tripleBacktick = %q;
		
		// Find the main content element
		let contentEl;
		if (selector) {
			contentEl = document.querySelector(selector);
		} else {
			// Try common content selectors in order of preference
			const contentSelectors = [
				'main',
				'article',
				'[role="main"]',
				'.content',
				'.post-content',
				'.article-content',
				'.entry-content',
				'#content',
				'.markdown-body',
				'.documentation',
				'.docs-content'
			];
			
			for (const sel of contentSelectors) {
				contentEl = document.querySelector(sel);
				if (contentEl) break;
			}
			
			// Fallback to body
			if (!contentEl) {
				contentEl = document.body;
			}
		}
		
		if (!contentEl) {
			return { error: 'No content found' };
		}
		
		// Clone to avoid modifying the page
		const clone = contentEl.cloneNode(true);
		
		// Remove unwanted elements
		const removeSelectors = [
			'script', 'style', 'noscript', 'iframe',
			'nav', 'header', 'footer', 'aside',
			'.sidebar', '.navigation', '.menu', '.ad', '.advertisement',
			'.social-share', '.comments', '.related-posts',
			'[role="navigation"]', '[role="banner"]', '[role="complementary"]'
		];
		
		for (const sel of removeSelectors) {
			clone.querySelectorAll(sel).forEach(el => el.remove());
		}
		
		// Extract text with optional link formatting
		function extractText(element, depth = 0) {
			let text = '';
			
			for (const node of element.childNodes) {
				if (node.nodeType === Node.TEXT_NODE) {
					text += node.textContent;
				} else if (node.nodeType === Node.ELEMENT_NODE) {
					const tag = node.tagName.toLowerCase();
					
					// Handle block elements
					if (['p', 'div', 'section', 'article', 'h1', 'h2', 'h3', 'h4', 'h5', 'h6', 'li', 'br'].includes(tag)) {
						text += '\n';
					}
					
					// Handle headers
					if (tag.match(/^h[1-6]$/)) {
						const level = parseInt(tag[1]);
						text += '#'.repeat(level) + ' ';
					}
					
					// Handle links
					if (tag === 'a' && includeLinks) {
						const href = node.getAttribute('href');
						const linkText = extractText(node, depth + 1);
						if (href && linkText.trim()) {
							text += '[' + linkText.trim() + '](' + href + ')';
							continue;
						}
					}
					
					// Handle code blocks
					if (tag === 'pre' || tag === 'code') {
						const codeText = node.textContent;
						if (tag === 'pre') {
							text += '\n' + tripleBacktick + '\n' + codeText + '\n' + tripleBacktick + '\n';
							continue;
						} else {
							text += backtick + codeText + backtick;
							continue;
						}
					}
					
					// Handle list items
					if (tag === 'li') {
						text += '• ';
					}
					
					text += extractText(node, depth + 1);
					
					if (['p', 'div', 'section', 'article', 'h1', 'h2', 'h3', 'h4', 'h5', 'h6', 'li'].includes(tag)) {
						text += '\n';
					}
				}
			}
			
			return text;
		}
		
		const content = extractText(clone);
		
		// Clean up whitespace
		const cleaned = content
			.replace(/\n{3,}/g, '\n\n')
			.replace(/[ \t]+/g, ' ')
			.trim();
		
		return { content: cleaned };
	})()`, selector, includeLinks, backtick, tripleBacktick)

	result, err := page.Evaluate(script)
	if err != nil {
		return "", fmt.Errorf("failed to extract content: %w", err)
	}

	resultMap, ok := result.(map[string]interface{})
	if !ok {
		return "", fmt.Errorf("unexpected result format")
	}

	if errMsg, hasError := resultMap["error"]; hasError {
		return "", fmt.Errorf("%v", errMsg)
	}

	content, ok := resultMap["content"].(string)
	if !ok {
		return "", fmt.Errorf("no content extracted")
	}

	// Truncate if needed
	if len(content) > maxLength {
		content = content[:maxLength] + "\n\n[Content truncated...]"
	}

	// Add citation header
	header := fmt.Sprintf("# %s\nSource: %s\n\n", title, pageURL)
	return header + content, nil
}

// ============================================================================
// GetLinksFromPageTool - Extract all links from the current page
// ============================================================================

type GetLinksFromPageTool struct {
	ctx *Context
}

func NewGetLinksFromPageTool(ctx *Context) *GetLinksFromPageTool {
	return &GetLinksFromPageTool{ctx: ctx}
}

func (t *GetLinksFromPageTool) Name() string {
	return "get_page_links"
}

func (t *GetLinksFromPageTool) Description() string {
	return `Extract all links from the current page. Useful for discovering related pages, navigation, or resources.
Returns links with their text and URLs.`
}

func (t *GetLinksFromPageTool) ParametersSchema() map[string]interface{} {
	return map[string]interface{}{
		"type": "object",
		"properties": map[string]interface{}{
			"selector": map[string]interface{}{
				"type":        "string",
				"description": "Optional: CSS selector to limit link extraction to a specific area",
			},
			"filter": map[string]interface{}{
				"type":        "string",
				"description": "Optional: Filter links by text or URL containing this string (case-insensitive)",
			},
			"max_results": map[string]interface{}{
				"type":        "integer",
				"description": "Maximum number of links to return (default: 50)",
			},
		},
	}
}

func (t *GetLinksFromPageTool) Execute(params map[string]interface{}) (string, error) {
	selector := getStringParam(params, "selector", "")
	filter := getStringParam(params, "filter", "")
	maxResults := int(getFloatParam(params, "max_results", 50))

	page, err := t.ctx.ActivePage()
	if err != nil {
		return "", err
	}

	script := fmt.Sprintf(`() => {
		const selector = %q;
		const filter = %q.toLowerCase();
		const maxResults = %d;
		
		const container = selector ? document.querySelector(selector) : document;
		if (!container) return [];
		
		const links = [];
		const seen = new Set();
		
		for (const a of container.querySelectorAll('a[href]')) {
			if (links.length >= maxResults) break;
			
			const href = a.href;
			const text = a.textContent?.trim() || '';
			
			// Skip empty, anchors, and duplicates
			if (!href || href.startsWith('javascript:') || href === '#' || seen.has(href)) {
				continue;
			}
			
			// Apply filter if provided
			if (filter && !text.toLowerCase().includes(filter) && !href.toLowerCase().includes(filter)) {
				continue;
			}
			
			seen.add(href);
			links.push({ text: text.slice(0, 100), url: href });
		}
		
		return links;
	}`, selector, filter, maxResults)

	result, err := page.Evaluate(script)
	if err != nil {
		return "", fmt.Errorf("failed to extract links: %w", err)
	}

	// Format as JSON
	output, err := json.MarshalIndent(result, "", "  ")
	if err != nil {
		return "", fmt.Errorf("failed to format links: %w", err)
	}

	return string(output), nil
}

// Ensure search tools implement the Tool interface
var (
	_ toolInterface = (*DuckDuckGoSearchTool)(nil)
	_ toolInterface = (*GetPageContentTool)(nil)
	_ toolInterface = (*GetLinksFromPageTool)(nil)
)

// RegisterSearchTools registers all search-related tools
func RegisterSearchTools(registry interface{ Register(tool toolInterface) }, ctx *Context) {
	registry.Register(NewDuckDuckGoSearchTool(ctx))
	registry.Register(NewGetPageContentTool(ctx))
	registry.Register(NewGetLinksFromPageTool(ctx))
}

// AllSearchTools returns all search tools for a given context
func AllSearchTools(ctx *Context) []toolInterface {
	return []toolInterface{
		NewDuckDuckGoSearchTool(ctx),
		NewGetPageContentTool(ctx),
		NewGetLinksFromPageTool(ctx),
	}
}

// SearchToolNames returns the names of all search tools
func SearchToolNames() []string {
	return []string{
		"web_search",
		"get_page_content",
		"get_page_links",
	}
}
