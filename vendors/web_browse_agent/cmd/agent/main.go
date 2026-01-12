package main

import (
	"bufio"
	"fmt"
	"os"
	"strings"

	"github.com/puter/web-browse-agent/internal/agent"
	"github.com/puter/web-browse-agent/internal/browser"
	"github.com/puter/web-browse-agent/internal/llm"
	"github.com/puter/web-browse-agent/internal/tool"
	"github.com/spf13/cobra"
)

var (
	provider    string
	interactive bool
	verbose     bool
	headless    bool
	noBrowser   bool
)

func main() {
	rootCmd := &cobra.Command{
		Use:   "web-browse-agent [prompt]",
		Short: "AI Agent for web browsing and UI testing",
		Long: `An AI agent that uses an agentic loop pattern to accomplish tasks
through recursive tool calls. It can browse the web, interact with pages,
read/write files, and execute shell commands.

Examples:
  web-browse-agent "go to google.com and search for playwright"
  web-browse-agent --interactive
  web-browse-agent --provider anthropic "test the login form"`,
		Args: cobra.MaximumNArgs(1),
		RunE: runAgent,
	}

	rootCmd.Flags().StringVarP(&provider, "provider", "p", "", "LLM provider (openai or anthropic)")
	rootCmd.Flags().BoolVarP(&interactive, "interactive", "i", false, "Run in interactive mode")
	rootCmd.Flags().BoolVarP(&verbose, "verbose", "v", false, "Enable verbose output")
	rootCmd.Flags().BoolVar(&headless, "headless", false, "Run browser in headless mode")
	rootCmd.Flags().BoolVar(&noBrowser, "no-browser", false, "Disable browser tools")

	if err := rootCmd.Execute(); err != nil {
		fmt.Fprintln(os.Stderr, err)
		os.Exit(1)
	}
}

func runAgent(cmd *cobra.Command, args []string) error {
	// Override headless from flag
	if headless {
		os.Setenv("BROWSER_HEADLESS", "true")
	}

	// Create LLM client
	client, err := llm.NewClient(provider)
	if err != nil {
		return fmt.Errorf("failed to create LLM client: %w", err)
	}

	if verbose {
		fmt.Printf("Using LLM provider: %s, model: %s\n", provider, client.GetModel())
	}

	// Create tool registry
	registry := tool.NewRegistry()

	// Register core tools
	registry.Register(tool.NewReadFileTool())
	registry.Register(tool.NewWriteFileTool())
	registry.Register(tool.NewBashTool())

	// Register browser tools if enabled
	var browserCtx *browser.Context
	if !noBrowser {
		config := browser.NewConfigFromEnv()
		browserCtx, err = browser.NewContext(config)
		if err != nil {
			fmt.Fprintf(os.Stderr, "Warning: Failed to initialize browser: %v\n", err)
			fmt.Fprintln(os.Stderr, "Continuing without browser tools...")
		} else {
			defer browserCtx.Close()
			browser.RegisterBrowserTools(registry, browserCtx)
			if verbose {
				fmt.Println("Browser tools enabled")
			}
		}
	}

	// Create agent
	ag := agent.NewAgent(client, registry, verbose)

	if interactive {
		return runInteractive(ag)
	}

	// Single prompt mode
	if len(args) == 0 {
		return fmt.Errorf("prompt required (or use --interactive)")
	}

	result, err := ag.Run(args[0])
	if err != nil {
		return err
	}

	fmt.Println(result)
	return nil
}

func runInteractive(ag *agent.Agent) error {
	fmt.Println("Web Browse Agent - Interactive Mode")
	fmt.Println("Type 'exit' or 'quit' to exit")
	fmt.Println()

	reader := bufio.NewReader(os.Stdin)

	for {
		fmt.Print("> ")
		input, err := reader.ReadString('\n')
		if err != nil {
			return err
		}

		input = strings.TrimSpace(input)
		if input == "" {
			continue
		}

		if input == "exit" || input == "quit" {
			fmt.Println("Goodbye!")
			return nil
		}

		if input == "reset" {
			ag.Reset()
			fmt.Println("Conversation reset")
			continue
		}

		result, err := ag.Run(input)
		if err != nil {
			fmt.Fprintf(os.Stderr, "Error: %v\n", err)
			continue
		}

		fmt.Println()
		fmt.Println(result)
		fmt.Println()
	}
}
