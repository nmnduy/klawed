package agent

import (
	"fmt"
	"log"

	"github.com/puter/web-browse-agent/internal/llm"
	"github.com/puter/web-browse-agent/internal/tool"
)

const (
	MaxIterations = 50
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
}

// NewAgent creates a new agent with the given LLM client and tool registry
func NewAgent(client llm.Client, registry *tool.Registry, verbose bool) *Agent {
	return &Agent{
		client:   client,
		registry: registry,
		messages: []llm.Message{},
		verbose:  verbose,
	}
}

// Run executes the agentic loop for the given user prompt
func (a *Agent) Run(userPrompt string) (string, error) {
	// Initialize conversation with system prompt and user message
	a.messages = []llm.Message{
		{Role: "system", Content: SystemPrompt},
		{Role: "user", Content: userPrompt},
	}

	// Get tool definitions for LLM
	tools := a.getToolDefinitions()

	for iteration := 0; iteration < MaxIterations; iteration++ {
		if a.verbose {
			log.Printf("Iteration %d", iteration+1)
		}

		// Call LLM
		response, err := a.client.Chat(a.messages, tools)
		if err != nil {
			return "", fmt.Errorf("LLM call failed: %w", err)
		}

		// Check stop reason
		if response.StopReason == "end_turn" || response.StopReason == "stop" {
			// Task complete
			if a.verbose {
				log.Println("Task completed")
			}
			return response.Content, nil
		}

		// Handle tool calls
		if len(response.ToolCalls) > 0 {
			// Add assistant message with tool calls
			a.addAssistantMessage(response)

			// Execute tools and add results
			for _, tc := range response.ToolCalls {
				result := a.executeTool(tc)
				a.addToolResult(tc.ID, tc.Name, result)
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
	if a.verbose {
		log.Printf("Executing tool: %s", tc.Name)
	}

	t, exists := a.registry.Get(tc.Name)
	if !exists {
		return tool.Error(fmt.Sprintf("unknown tool: %s", tc.Name))
	}

	// tc.Input is already map[string]interface{}
	output, err := t.Execute(tc.Input)
	if err != nil {
		if a.verbose {
			log.Printf("Tool %s failed: %s", tc.Name, err)
		}
		return tool.Error(fmt.Sprintf("tool execution error: %v", err))
	}

	if a.verbose {
		log.Printf("Tool %s succeeded", tc.Name)
	}

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
