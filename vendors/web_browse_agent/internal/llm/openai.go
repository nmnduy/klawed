package llm

import (
	"bytes"
	"encoding/json"
	"fmt"
	"io"
	"log"
	"net/http"
	"os"
	"strings"
	"time"
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
	
	// If using a non-OpenAI provider that likely uses chat completions API,
	// ensure we have the correct endpoint path
	if !strings.Contains(baseURL, "openai.com") {
		// Check if we need to append /v1/chat/completions
		if !strings.Contains(baseURL, "/v1/") {
			// Try to detect what endpoint to use
			if strings.Contains(baseURL, "deepseek.com") {
				baseURL = strings.TrimSuffix(baseURL, "/") + "/v1/chat/completions"
			} else if strings.Contains(baseURL, "openrouter.ai") {
				baseURL = strings.TrimSuffix(baseURL, "/") + "/api/v1/chat/completions"
			}
		}
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

// isChatCompletionsAPI returns true if the endpoint uses Chat Completions format
// (like OpenRouter, Together AI, etc.) instead of the Responses API format
func (c *OpenAIClient) isChatCompletionsAPI() bool {
	// OpenRouter and similar proxies use /chat/completions endpoint
	if strings.Contains(c.baseURL, "chat/completions") {
		return true
	}
	// OpenRouter explicitly uses chat completions format
	if strings.Contains(c.baseURL, "openrouter.ai") {
		return true
	}
	// DeepSeek uses chat completions format
	if strings.Contains(c.baseURL, "deepseek.com") {
		return true
	}
	// Check if this is likely a chat completions endpoint (not responses)
	// Responses API is specific to OpenAI's newer API
	if !strings.Contains(c.baseURL, "openai.com/v1/responses") &&
	   !strings.Contains(c.baseURL, "responses") {
		// If it's not explicitly the responses endpoint, assume chat completions
		return true
	}
	return false
}

type responsesResponse struct {
	Output []responsesMessage `json:"output"`
	Status string             `json:"status,omitempty"`
	Error  *struct {
		Message string `json:"message"`
		Type    string `json:"type,omitempty"`
	} `json:"error,omitempty"`
}

// Chat Completions API types
type chatCompletionRequest struct {
	Model    string                  `json:"model"`
	Messages []chatCompletionMessage `json:"messages"`
	Tools    []chatCompletionTool    `json:"tools,omitempty"`
}

type chatCompletionMessage struct {
	Role     string                   `json:"role"`
	Content  interface{}              `json:"content"` // string or array
	ToolCalls []chatCompletionToolCall `json:"tool_calls,omitempty"`
}

type chatCompletionToolCall struct {
	ID       string                     `json:"id"`
	Type     string                     `json:"type"`
	Function chatCompletionToolFunction `json:"function"`
	Index    int                        `json:"index,omitempty"`
}

type chatCompletionTool struct {
	Type     string                     `json:"type"`
	Function chatCompletionToolFunction `json:"function"`
}

type chatCompletionToolFunction struct {
	Name        string                 `json:"name"`
	Description string                 `json:"description,omitempty"`
	Parameters  map[string]interface{} `json:"parameters"`
	Arguments   string                 `json:"arguments,omitempty"`
}

type chatCompletionResponse struct {
	ID      string `json:"id"`
	Object  string `json:"object"`
	Created int64  `json:"created"`
	Model   string `json:"model"`
	Choices []struct {
		Index        int                   `json:"index"`
		Message      chatCompletionMessage `json:"message"`
		FinishReason string                `json:"finish_reason"`
	} `json:"choices"`
	Usage struct {
		PromptTokens     int `json:"prompt_tokens"`
		CompletionTokens int `json:"completion_tokens"`
		TotalTokens      int `json:"total_tokens"`
	} `json:"usage"`
	Error *struct {
		Message string `json:"message"`
		Type    string `json:"type,omitempty"`
		Code    string `json:"code,omitempty"`
	} `json:"error,omitempty"`
}

// Chat sends messages to OpenAI and returns the response using the appropriate API format
func (c *OpenAIClient) Chat(messages []Message, tools []ToolDefinition) (*Response, error) {
	if c.isChatCompletionsAPI() {
		return c.chatCompletions(messages, tools)
	}
	return c.responsesAPI(messages, tools)
}

// responsesAPI sends messages using the Responses API format
func (c *OpenAIClient) responsesAPI(messages []Message, tools []ToolDefinition) (*Response, error) {
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
		log.Printf("[openai] request body (formatted):\n%s", formatJSONStruct(reqBody))
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
		log.Printf("[openai] raw response (formatted):\n%s", formatJSONBytes(body))
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

// chatCompletions sends messages using the Chat Completions API format
func (c *OpenAIClient) chatCompletions(messages []Message, tools []ToolDefinition) (*Response, error) {
	// Convert messages to chat completions format
	chatMessages, err := convertToChatCompletionsMessages(messages)
	if err != nil {
		return nil, err
	}

	// Convert tools to chat completions format
	var chatTools []chatCompletionTool
	if len(tools) > 0 {
		chatTools = make([]chatCompletionTool, len(tools))
		for i, tool := range tools {
			chatTools[i] = chatCompletionTool{
				Type: "function",
				Function: chatCompletionToolFunction{
					Name:        tool.Name,
					Description: tool.Description,
					Parameters:  tool.InputSchema,
				},
			}
		}
	}

	// Build request
	reqBody := chatCompletionRequest{
		Model:    c.model,
		Messages: chatMessages,
		Tools:    chatTools,
	}

	jsonData, err := json.Marshal(reqBody)
	if err != nil {
		return nil, fmt.Errorf("failed to marshal request: %w", err)
	}

	if c.debug {
		log.Printf("[openai] request body (formatted):\n%s", formatJSONStruct(reqBody))
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
		log.Printf("[openai] raw response (formatted):\n%s", formatJSONBytes(body))
	}

	if resp.StatusCode >= 300 {
		return nil, fmt.Errorf("OpenAI API error: status %d: %s", resp.StatusCode, string(body))
	}

	// Parse response
	var chatResp chatCompletionResponse
	if err := json.Unmarshal(body, &chatResp); err != nil {
		return nil, fmt.Errorf("failed to parse response: %w", err)
	}

	// Check for API error
	if chatResp.Error != nil {
		return nil, fmt.Errorf("OpenAI API error: %s", chatResp.Error.Message)
	}

	if len(chatResp.Choices) == 0 {
		return nil, fmt.Errorf("no choices in OpenAI response")
	}

	choice := chatResp.Choices[0]
	response := &Response{
		StopReason: choice.FinishReason,
	}

	// Extract content and tool calls from the message
	response.Content, response.ToolCalls = extractFromChatCompletionMessage(choice.Message)

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

// convertToChatCompletionsMessages converts our Message list to the Chat Completions API format.
func convertToChatCompletionsMessages(messages []Message) ([]chatCompletionMessage, error) {
	chatMessages := make([]chatCompletionMessage, 0, len(messages))
	
	for _, msg := range messages {
		// Include system messages as regular messages with role "system"
		chatMsg := chatCompletionMessage{
			Role: msg.Role,
		}
		
		// Convert content
		switch content := msg.Content.(type) {
		case string:
			chatMsg.Content = content
		case []ContentBlock:
			// For chat completions, we need to convert tool results and tool calls
			converted, err := convertContentBlocksForChatCompletions(content, msg.Role)
			if err != nil {
				return nil, err
			}
			chatMsg.Content = converted
		case []interface{}:
			// Convert from raw JSON
			blocks := convertInterfaceBlocks(content)
			converted, err := convertContentBlocksForChatCompletions(blocks, msg.Role)
			if err != nil {
				return nil, err
			}
			chatMsg.Content = converted
		default:
			return nil, fmt.Errorf("unsupported content type: %T", content)
		}
		
		chatMessages = append(chatMessages, chatMsg)
	}
	
	return chatMessages, nil
}

// convertContentBlocksForChatCompletions converts ContentBlock slices to chat completions format.
func convertContentBlocksForChatCompletions(blocks []ContentBlock, role string) (interface{}, error) {
	if len(blocks) == 0 {
		return "", nil
	}
	
	// If there's only one text block, return it as a string
	if len(blocks) == 1 && blocks[0].Type == "text" {
		return blocks[0].Text, nil
	}
	
	// Check if we're talking to DeepSeek - it has stricter format requirements
	// For DeepSeek, we need to convert tool calls and tool results to text
	baseURL := os.Getenv("OPENAI_API_BASE")
	if strings.Contains(baseURL, "deepseek.com") {
		// For DeepSeek, convert everything to text
		var textParts []string
		for _, block := range blocks {
			switch block.Type {
			case "text":
				textParts = append(textParts, block.Text)
			case "tool_use":
				argsBytes, _ := json.Marshal(block.Input)
				textParts = append(textParts, fmt.Sprintf("[Called tool: %s with args: %s]", block.Name, string(argsBytes)))
			case "tool_result":
				textParts = append(textParts, fmt.Sprintf("[Tool result: %s]", block.Content))
			}
		}
		return strings.Join(textParts, "\n"), nil
	}
	
	// Otherwise, build an array of content blocks for standard OpenAI-compatible APIs
	result := make([]map[string]interface{}, 0, len(blocks))
	
	for _, block := range blocks {
		switch block.Type {
		case "text":
			result = append(result, map[string]interface{}{
				"type": "text",
				"text": block.Text,
			})
		case "tool_use":
			if role == "assistant" {
				argsBytes, _ := json.Marshal(block.Input)
				result = append(result, map[string]interface{}{
					"type": "tool_call",
					"id":   block.ID,
					"function": map[string]interface{}{
						"name":      block.Name,
						"arguments": string(argsBytes),
					},
				})
			}
		case "tool_result":
			if role == "user" {
				result = append(result, map[string]interface{}{
					"type": "tool_result",
					"tool_call_id": block.ToolUseID,
					"content": block.Content,
				})
			}
		}
	}
	
	return result, nil
}

// extractFromChatCompletionMessage extracts content and tool calls from a chat completion message.
func extractFromChatCompletionMessage(msg chatCompletionMessage) (string, []ToolCall) {
	var textContent string
	var toolCalls []ToolCall
	
	// First, extract text content from the Content field
	switch content := msg.Content.(type) {
	case string:
		textContent = content
		// Check for tool calls in text format [Called tool: ...] (DeepSeek format)
		toolCalls = append(toolCalls, extractToolCallsFromText(textContent)...)
	case []interface{}:
		for _, item := range content {
			if m, ok := item.(map[string]interface{}); ok {
				if typ, _ := m["type"].(string); typ == "text" {
					if text, ok := m["text"].(string); ok {
						textContent += text
						// Check for tool calls in text format [Called tool: ...] (DeepSeek format)
						toolCalls = append(toolCalls, extractToolCallsFromText(text)...)
					}
				} else if typ == "tool_call" {
					// This is for when tool calls are embedded in content array
					if funcObj, ok := m["function"].(map[string]interface{}); ok {
						name, _ := funcObj["name"].(string)
						argsStr, _ := funcObj["arguments"].(string)
						
						var input map[string]interface{}
						if argsStr != "" {
							_ = json.Unmarshal([]byte(argsStr), &input)
						}
						if input == nil {
							input = make(map[string]interface{})
						}
						
						id, _ := m["id"].(string)
						toolCalls = append(toolCalls, ToolCall{
							ID:    id,
							Name:  name,
							Input: input,
						})
					}
				}
			}
		}
	}
	
	// Also check for tool calls in the ToolCalls field (OpenAI format)
	for _, tc := range msg.ToolCalls {
		var input map[string]interface{}
		if tc.Function.Arguments != "" {
			_ = json.Unmarshal([]byte(tc.Function.Arguments), &input)
		}
		if input == nil {
			input = make(map[string]interface{})
		}
		
		toolCalls = append(toolCalls, ToolCall{
			ID:    tc.ID,
			Name:  tc.Function.Name,
			Input: input,
		})
	}
	
	return textContent, toolCalls
}

// extractToolCallsFromText extracts tool calls from text in the format [Called tool: name with args: {...}]
func extractToolCallsFromText(text string) []ToolCall {
	var toolCalls []ToolCall
	
	// Look for patterns like [Called tool: bash with args: {"command":"find ..."}]
	// or [Called tool: read_file with args: {"path":"./semrush_search_notes.md"}]
	lines := strings.Split(text, "\n")
	for _, line := range lines {
		line = strings.TrimSpace(line)
		if strings.HasPrefix(line, "[Called tool: ") && strings.Contains(line, " with args: ") {
			// Extract tool name and arguments
			// Format: [Called tool: NAME with args: {JSON}]
			prefix := "[Called tool: "
			suffix := " with args: "
			nameStart := len(prefix)
			nameEnd := strings.Index(line, suffix)
			if nameEnd > nameStart {
				toolName := line[nameStart:nameEnd]
				argsStart := nameEnd + len(suffix)
				argsStr := line[argsStart:]
				argsStr = strings.TrimSuffix(argsStr, "]")
				
				// Parse JSON arguments
				var input map[string]interface{}
				if argsStr != "" {
					_ = json.Unmarshal([]byte(argsStr), &input)
				}
				if input == nil {
					input = make(map[string]interface{})
				}
				
				// Generate a unique ID
				id := fmt.Sprintf("call_%s_%d", toolName, time.Now().UnixNano())
				
				toolCalls = append(toolCalls, ToolCall{
					ID:    id,
					Name:  toolName,
					Input: input,
				})
			}
		}
	}
	
	return toolCalls
}
