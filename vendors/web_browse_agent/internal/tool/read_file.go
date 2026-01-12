package tool

import (
	"fmt"
	"os"
)

// ReadFileTool reads contents of a file
type ReadFileTool struct{}

// NewReadFileTool creates a new ReadFileTool
func NewReadFileTool() *ReadFileTool {
	return &ReadFileTool{}
}

// Name returns the tool name
func (t *ReadFileTool) Name() string {
	return "read_file"
}

// Description returns the tool description
func (t *ReadFileTool) Description() string {
	return "Read contents of a file"
}

// ParametersSchema returns the JSON schema for the tool parameters
func (t *ReadFileTool) ParametersSchema() map[string]interface{} {
	return map[string]interface{}{
		"type": "object",
		"properties": map[string]interface{}{
			"path": map[string]interface{}{
				"type":        "string",
				"description": "The path to the file to read",
			},
		},
		"required": []string{"path"},
	}
}

// Execute reads the file at the specified path
func (t *ReadFileTool) Execute(params map[string]interface{}) (string, error) {
	path, _ := params["path"].(string)
	if path == "" {
		return "", fmt.Errorf("path is required")
	}

	content, err := os.ReadFile(path)
	if err != nil {
		return "", fmt.Errorf("failed to read file: %v", err)
	}

	return string(content), nil
}
