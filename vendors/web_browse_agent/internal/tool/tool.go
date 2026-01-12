package tool

import "encoding/json"

// Tool defines the interface for all tools in the agent
type Tool interface {
	// Name returns the unique identifier for this tool
	Name() string
	// Description returns a human-readable description of what this tool does
	Description() string
	// ParametersSchema returns the JSON schema for the tool's parameters
	ParametersSchema() map[string]interface{}
	// Execute runs the tool with the given parameters and returns the result
	Execute(params map[string]interface{}) (string, error)
}

// Result represents the output from a tool execution
type Result struct {
	Success bool   `json:"success"`
	Output  string `json:"output"`
	Error   string `json:"error,omitempty"`
}

// Success creates a successful result
func Success(output string) *Result {
	return &Result{
		Success: true,
		Output:  output,
	}
}

// Error creates an error result
func Error(msg string) *Result {
	return &Result{
		Success: false,
		Error:   msg,
	}
}

// ParseParams is a helper to parse JSON string parameters into a map
func ParseParams(jsonStr string) (map[string]interface{}, error) {
	var params map[string]interface{}
	if err := json.Unmarshal([]byte(jsonStr), &params); err != nil {
		return nil, err
	}
	return params, nil
}

// ParseRawParams is a helper to parse json.RawMessage parameters into a map
func ParseRawParams(raw json.RawMessage) (map[string]interface{}, error) {
	var params map[string]interface{}
	if err := json.Unmarshal(raw, &params); err != nil {
		return nil, err
	}
	return params, nil
}
