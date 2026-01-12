package llm

// Message represents a chat message
type Message struct {
	Role    string      `json:"role"`
	Content interface{} `json:"content"` // can be string or []ContentBlock
}

// ContentBlock for multimodal content
type ContentBlock struct {
	Type      string `json:"type"`
	Text      string `json:"text,omitempty"`
	ID        string `json:"id,omitempty"`
	Name      string `json:"name,omitempty"`
	Input     any    `json:"input,omitempty"`
	ToolUseID string `json:"tool_use_id,omitempty"`
	Content   string `json:"content,omitempty"`
}

// ToolCall represents a tool call from the LLM
type ToolCall struct {
	ID    string
	Name  string
	Input map[string]interface{}
}

// Response from LLM
type Response struct {
	Content    string
	ToolCalls  []ToolCall
	StopReason string
}

// ToolDefinition for LLM
type ToolDefinition struct {
	Name        string                 `json:"name"`
	Description string                 `json:"description"`
	InputSchema map[string]interface{} `json:"input_schema"`
}

// Client interface for LLM providers
type Client interface {
	Chat(messages []Message, tools []ToolDefinition) (*Response, error)
	GetModel() string
}
