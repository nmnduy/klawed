package main

import (
	"bytes"
	"encoding/json"
	"errors"
	"flag"
	"fmt"
	"io"
	"net/http"
	"os"
	"time"
)

// --- Types for the Responses API ---

type reasoningConfig struct {
	Effort string `json:"effort,omitempty"`
}

type responsesRequest struct {
	Model           string           `json:"model"`
	Input           string           `json:"input"`
	MaxOutputTokens int              `json:"max_output_tokens"`
	Store           bool             `json:"store"`
	Reasoning       *reasoningConfig `json:"reasoning,omitempty"`
}

type responseOutputItem struct {
	Type    string `json:"type"`
	Content []struct {
		Type string `json:"type"`
		Text string `json:"text"`
	} `json:"content,omitempty"`
	Role string `json:"role,omitempty"`
}

type responsesError struct {
	Code    string `json:"code"`
	Message string `json:"message"`
}

type responsesResponse struct {
	ID     string               `json:"id"`
	Status string               `json:"status"`
	Output []responseOutputItem `json:"output"`
	Error  *responsesError      `json:"error,omitempty"`
	Usage  *struct {
		InputTokens  int `json:"input_tokens"`
		OutputTokens int `json:"output_tokens"`
	} `json:"usage,omitempty"`
}

// --- Types for Chat Completions API ---

type chatMessage struct {
	Role    string `json:"role"`
	Content string `json:"content"`
}

type chatRequest struct {
	Model               string        `json:"model"`
	Messages            []chatMessage `json:"messages"`
	MaxCompletionTokens int           `json:"max_completion_tokens"`
	ReasoningEffort     string        `json:"reasoning_effort,omitempty"`
}

type chatChoice struct {
	Index   int         `json:"index"`
	Message chatMessage `json:"message"`
}

type chatUsage struct {
	PromptTokens     int `json:"prompt_tokens"`
	CompletionTokens int `json:"completion_tokens"`
	TotalTokens      int `json:"total_tokens"`
}

type chatResponse struct {
	ID      string       `json:"id"`
	Object  string       `json:"object"`
	Created int64        `json:"created"`
	Model   string       `json:"model"`
	Choices []chatChoice `json:"choices"`
	Usage   *chatUsage   `json:"usage,omitempty"`
}

// --- Config ---

type config struct {
	prompt          string
	promptFile      string
	outputFile      string
	model           string
	maxTokens       int
	apiKey          string
	apiBase         string
	useChat         bool
	showUsage       bool
	reasoningEffort string
	listEfforts     bool
	doctor          bool
}

func parseFlags() config {
	var cfg config

	flag.StringVar(&cfg.prompt, "p", "", "Prompt text (inline)")
	flag.StringVar(&cfg.prompt, "prompt", "", "Prompt text (inline)")
	flag.StringVar(&cfg.promptFile, "f", "", "Read prompt from file")
	flag.StringVar(&cfg.promptFile, "file", "", "Read prompt from file")
	flag.StringVar(&cfg.outputFile, "o", "", "Write output to file (default: stdout)")
	flag.StringVar(&cfg.outputFile, "output", "", "Write output to file (default: stdout)")
	flag.StringVar(&cfg.reasoningEffort, "reasoning-effort", "", "Reasoning effort: low, medium, high")
	flag.BoolVar(&cfg.listEfforts, "list-efforts", false, "List available reasoning effort values and exit")
	flag.BoolVar(&cfg.doctor, "doctor", false, "Run self-diagnostic checks and exit")

	flag.StringVar(&cfg.model, "m", "gpt-5.5", "Model name")
	flag.StringVar(&cfg.model, "model", "gpt-5.5", "Model name")
	flag.IntVar(&cfg.maxTokens, "max-tokens", 32768, "Maximum output tokens")
	flag.StringVar(&cfg.apiKey, "api-key", "", "OpenAI API key (default: $OPENAI_API_KEY)")
	flag.StringVar(&cfg.apiBase, "api-base", "https://api.openai.com/v1", "API base URL")
	flag.StringVar(&cfg.apiBase, "api-base-url", "https://api.openai.com/v1", "API base URL")
	flag.BoolVar(&cfg.useChat, "chat", false, "Use Chat Completions API instead of Responses API")
	flag.BoolVar(&cfg.showUsage, "show-usage", false, "Print token usage to stderr")

	flag.Usage = func() {
		fmt.Fprintf(os.Stderr, `dear_oracle — submit a prompt to a large model

Usage:
  dear_oracle [flags] -p "your prompt"
  dear_oracle [flags] -f prompt.txt
  echo "your prompt" | dear_oracle [flags]

Flags:
  -p, --prompt TEXT          Prompt text (inline)
  -f, --file PATH            Read prompt from file
  -o, --output PATH          Write output to file (default: stdout)
  -m, --model NAME           Model name (default: gpt-5.5)
  --max-tokens N             Max output tokens (default: 32768)
  --reasoning-effort LEVEL   Reasoning effort: low, medium, high
  --list-efforts             List available reasoning effort values and exit
  --doctor                   Run self-diagnostics to check configuration
  --api-key KEY              API key (default: $OPENAI_API_KEY)
  --api-base URL             API base URL (default: https://api.openai.com/v1)
  --chat                     Use Chat Completions API instead of Responses API
  --show-usage               Print token usage to stderr
  -h, --help                 Show this help

Environment:
  OPENAI_API_KEY            Required unless --api-key is provided

Examples:
  # Send a prompt inline
  dear_oracle -p "Explain quantum computing in one sentence"

  # Read prompt from a file, save output
  dear_oracle -f specs.txt -o answer.md

  # Pipe a prompt from another command
  cat prompt.txt | dear_oracle -m gpt-5.5

  # Use chat completions with a cheaper model
  dear_oracle -p "summarize this" --chat -m gpt-4o-mini

  # Use high reasoning effort for complex decisions
  dear_oracle -p "do the hard thing" --reasoning-effort high
`)
	}

	flag.Parse()
	return cfg
}

func resolvePrompt(cfg config) (string, error) {
	if cfg.promptFile != "" {
		data, err := os.ReadFile(cfg.promptFile)
		if err != nil {
			return "", fmt.Errorf("reading prompt file: %w", err)
		}
		return string(data), nil
	}

	if cfg.prompt != "" {
		return cfg.prompt, nil
	}

	// Try stdin (pipe mode)
	stat, err := os.Stdin.Stat()
	if err != nil {
		return "", fmt.Errorf("checking stdin: %w", err)
	}

	if stat.Mode()&os.ModeCharDevice == 0 {
		data, err := io.ReadAll(os.Stdin)
		if err != nil {
			return "", fmt.Errorf("reading stdin: %w", err)
		}
		return string(data), nil
	}

	// Check for positional argument
	args := flag.Args()
	if len(args) > 0 {
		return stringsJoin(args, " "), nil
	}

	return "", errors.New("no prompt provided. Use -p, -f, pipe input, or pass as argument. See --help")
}

func stringsJoin(elems []string, sep string) string {
	var b bytes.Buffer
	for i, s := range elems {
		if i > 0 {
			b.WriteString(sep)
		}
		b.WriteString(s)
	}
	return b.String()
}

// --- API call ---

func callResponsesAPI(cfg config, prompt string) (string, responsesResponse, error) {
	url := cfg.apiBase + "/responses"

	body := responsesRequest{
		Model:           cfg.model,
		Input:           prompt,
		MaxOutputTokens: cfg.maxTokens,
		Store:           false,
	}

	if cfg.reasoningEffort != "" {
		body.Reasoning = &reasoningConfig{Effort: cfg.reasoningEffort}
	}

	payload, err := json.Marshal(body)
	if err != nil {
		return "", responsesResponse{}, fmt.Errorf("marshalling request: %w", err)
	}

	req, err := http.NewRequest("POST", url, bytes.NewReader(payload))
	if err != nil {
		return "", responsesResponse{}, fmt.Errorf("creating request: %w", err)
	}

	req.Header.Set("Content-Type", "application/json")
	req.Header.Set("Authorization", "Bearer "+cfg.apiKey)

	client := &http.Client{Timeout: 120 * time.Second}

	resp, err := client.Do(req)
	if err != nil {
		return "", responsesResponse{}, fmt.Errorf("API request failed: %w", err)
	}
	defer resp.Body.Close()

	respBody, err := io.ReadAll(resp.Body)
	if err != nil {
		return "", responsesResponse{}, fmt.Errorf("reading response: %w", err)
	}

	if resp.StatusCode != http.StatusOK {
		return "", responsesResponse{}, fmt.Errorf("API error (HTTP %d): %s", resp.StatusCode, string(respBody))
	}

	var result responsesResponse
	if err := json.Unmarshal(respBody, &result); err != nil {
		return "", responsesResponse{}, fmt.Errorf("parsing response: %w", err)
	}

	if result.Error != nil {
		return "", result, fmt.Errorf("API error: [%s] %s", result.Error.Code, result.Error.Message)
	}

	// Extract text from output items
	var text string
	for _, item := range result.Output {
		if item.Type == "message" {
			for _, content := range item.Content {
				if content.Type == "output_text" {
					text += content.Text
				}
			}
		}
	}

	return text, result, nil
}

func callChatAPI(cfg config, prompt string) (string, chatResponse, error) {
	url := cfg.apiBase + "/chat/completions"

	body := chatRequest{
		Model: cfg.model,
		Messages: []chatMessage{
			{Role: "user", Content: prompt},
		},
		MaxCompletionTokens: cfg.maxTokens,
		ReasoningEffort:     cfg.reasoningEffort,
	}

	payload, err := json.Marshal(body)
	if err != nil {
		return "", chatResponse{}, fmt.Errorf("marshalling request: %w", err)
	}

	req, err := http.NewRequest("POST", url, bytes.NewReader(payload))
	if err != nil {
		return "", chatResponse{}, fmt.Errorf("creating request: %w", err)
	}

	req.Header.Set("Content-Type", "application/json")
	req.Header.Set("Authorization", "Bearer "+cfg.apiKey)

	client := &http.Client{Timeout: 120 * time.Second}

	resp, err := client.Do(req)
	if err != nil {
		return "", chatResponse{}, fmt.Errorf("API request failed: %w", err)
	}
	defer resp.Body.Close()

	respBody, err := io.ReadAll(resp.Body)
	if err != nil {
		return "", chatResponse{}, fmt.Errorf("reading response: %w", err)
	}

	if resp.StatusCode != http.StatusOK {
		return "", chatResponse{}, fmt.Errorf("API error (HTTP %d): %s", resp.StatusCode, string(respBody))
	}

	var result chatResponse
	if err := json.Unmarshal(respBody, &result); err != nil {
		return "", chatResponse{}, fmt.Errorf("parsing response: %w", err)
	}

	if len(result.Choices) == 0 {
		return "", result, errors.New("no choices returned from API")
	}

	return result.Choices[0].Message.Content, result, nil
}

func writeOutput(cfg config, text string) error {
	if cfg.outputFile != "" {
		return os.WriteFile(cfg.outputFile, []byte(text), 0644)
	}
	_, err := fmt.Print(text)
	return err
}

// runDoctor performs self-diagnostics to check if dear_oracle is configured correctly.
func runDoctor(cfg config) {
	ok := 0
	fail := 0

	check := func(name string, passed bool, info string) {
		if passed {
			fmt.Fprintf(os.Stderr, "  ✅ %s — %s\n", name, info)
			ok++
		} else {
			fmt.Fprintf(os.Stderr, "  ❌ %s — %s\n", name, info)
			fail++
		}
	}

	fmt.Fprintln(os.Stderr, "🔍 dear_oracle doctor — checking configuration")
	fmt.Fprintln(os.Stderr, "")

	// Check API key
	key := cfg.apiKey
	if key == "" {
		key = os.Getenv("OPENAI_API_KEY")
	}
	if key != "" {
		prefix := ""
		if len(key) > 8 {
			prefix = key[:8]
		}
		check("API key", true,
			fmt.Sprintf("found (%s...%d chars)", prefix, len(key)))
	} else {
		check("API key", false,
			"not set. Use --api-key or set OPENAI_API_KEY")
	}

	// Check API base URL
	check("API base URL", cfg.apiBase != "",
		fmt.Sprintf("configured as %s", cfg.apiBase))

	// Check API reachability
	if cfg.apiBase != "" {
		client := &http.Client{Timeout: 10 * time.Second}
		req, err := http.NewRequest("GET", cfg.apiBase+"/models", nil)
		if err == nil {
			if key != "" {
				req.Header.Set("Authorization", "Bearer "+key)
			}
			resp, connErr := client.Do(req)
			if connErr != nil {
				check("API connectivity", false,
					fmt.Sprintf("cannot reach %s: %v", cfg.apiBase, connErr))
			} else {
				resp.Body.Close()
				if resp.StatusCode == http.StatusOK || resp.StatusCode == http.StatusUnauthorized {
					if resp.StatusCode == http.StatusOK {
						check("API connectivity", true, "API is reachable and key is valid")
					} else {
						check("API connectivity", true,
							fmt.Sprintf("API is reachable (HTTP %d — key may be invalid or endpoint requires auth)", resp.StatusCode))
					}
				} else {
					check("API connectivity", false,
						fmt.Sprintf("unexpected HTTP %d from %s/models", resp.StatusCode, cfg.apiBase))
				}
			}
		} else {
			check("API connectivity", false,
				fmt.Sprintf("failed to create request: %v", err))
		}
	}

	// Check model name
	check("Model name", cfg.model != "",
		fmt.Sprintf("configured as %q", cfg.model))

	fmt.Fprintln(os.Stderr, "")

	// Summary
	if fail == 0 {
		fmt.Fprintf(os.Stderr, "✅ All %d checks passed.\n", ok)
	} else {
		fmt.Fprintf(os.Stderr, "⚠️  %d passed, %d failed\n", ok, fail)
		os.Exit(1)
	}
}

func main() {
	cfg := parseFlags()

	// Handle --doctor (run before any other checks to avoid requiring a prompt)
	if cfg.doctor {
		runDoctor(cfg)
		os.Exit(0)
	}

	// Handle --list-efforts
	if cfg.listEfforts {
		fmt.Fprintln(os.Stderr, "Available reasoning effort values:")
		fmt.Fprintln(os.Stderr, "  low     - Fast, minimal reasoning (best for simple tasks)")
		fmt.Fprintln(os.Stderr, "  medium  - Balanced reasoning (default)")
		fmt.Fprintln(os.Stderr, "  high    - Deep, thorough reasoning (best for complex decisions)")
		fmt.Fprintln(os.Stderr, "")
		fmt.Fprintln(os.Stderr, "Usage: --reasoning-effort low|medium|high")
		os.Exit(0)
	}

	// Validate reasoning effort
	if cfg.reasoningEffort != "" {
		switch cfg.reasoningEffort {
		case "low", "medium", "high":
			// valid
		default:
			fmt.Fprintf(os.Stderr, "Error: invalid reasoning effort %q. Valid values: low, medium, high (see --list-efforts)\n", cfg.reasoningEffort)
			os.Exit(1)
		}
	}

	// Resolve API key
	if cfg.apiKey == "" {
		cfg.apiKey = os.Getenv("OPENAI_API_KEY")
	}
	if cfg.apiKey == "" {
		fmt.Fprintln(os.Stderr, "Error: No API key found. Set OPENAI_API_KEY or use --api-key")
		os.Exit(1)
	}

	// Resolve prompt
	prompt, err := resolvePrompt(cfg)
	if err != nil {
		fmt.Fprintf(os.Stderr, "Error: %v\n", err)
		os.Exit(1)
	}

	// Print status to stderr (keeps stdout clean for piping)
	if cfg.outputFile == "" {
		mode := "responses"
		if cfg.useChat {
			mode = "chat"
		}
		if cfg.reasoningEffort != "" {
			fmt.Fprintf(os.Stderr, "🔮 dear_oracle: asking %s (%s reasoning) via %s API...\n", cfg.model, cfg.reasoningEffort, mode)
		} else {
			fmt.Fprintf(os.Stderr, "🔮 dear_oracle: asking %s via %s API...\n", cfg.model, mode)
		}
	}

	// Call API
	var text string
	var usageInfo string

	if cfg.useChat {
		chatText, chatResp, err := callChatAPI(cfg, prompt)
		if err != nil {
			fmt.Fprintf(os.Stderr, "Error: %v\n", err)
			os.Exit(1)
		}
		text = chatText

		if cfg.showUsage && chatResp.Usage != nil {
			usageInfo = fmt.Sprintf("\n[usage: %d in / %d out / %d total]",
				chatResp.Usage.PromptTokens,
				chatResp.Usage.CompletionTokens,
				chatResp.Usage.TotalTokens)
		}
	} else {
		respText, resp, err := callResponsesAPI(cfg, prompt)
		if err != nil {
			fmt.Fprintf(os.Stderr, "Error: %v\n", err)
			os.Exit(1)
		}
		text = respText

		if cfg.showUsage && resp.Usage != nil {
			usageInfo = fmt.Sprintf("\n[usage: %d in / %d out]",
				resp.Usage.InputTokens,
				resp.Usage.OutputTokens)
		}
	}

	// Append trailing newline if missing
	if len(text) > 0 && text[len(text)-1] != '\n' {
		text += "\n"
	}

	// Write output
	if err := writeOutput(cfg, text); err != nil {
		fmt.Fprintf(os.Stderr, "Error writing output: %v\n", err)
		os.Exit(1)
	}

	// Print usage info to stderr
	if usageInfo != "" {
		fmt.Fprint(os.Stderr, usageInfo)
	}
}
