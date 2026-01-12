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
	openAIAPIURL     = "https://api.openai.com/v1/chat/completions"
	defaultOpenAIModel = "gpt-4o"
)

// OpenAIClient implements the Client interface for OpenAI
type OpenAIClient struct {
	apiKey  string
	model   string
	baseURL string
	client  *http.Client
}

// NewOpenAIClient creates a new OpenAI client
func NewOpenAIClient() (*OpenAIClient, error) {
	apiKey := os.Getenv("OPENAI_API_KEY")
	if apiKey == "" {
		return nil, fmt.Errorf("OPENAI_API_KEY environment variable is not set")
	}

	model := os.Getenv("OPENAI_MODEL")
	if model == "" {
		model = defaultOpenAIModel
	}

	baseURL := os.Getenv("OPENAI_API_BASE")
	if baseURL == "" {
		baseURL = openAIAPIURL
	}

	return &OpenAIClient{
		apiKey:  apiKey,
		model:   model,
		baseURL: baseURL,
		client:  &http.Client{},
	}, nil
}

// GetModel returns the model name
func (c *OpenAIClient) GetModel() string {
	return c.model
}

// OpenAI API request/response types
type openAIRequest struct {
	Model    string          `json:"model"`
	Messages []openAIMessage `json:"messages"`
	Tools    []openAITool    `json:"tools,omitempty"`
}

type openAIMessage struct {
	Role       string           `json:"role"`
	Content    interface{}      `json:"content,omitempty"`
	ToolCalls  []openAIToolCall `json:"tool_calls,omitempty"`
	ToolCallID string           `json:"tool_call_id,omitempty"`
}

type openAITool struct {
	Type     string         `json:"type"`
	Function openAIFunction `json:"function"`
}

type openAIFunction struct {
	Name        string                 `json:"name"`
	Description string                 `json:"description"`
	Parameters  map[string]interface{} `json:"parameters"`
}

type openAIToolCall struct {
	ID       string `json:"id"`
	Type     string `json:"type"`
	Function struct {
		Name      string `json:"name"`
		Arguments string `json:"arguments"`
	} `json:"function"`
}

type openAIResponse struct {
	ID      string `json:"id"`
	Object  string `json:"object"`
	Created int64  `json:"created"`
	Model   string `json:"model"`
	Choices []struct {
		Index        int           `json:"index"`
		Message      openAIMessage `json:"message"`
		FinishReason string        `json:"finish_reason"`
	} `json:"choices"`
	Error *struct {
		Message string `json:"message"`
		Type    string `json:"type"`
	} `json:"error,omitempty"`
}

// Chat sends messages to OpenAI and returns the response
func (c *OpenAIClient) Chat(messages []Message, tools []ToolDefinition) (*Response, error) {
	// Convert messages to OpenAI format
	openAIMessages := make([]openAIMessage, 0, len(messages))
	for _, msg := range messages {
		openAIMsg := convertToOpenAIMessage(msg)
		openAIMessages = append(openAIMessages, openAIMsg)
	}

	// Convert tools to OpenAI format
	var openAITools []openAITool
	if len(tools) > 0 {
		openAITools = make([]openAITool, len(tools))
		for i, tool := range tools {
			openAITools[i] = openAITool{
				Type: "function",
				Function: openAIFunction{
					Name:        tool.Name,
					Description: tool.Description,
					Parameters:  tool.InputSchema,
				},
			}
		}
	}

	// Build request
	reqBody := openAIRequest{
		Model:    c.model,
		Messages: openAIMessages,
		Tools:    openAITools,
	}

	jsonData, err := json.Marshal(reqBody)
	if err != nil {
		return nil, fmt.Errorf("failed to marshal request: %w", err)
	}

	// Create HTTP request
	req, err := http.NewRequest("POST", c.baseURL, bytes.NewBuffer(jsonData))
	if err != nil {
		return nil, fmt.Errorf("failed to create request: %w", err)
	}

	req.Header.Set("Content-Type", "application/json")
	req.Header.Set("Authorization", "Bearer "+c.apiKey)

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
	var openAIResp openAIResponse
	if err := json.Unmarshal(body, &openAIResp); err != nil {
		return nil, fmt.Errorf("failed to parse response: %w", err)
	}

	// Check for API error
	if openAIResp.Error != nil {
		return nil, fmt.Errorf("OpenAI API error: %s", openAIResp.Error.Message)
	}

	if len(openAIResp.Choices) == 0 {
		return nil, fmt.Errorf("no choices in OpenAI response")
	}

	// Convert response to our format
	choice := openAIResp.Choices[0]
	response := &Response{
		StopReason: choice.FinishReason,
	}

	// Extract content
	if choice.Message.Content != nil {
		switch v := choice.Message.Content.(type) {
		case string:
			response.Content = v
		}
	}

	// Extract tool calls
	if len(choice.Message.ToolCalls) > 0 {
		response.ToolCalls = make([]ToolCall, len(choice.Message.ToolCalls))
		for i, tc := range choice.Message.ToolCalls {
			var input map[string]interface{}
			if err := json.Unmarshal([]byte(tc.Function.Arguments), &input); err != nil {
				input = make(map[string]interface{})
			}
			response.ToolCalls[i] = ToolCall{
				ID:    tc.ID,
				Name:  tc.Function.Name,
				Input: input,
			}
		}
	}

	return response, nil
}

// convertToOpenAIMessage converts our Message type to OpenAI format
func convertToOpenAIMessage(msg Message) openAIMessage {
	openAIMsg := openAIMessage{
		Role: msg.Role,
	}

	switch content := msg.Content.(type) {
	case string:
		openAIMsg.Content = content
	case []ContentBlock:
		// Handle content blocks - check for tool results
		for _, block := range content {
			if block.Type == "tool_result" {
				openAIMsg.Role = "tool"
				openAIMsg.ToolCallID = block.ToolUseID
				openAIMsg.Content = block.Content
				return openAIMsg
			}
		}
		// For assistant messages with tool_use blocks
		var textContent string
		var toolCalls []openAIToolCall
		for _, block := range content {
			switch block.Type {
			case "text":
				textContent = block.Text
			case "tool_use":
				inputJSON, _ := json.Marshal(block.Input)
				toolCalls = append(toolCalls, openAIToolCall{
					ID:   block.ID,
					Type: "function",
					Function: struct {
						Name      string `json:"name"`
						Arguments string `json:"arguments"`
					}{
						Name:      block.Name,
						Arguments: string(inputJSON),
					},
				})
			}
		}
		if len(toolCalls) > 0 {
			openAIMsg.ToolCalls = toolCalls
			if textContent != "" {
				openAIMsg.Content = textContent
			}
		} else if textContent != "" {
			openAIMsg.Content = textContent
		}
	case []interface{}:
		// Handle raw interface array (from JSON unmarshaling)
		blocks := make([]ContentBlock, 0)
		for _, item := range content {
			if m, ok := item.(map[string]interface{}); ok {
				block := ContentBlock{}
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
		// Recursively convert with typed blocks
		return convertToOpenAIMessage(Message{Role: msg.Role, Content: blocks})
	}

	return openAIMsg
}
