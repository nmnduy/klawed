package agent

import (
	"encoding/json"
	"fmt"
	"log"
	"os"

	"github.com/puter/web-browse-agent/internal/llm"
	"github.com/puter/web-browse-agent/internal/tool"
)

const (
	MaxIterations = 1000000
	SystemPrompt  = `You are an AI agent that can browse the web and perform tasks using tools.
You have access to browser automation tools and file system tools.
When given a task, break it down into steps and use the available tools to accomplish it.
Always verify your actions by checking the results.
If a tool call fails, try alternative approaches.
When the task is complete, provide a summary of what was accomplished.`
)

// Agent implements the agentic loop
type Agent struct {
	client   llm.Client
	registry *tool.Registry
	messages []llm.Message
	verbose  bool
	logFile  *os.File
	logPath  string
}

// NewAgent creates a new agent with the given LLM client and tool registry
func NewAgent(client llm.Client, registry *tool.Registry, verbose bool, logPath string) *Agent {
	var logFile *os.File
	var err error

	if logPath != "" {
		logFile, err = os.OpenFile(logPath, os.O_CREATE|os.O_WRONLY|os.O_APPEND, 0644)
		if err != nil {
			log.Printf("Warning: failed to open log file %s: %v", logPath, err)
		}
	}

	return &Agent{
		client:   client,
		registry: registry,
		messages: []llm.Message{},
		verbose:  verbose,
		logFile:  logFile,
		logPath:  logPath,
	}
}

// log writes a message to both console and log file (if configured)
func (a *Agent) log(args ...interface{}) {
	msg := fmt.Sprint(args...)
	fmt.Println(msg)
	if a.logFile != nil {
		fmt.Fprintln(a.logFile, msg)
	}
}

// logJSON writes a JSON object to both console and log file (if configured)
func (a *Agent) logJSON(prefix string, data interface{}) {
	jsonData, err := json.MarshalIndent(data, "", "  ")
	if err != nil {
		a.log(prefix, ": failed to marshal JSON:", err)
		return
	}
	a.log(prefix, "\n", string(jsonData))
}

// Close closes the log file if open
func (a *Agent) Close() {
	if a.logFile != nil {
		a.logFile.Close()
	}
}

// Run executes the agentic loop for the given user prompt
func (a *Agent) Run(userPrompt string) (string, error) {
	defer a.Close()

	// Initialize conversation with system prompt and user message
	a.messages = []llm.Message{
		{Role: "system", Content: SystemPrompt},
		{Role: "user", Content: userPrompt},
	}

	a.log("=== User Prompt ===")
	a.log(userPrompt)
	a.log("=== End User Prompt ===")

	// Get tool definitions for LLM
	tools := a.getToolDefinitions()

	for iteration := 0; iteration < MaxIterations; iteration++ {
		a.log("--- Iteration", iteration+1, "---")

		// Call LLM
		response, err := a.client.Chat(a.messages, tools)
		if err != nil {
			return "", fmt.Errorf("LLM call failed: %w", err)
		}

		// Log LLM response
		a.log("=== LLM Response ===")
		a.logJSON("raw response", response)
		if response.Content != "" {
			a.log(response.Content)
		}
		if len(response.ToolCalls) > 0 {
			a.log("=== Tool Calls ===")
			for _, tc := range response.ToolCalls {
				a.logJSON("Tool Call:", tc)
			}
		}
		a.log("=== End LLM Response ===")

		// Handle known stop reasons first
		switch response.StopReason {
		case "end_turn", "stop":
			a.log("=== Task Completed ===")
			return response.Content, nil
		case "in_progress":
			a.log("LLM reported in_progress; waiting for more output")
			continue
		}

		// Handle empty responses to avoid infinite loops
		if response.Content == "" && len(response.ToolCalls) == 0 {
			errMsg := fmt.Sprintf("LLM returned empty response (stop_reason=%s)", response.StopReason)
			a.log(errMsg)
			return "", fmt.Errorf(errMsg)
		}

		// Handle tool calls
		if len(response.ToolCalls) > 0 {
			// Add assistant message with tool calls
			a.addAssistantMessage(response)

			// Execute tools and add results
			for _, tc := range response.ToolCalls {
				a.log("--- Executing Tool:", tc.Name, "---")
				result := a.executeTool(tc)
				a.addToolResult(tc.ID, tc.Name, result)
				a.log("--- End Tool:", tc.Name, "---")
			}
		} else if response.Content != "" {
			// No tool calls, just content - might be done
			return response.Content, nil
		}
	}

	return "", fmt.Errorf("max iterations (%d) reached without completing task", MaxIterations)
}

// getToolDefinitions converts registry tools to LLM format
func (a *Agent) getToolDefinitions() []llm.ToolDefinition {
	tools := a.registry.List()
	definitions := make([]llm.ToolDefinition, len(tools))

	for i, t := range tools {
		definitions[i] = llm.ToolDefinition{
			Name:        t.Name(),
			Description: t.Description(),
			InputSchema: t.ParametersSchema(),
		}
	}

	return definitions
}

// executeTool runs a tool and returns the result
func (a *Agent) executeTool(tc llm.ToolCall) *tool.Result {
	t, exists := a.registry.Get(tc.Name)
	if !exists {
		return tool.Error(fmt.Sprintf("unknown tool: %s", tc.Name))
	}

	// tc.Input is already map[string]interface{}
	output, err := t.Execute(tc.Input)
	if err != nil {
		return tool.Error(fmt.Sprintf("tool execution error: %v", err))
	}

	// Print tool result to both console and log file
	a.log("=== Tool Output:", tc.Name, "===")
	a.log(output)
	a.log("=== End Tool Output:", tc.Name, "===")

	return tool.Success(output)
}

// addAssistantMessage adds the assistant's response to conversation
func (a *Agent) addAssistantMessage(response *llm.Response) {
	// Build content blocks for tool uses
	var content []llm.ContentBlock

	if response.Content != "" {
		content = append(content, llm.ContentBlock{
			Type: "text",
			Text: response.Content,
		})
	}

	for _, tc := range response.ToolCalls {
		content = append(content, llm.ContentBlock{
			Type:  "tool_use",
			ID:    tc.ID,
			Name:  tc.Name,
			Input: tc.Input,
		})
	}

	a.messages = append(a.messages, llm.Message{
		Role:    "assistant",
		Content: content,
	})
}

// addToolResult adds a tool result to the conversation
func (a *Agent) addToolResult(toolID, toolName string, result *tool.Result) {
	output := result.Output
	if !result.Success {
		output = fmt.Sprintf("Error: %s", result.Error)
	}

	a.messages = append(a.messages, llm.Message{
		Role: "user",
		Content: []llm.ContentBlock{
			{
				Type:      "tool_result",
				ToolUseID: toolID,
				Content:   output,
			},
		},
	})
}

// Reset clears the conversation history
func (a *Agent) Reset() {
	a.messages = []llm.Message{}
}
