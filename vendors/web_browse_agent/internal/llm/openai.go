package llm

import (
	"bytes"
	"encoding/json"
	"fmt"
	"io"
	"log"
	"net/http"
	"os"
)

const (
	openAIAPIURL       = "https://api.openai.com/v1/responses"
	defaultOpenAIModel = "gpt-4o"
)

// OpenAIClient implements the Client interface for OpenAI
// using the newer /v1/responses endpoint.
type OpenAIClient struct {
	apiKey  string
	model   string
	baseURL string
	client  *http.Client
	debug   bool
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

	debug := os.Getenv("WEB_BROWSE_AGENT_DEBUG") == "1"

	return &OpenAIClient{
		apiKey:  apiKey,
		model:   model,
		baseURL: baseURL,
		client:  &http.Client{},
		debug:   debug,
	}, nil
}

// GetModel returns the model name
func (c *OpenAIClient) GetModel() string {
	return c.model
}

// GetBaseURL returns the base URL for the OpenAI API
func (c *OpenAIClient) GetBaseURL() string {
	return c.baseURL
}

// Provider returns the provider name
func (c *OpenAIClient) Provider() string {
	return "openai"
}

// OpenAI Responses API request/response types
type responsesRequest struct {
	Model           string             `json:"model"`
	Input           []responsesMessage `json:"input"`
	Tools           []responsesTool    `json:"tools,omitempty"`
	Instructions    string             `json:"instructions,omitempty"`
	MaxOutputTokens int                `json:"max_output_tokens,omitempty"`
}

type responsesMessage struct {
	Type    string             `json:"type"`
	Role    string             `json:"role"`
	Content []responsesContent `json:"content"`
}

type responsesContent struct {
	Type      string `json:"type"`
	Text      string `json:"text,omitempty"`
	ID        string `json:"id,omitempty"`
	Name      string `json:"name,omitempty"`
	Arguments string `json:"arguments,omitempty"`
}

type responsesTool struct {
	Type        string                 `json:"type"`
	Name        string                 `json:"name,omitempty"`
	Description string                 `json:"description,omitempty"`
	Parameters  map[string]interface{} `json:"parameters,omitempty"`
}

type responsesResponse struct {
	Output []responsesMessage `json:"output"`
	Status string             `json:"status,omitempty"`
	Error  *struct {
		Message string `json:"message"`
		Type    string `json:"type,omitempty"`
	} `json:"error,omitempty"`
}

// Chat sends messages to OpenAI and returns the response using the Responses API
func (c *OpenAIClient) Chat(messages []Message, tools []ToolDefinition) (*Response, error) {
	instructions, inputItems, err := convertToResponsesInput(messages)
	if err != nil {
		return nil, err
	}

	// Convert tools to Responses API format
	var responsesTools []responsesTool
	if len(tools) > 0 {
		responsesTools = make([]responsesTool, len(tools))
		for i, tool := range tools {
			responsesTools[i] = responsesTool{
				Type:        "function",
				Name:        tool.Name,
				Description: tool.Description,
				Parameters:  tool.InputSchema,
			}
		}
	}

	// Build request
	reqBody := responsesRequest{
		Model:        c.model,
		Input:        inputItems,
		Tools:        responsesTools,
		Instructions: instructions,
		// We currently rely on model defaults for max tokens
	}

	jsonData, err := json.Marshal(reqBody)
	if err != nil {
		return nil, fmt.Errorf("failed to marshal request: %w", err)
	}

	if c.debug {
		log.Printf("[openai] request body: %s", string(jsonData))
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

	if c.debug {
		log.Printf("[openai] status: %s", resp.Status)
		log.Printf("[openai] raw response: %s", string(body))
	}

	if resp.StatusCode >= 300 {
		return nil, fmt.Errorf("OpenAI API error: status %d: %s", resp.StatusCode, string(body))
	}

	// Parse response
	var responsesResp responsesResponse
	if err := json.Unmarshal(body, &responsesResp); err != nil {
		return nil, fmt.Errorf("failed to parse response: %w", err)
	}

	// Check for API error
	if responsesResp.Error != nil {
		return nil, fmt.Errorf("OpenAI API error: %s", responsesResp.Error.Message)
	}

	if len(responsesResp.Output) == 0 {
		return nil, fmt.Errorf("no output in OpenAI response")
	}

	// Convert response to our format
	response := &Response{
		StopReason: mapStatus(responsesResp.Status),
	}

	// Collect text and tool calls from the assistant messages
	var textContent string
	var toolCalls []ToolCall

	for _, item := range responsesResp.Output {
		for _, content := range item.Content {
			switch content.Type {
			case "output_text":
				textContent += content.Text
			case "function_call":
				// Parse arguments JSON string to map
				var input map[string]interface{}
				if content.Arguments != "" {
					_ = json.Unmarshal([]byte(content.Arguments), &input)
				}
				if input == nil {
					input = make(map[string]interface{})
				}
				toolCalls = append(toolCalls, ToolCall{
					ID:    content.ID,
					Name:  content.Name,
					Input: input,
				})
			}
		}
	}

	response.Content = textContent
	response.ToolCalls = toolCalls

	// If no explicit status, set a default stop reason
	if response.StopReason == "" {
		if len(toolCalls) > 0 {
			response.StopReason = "tool_calls"
		} else {
			response.StopReason = "stop"
		}
	}

	return response, nil
}

// mapStatus converts the Responses API status into the agent stop reason.
func mapStatus(status string) string {
	switch status {
	case "completed", "done", "finished":
		return "stop"
	case "requires_action":
		return "tool_calls"
	case "in_progress":
		return "in_progress"
	case "failed", "expired", "cancelled":
		return status
	default:
		return status
	}
}

// convertToResponsesInput converts our Message list to the Responses API input format.
// Returns the instructions string (from the first system message) and the input array.
func convertToResponsesInput(messages []Message) (string, []responsesMessage, error) {
	var instructions string
	inputItems := make([]responsesMessage, 0, len(messages))

	for _, msg := range messages {
		switch msg.Role {
		case "system":
			if instructions == "" {
				if s, ok := msg.Content.(string); ok {
					instructions = s
				} else if blocks, ok := msg.Content.([]ContentBlock); ok {
					for _, b := range blocks {
						if b.Type == "text" && b.Text != "" {
							instructions = b.Text
							break
						}
					}
				}
			}
			// System messages are not appended to input; instructions covers them
			continue
		case "user":
			contents := buildUserContents(msg.Content)
			if len(contents) == 0 {
				continue
			}
			inputItems = append(inputItems, responsesMessage{
				Type:    "message",
				Role:    "user",
				Content: contents,
			})
		case "assistant":
			contents := buildAssistantContents(msg.Content)
			if len(contents) == 0 {
				continue
			}
			inputItems = append(inputItems, responsesMessage{
				Type:    "message",
				Role:    "assistant",
				Content: contents,
			})
		}
	}

	return instructions, inputItems, nil
}

// buildUserContents converts user content into Responses API content blocks.
func buildUserContents(content interface{}) []responsesContent {
	switch v := content.(type) {
	case string:
		if v == "" {
			return nil
		}
		return []responsesContent{{
			Type: "input_text",
			Text: v,
		}}
	case []ContentBlock:
		blocks := make([]responsesContent, 0, len(v))
		for _, b := range v {
			switch b.Type {
			case "text":
				if b.Text != "" {
					blocks = append(blocks, responsesContent{Type: "input_text", Text: b.Text})
				}
			case "tool_result":
				// Encode tool result as JSON string with call id so the model can associate it
				wrapped := map[string]interface{}{
					"tool_call_id": b.ToolUseID,
					"output":       b.Content,
				}
				wrappedJSON, _ := json.Marshal(wrapped)
				blocks = append(blocks, responsesContent{Type: "input_text", Text: string(wrappedJSON)})
			}
		}
		return blocks
	case []interface{}:
		converted := convertInterfaceBlocks(v)
		return buildUserContents(converted)
	default:
		return nil
	}
}

// buildAssistantContents converts assistant content into Responses API content blocks.
func buildAssistantContents(content interface{}) []responsesContent {
	switch v := content.(type) {
	case string:
		if v == "" {
			return nil
		}
		return []responsesContent{{
			Type: "output_text",
			Text: v,
		}}
	case []ContentBlock:
		blocks := make([]responsesContent, 0, len(v))
		for _, b := range v {
			switch b.Type {
			case "text":
				if b.Text != "" {
					blocks = append(blocks, responsesContent{Type: "output_text", Text: b.Text})
				}
			case "tool_use":
				argsBytes, _ := json.Marshal(b.Input)
				blocks = append(blocks, responsesContent{
					Type:      "function_call",
					ID:        b.ID,
					Name:      b.Name,
					Arguments: string(argsBytes),
				})
			}
		}
		return blocks
	case []interface{}:
		converted := convertInterfaceBlocks(v)
		return buildAssistantContents(converted)
	default:
		return nil
	}
}

// convertInterfaceBlocks converts a raw []interface{} (from JSON) into []ContentBlock
// so we can reuse the typed handlers above.
func convertInterfaceBlocks(raw []interface{}) []ContentBlock {
	blocks := make([]ContentBlock, 0, len(raw))
	for _, item := range raw {
		m, ok := item.(map[string]interface{})
		if !ok {
			continue
		}
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
	return blocks
}
