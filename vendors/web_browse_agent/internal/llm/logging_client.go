package llm

import (
	"encoding/json"
	"fmt"
	"strings"
	"time"

	"github.com/puter/web-browse-agent/internal/persistence"
)

// LoggingClient wraps an LLM client to log API calls to persistence
type LoggingClient struct {
	client      Client
	persistence *persistence.DB
	sessionID   string
}

// NewLoggingClient creates a new logging wrapper around an LLM client
func NewLoggingClient(client Client, persistenceDB *persistence.DB, sessionID string) *LoggingClient {
	return &LoggingClient{
		client:      client,
		persistence: persistenceDB,
		sessionID:   sessionID,
	}
}

// Chat sends messages to the LLM and logs the API call
func (c *LoggingClient) Chat(messages []Message, tools []ToolDefinition) (*Response, error) {
	startTime := time.Now()

	// Convert messages and tools to JSON for logging
	var requestJSON string
	// Create a simplified request object for logging
	requestObj := map[string]interface{}{
		"messages": messages,
		"tools":    tools,
		"model":    c.client.GetModel(),
	}
	if jsonData, err := json.Marshal(requestObj); err == nil {
		requestJSON = string(jsonData)
	}

	// Create headers JSON
	headers := map[string]string{
		"provider": c.client.Provider(),
	}
	headersJSON := "{}"
	if jsonData, err := json.Marshal(headers); err == nil {
		headersJSON = string(jsonData)
	}

	// Call the underlying client
	response, err := c.client.Chat(messages, tools)
	duration := time.Since(startTime)

	// Prepare API call for logging
	apiCall := &persistence.APICall{
		Timestamp:   startTime,
		SessionID:   c.sessionID,
		APIBaseURL:  c.client.GetBaseURL(),
		RequestJSON: requestJSON,
		HeadersJSON: headersJSON,
		Model:       c.client.GetModel(),
		DurationMS:  duration.Milliseconds(),
	}

	// Handle response or error
	if err != nil {
		apiCall.Status = "error"
		apiCall.ErrorMessage = err.Error()
		// Try to extract HTTP status from error if possible
		apiCall.HTTPStatus = 0 // Default, can't determine from error
		
		// Check if it's an HTTP error by looking for status codes in the error message
		// This is a simple heuristic - in a more complete implementation,
		// we would need to modify the LLM clients to return HTTP status
		if strings.Contains(err.Error(), "status 401") {
			apiCall.HTTPStatus = 401
		} else if strings.Contains(err.Error(), "status 403") {
			apiCall.HTTPStatus = 403
		} else if strings.Contains(err.Error(), "status 404") {
			apiCall.HTTPStatus = 404
		} else if strings.Contains(err.Error(), "status 429") {
			apiCall.HTTPStatus = 429
		} else if strings.Contains(err.Error(), "status 5") {
			apiCall.HTTPStatus = 500 // Generic 5xx error
		}
	} else {
		apiCall.Status = "success"
		apiCall.HTTPStatus = 200 // Assuming success means HTTP 200

		// Convert response to JSON for logging
		if responseJSON, err := json.Marshal(response); err == nil {
			apiCall.ResponseJSON = string(responseJSON)
		}

		// Count tool calls
		apiCall.ToolCount = len(response.ToolCalls)
	}

	// Log to persistence if available
	if c.persistence != nil {
		if err := c.persistence.LogAPICall(apiCall); err != nil {
			// Log error but don't fail the API call
			fmt.Printf("Warning: failed to log API call to persistence: %v\n", err)
		}
	}

	return response, err
}

// GetModel returns the model name from the wrapped client
func (c *LoggingClient) GetModel() string {
	return c.client.GetModel()
}

// GetBaseURL returns the base URL from the wrapped client
func (c *LoggingClient) GetBaseURL() string {
	return c.client.GetBaseURL()
}

// Provider returns the provider name from the wrapped client
func (c *LoggingClient) Provider() string {
	return c.client.Provider()
}