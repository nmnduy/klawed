package llm

import (
	"fmt"
	"os"
	"strings"
)

// NewClient creates a new LLM client based on the provider
// If provider is empty, it reads from LLM_PROVIDER env var, defaulting to "openai"
func NewClient(provider string) (Client, error) {
	if provider == "" {
		provider = os.Getenv("LLM_PROVIDER")
		if provider == "" {
			provider = "openai"
		}
	}

	provider = strings.ToLower(provider)

	switch provider {
	case "openai":
		return NewOpenAIClient()
	case "anthropic", "claude":
		return NewAnthropicClient()
	default:
		return nil, fmt.Errorf("unsupported LLM provider: %s (supported: openai, anthropic)", provider)
	}
}
