package llm

import (
	"bytes"
	"encoding/json"
	"fmt"
	"io"
	"net/http"
	"os"
)

const (
	anthropicAPIURL       = "https://api.anthropic.com/v1/messages"
	anthropicVersion      = "2023-06-01"
	defaultAnthropicModel = "claude-sonnet-4-20250514"
)

// AnthropicClient implements the Client interface for Anthropic Claude
type AnthropicClient struct {
	apiKey string
	model  string
	client *http.Client
}

// NewAnthropicClient creates a new Anthropic client
func NewAnthropicClient() (*AnthropicClient, error) {
	apiKey := os.Getenv("ANTHROPIC_API_KEY")
	if apiKey == "" {
		return nil, fmt.Errorf("ANTHROPIC_API_KEY environment variable is not set")
	}

	model := os.Getenv("ANTHROPIC_MODEL")
	if model == "" {
		model = defaultAnthropicModel
	}

	return &AnthropicClient{
		apiKey: apiKey,
		model:  model,
		client: &http.Client{},
	}, nil
}

// GetModel returns the model name
func (c *AnthropicClient) GetModel() string {
	return c.model
}

// Anthropic API request/response types
type anthropicRequest struct {
	Model     string             `json:"model"`
	MaxTokens int                `json:"max_tokens"`
	Messages  []anthropicMessage `json:"messages"`
	Tools     []anthropicTool    `json:"tools,omitempty"`
}

type anthropicMessage struct {
	Role    string      `json:"role"`
	Content interface{} `json:"content"` // string or []anthropicContentBlock
}

type anthropicContentBlock struct {
	Type      string `json:"type"`
	Text      string `json:"text,omitempty"`
	ID        string `json:"id,omitempty"`
	Name      string `json:"name,omitempty"`
	Input     any    `json:"input,omitempty"`
	ToolUseID string `json:"tool_use_id,omitempty"`
	Content   string `json:"content,omitempty"`
}

type anthropicTool struct {
	Name        string                 `json:"name"`
	Description string                 `json:"description"`
	InputSchema map[string]interface{} `json:"input_schema"`
}

type anthropicResponse struct {
	ID         string `json:"id"`
	Type       string `json:"type"`
	Role       string `json:"role"`
	Model      string `json:"model"`
	StopReason string `json:"stop_reason"`
	Content    []struct {
		Type  string `json:"type"`
		Text  string `json:"text,omitempty"`
		ID    string `json:"id,omitempty"`
		Name  string `json:"name,omitempty"`
		Input any    `json:"input,omitempty"`
	} `json:"content"`
	Error *struct {
		Type    string `json:"type"`
		Message string `json:"message"`
	} `json:"error,omitempty"`
}

// Chat sends messages to Anthropic and returns the response
func (c *AnthropicClient) Chat(messages []Message, tools []ToolDefinition) (*Response, error) {
	// Convert messages to Anthropic format
	anthropicMessages := make([]anthropicMessage, 0, len(messages))
	for _, msg := range messages {
		anthropicMsg := convertToAnthropicMessage(msg)
		anthropicMessages = append(anthropicMessages, anthropicMsg)
	}

	// Convert tools to Anthropic format
	var anthropicTools []anthropicTool
	if len(tools) > 0 {
		anthropicTools = make([]anthropicTool, len(tools))
		for i, tool := range tools {
			anthropicTools[i] = anthropicTool{
				Name:        tool.Name,
				Description: tool.Description,
				InputSchema: tool.InputSchema,
			}
		}
	}

	// Build request
	reqBody := anthropicRequest{
		Model:     c.model,
		MaxTokens: 8192,
		Messages:  anthropicMessages,
		Tools:     anthropicTools,
	}

	jsonData, err := json.Marshal(reqBody)
	if err != nil {
		return nil, fmt.Errorf("failed to marshal request: %w", err)
	}

	// Create HTTP request
	req, err := http.NewRequest("POST", anthropicAPIURL, bytes.NewBuffer(jsonData))
	if err != nil {
		return nil, fmt.Errorf("failed to create request: %w", err)
	}

	req.Header.Set("Content-Type", "application/json")
	req.Header.Set("x-api-key", c.apiKey)
	req.Header.Set("anthropic-version", anthropicVersion)

	// Send request
	resp, err := c.client.Do(req)
	if err != nil {
		return nil, fmt.Errorf("failed to send request: %w", err)
	}
	defer resp.Body.Close()

	// Read response
	body, err := io.ReadAll(resp.Body)
	if err != nil {
		return nil, fmt.Errorf("failed to read response: %w", err)
	}

	// Parse response
	var anthropicResp anthropicResponse
	if err := json.Unmarshal(body, &anthropicResp); err != nil {
		return nil, fmt.Errorf("failed to parse response: %w", err)
	}

	// Check for API error
	if anthropicResp.Error != nil {
		return nil, fmt.Errorf("Anthropic API error: %s", anthropicResp.Error.Message)
	}

	// Convert response to our format
	response := &Response{
		StopReason: anthropicResp.StopReason,
	}

	// Extract content and tool calls
	var textContent string
	var toolCalls []ToolCall

	for _, block := range anthropicResp.Content {
		switch block.Type {
		case "text":
			textContent = block.Text
		case "tool_use":
			// Convert input to map[string]interface{}
			var input map[string]interface{}
			if block.Input != nil {
				switch v := block.Input.(type) {
				case map[string]interface{}:
					input = v
				default:
					// Try to convert via JSON
					inputBytes, _ := json.Marshal(block.Input)
					json.Unmarshal(inputBytes, &input)
				}
			}
			if input == nil {
				input = make(map[string]interface{})
			}
			toolCalls = append(toolCalls, ToolCall{
				ID:    block.ID,
				Name:  block.Name,
				Input: input,
			})
		}
	}

	response.Content = textContent
	response.ToolCalls = toolCalls

	return response, nil
}

// convertToAnthropicMessage converts our Message type to Anthropic format
func convertToAnthropicMessage(msg Message) anthropicMessage {
	anthropicMsg := anthropicMessage{
		Role: msg.Role,
	}

	switch content := msg.Content.(type) {
	case string:
		anthropicMsg.Content = content
	case []ContentBlock:
		// Convert ContentBlock to anthropicContentBlock
		blocks := make([]anthropicContentBlock, len(content))
		for i, block := range content {
			blocks[i] = anthropicContentBlock{
				Type:      block.Type,
				Text:      block.Text,
				ID:        block.ID,
				Name:      block.Name,
				Input:     block.Input,
				ToolUseID: block.ToolUseID,
				Content:   block.Content,
			}
		}
		anthropicMsg.Content = blocks
	case []interface{}:
		// Handle raw interface array (from JSON unmarshaling)
		blocks := make([]anthropicContentBlock, 0)
		for _, item := range content {
			if m, ok := item.(map[string]interface{}); ok {
				block := anthropicContentBlock{}
				if t, ok := m["type"].(string); ok {
					block.Type = t
				}
				if t, ok := m["text"].(string); ok {
					block.Text = t
				}
				if t, ok := m["id"].(string); ok {
					block.ID = t
				}
				if t, ok := m["name"].(string); ok {
					block.Name = t
				}
				if t, ok := m["input"]; ok {
					block.Input = t
				}
				if t, ok := m["tool_use_id"].(string); ok {
					block.ToolUseID = t
				}
				if t, ok := m["content"].(string); ok {
					block.Content = t
				}
				blocks = append(blocks, block)
			}
		}
		anthropicMsg.Content = blocks
	}

	return anthropicMsg
}
