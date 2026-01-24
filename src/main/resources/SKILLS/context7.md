# Context7: Library Documentation Search for AI Agents

Context7 provides instant access to up-to-date documentation for popular libraries and frameworks. Instead of relying on potentially outdated training data, use Context7 to get current, accurate documentation directly in your context.

**Last tested: 2026-01-24**

---

## Quick Reference

| Feature | Details |
|---------|---------|
| Tool | `c7` (Context7 CLI) |
| Installation | `npm install -g context7` |
| Auth Required | No |
| Rate Limits | Unspecified (be respectful) |
| Output Formats | `txt` (default), `json` |

---

## Why Use Context7?

1. **Up-to-date documentation** - Unlike training data, Context7 fetches current docs
2. **Framework-specific context** - Get relevant docs for React, Next.js, Python libs, etc.
3. **Focused responses** - Query specific topics instead of getting entire documentation
4. **Token-efficient** - Control output size with `--tokens` flag

---

## Installation

```bash
# npm
npm install -g context7

# yarn
yarn global add context7

# bun
bun install -g context7
```

Verify installation:
```bash
c7 --help
```

---

## Core Commands

### 1. Search for Projects

Find available documentation libraries by keyword:

```bash
c7 search <term>
```

**Examples:**
```bash
c7 search react
c7 search python
c7 search aws
c7 search database
```

**Output shows:**
- Project title (human-readable name)
- Project name (use this for queries)

### 2. Query Documentation

Get documentation on a specific topic within a project:

```bash
c7 <projectname> <query...>
```

**Examples:**
```bash
# React hooks documentation
c7 react hooks useEffect cleanup

# Next.js data fetching
c7 nextjs data fetching strategies

# Python package installation
c7 uv installation steps

# Node.js streams API
c7 nodejs streams readable writable
```

### 3. Get Project Info

View metadata about a project (source, last update, etc.):

```bash
c7 info <projectname>
```

**Example:**
```bash
c7 info nextjs
```

---

## Command Options

### Output Format (`-t, --type`)

```bash
# Default: plain text
c7 react hooks -t txt

# JSON format (useful for parsing)
c7 react hooks -t json
```

### Token Limit (`-k, --tokens`)

Control response size (default: 5000):

```bash
# Shorter response (fewer tokens)
c7 nextjs routing -k 2000

# Longer, more detailed response
c7 nextjs routing -k 10000
```

### Save to File (`-s, --save`)

Save output to `llms_{projectname}.{format}`:

```bash
# Saves to llms_nextjs.txt
c7 nextjs data fetching --save

# Saves to llms_react.json
c7 react state management -t json -s
```

---

## Common Workflows for AI Agents

### Workflow 1: Research Before Coding

Before writing code with a library you're unsure about:

```bash
# 1. Find the project
c7 search prisma

# 2. Get relevant docs
c7 prisma database schema migrations

# 3. Use the documentation to write accurate code
```

### Workflow 2: Verify API Usage

When uncertain about current API syntax:

```bash
# Get current API documentation
c7 react useEffect dependencies cleanup
```

### Workflow 3: Learn New Features

When a user mentions a feature you may not know:

```bash
# Example: Next.js App Router (newer feature)
c7 nextjs app router server components
```

### Workflow 4: Troubleshooting

When debugging library-specific issues:

```bash
# Get error handling documentation
c7 axios error handling interceptors
```

---

## Popular Projects Available

| Category | Project Names |
|----------|---------------|
| Frontend | `react`, `nextjs`, `vue`, `svelte`, `angular` |
| Backend | `nodejs`, `express`, `fastapi`, `django` |
| Database | `prisma`, `mongoose`, `typeorm`, `drizzle` |
| Python | `uv`, `poetry`, `pydantic`, `langchain` |
| DevOps | `docker`, `kubernetes`, `terraform` |
| Cloud | `aws`, `vercel`, `supabase` |

> **Note:** Use `c7 search <term>` to discover available projects. Not all libraries are indexed.

---

## Best Practices for AI Agents

### 1. Search Before Querying

Always verify a project exists before querying:

```bash
# Good: Check if project exists
c7 search tailwind
# Then query
c7 tailwindcss responsive design

# Bad: Guessing project names
c7 tailwind-css responsive design  # May not work
```

### 2. Be Specific in Queries

More specific queries yield better results:

```bash
# Good: Specific topic
c7 react useCallback memoization dependencies

# Less ideal: Too broad
c7 react hooks
```

### 3. Control Token Usage

Use appropriate token limits for your needs:

```bash
# Quick reference (fewer tokens)
c7 nextjs image component -k 1500

# Deep dive (more tokens)
c7 nextjs image component optimization -k 8000
```

### 4. Use JSON for Parsing

When you need structured data:

```bash
c7 react hooks -t json | jq '.content'
```

### 5. Cache Results When Appropriate

If making multiple related queries, save results:

```bash
c7 nextjs complete api reference --save -k 10000
# Then read from llms_nextjs.txt instead of re-querying
```

---

## Integration Examples

### Bash Script for Documentation Lookup

```bash
#!/bin/bash
# get-docs.sh - Quick documentation lookup

PROJECT="$1"
TOPIC="${@:2}"

if [ -z "$PROJECT" ] || [ -z "$TOPIC" ]; then
    echo "Usage: get-docs.sh <project> <topic...>"
    exit 1
fi

c7 "$PROJECT" "$TOPIC" -k 3000
```

### Python Subprocess Integration

```python
import subprocess
import json

def get_docs(project: str, query: str, tokens: int = 5000) -> str:
    """
    Get documentation from Context7.
    
    Args:
        project: Project name (e.g., 'react', 'nextjs')
        query: Topic to search for
        tokens: Max tokens for response
    
    Returns:
        Documentation text
    """
    cmd = ["c7", project, query, "-k", str(tokens)]
    result = subprocess.run(cmd, capture_output=True, text=True)
    
    if result.returncode != 0:
        raise Exception(f"Context7 error: {result.stderr}")
    
    return result.stdout

def get_docs_json(project: str, query: str) -> dict:
    """Get documentation as JSON."""
    cmd = ["c7", project, query, "-t", "json"]
    result = subprocess.run(cmd, capture_output=True, text=True)
    
    if result.returncode != 0:
        raise Exception(f"Context7 error: {result.stderr}")
    
    return json.loads(result.stdout)

# Usage
docs = get_docs("react", "useEffect cleanup function")
print(docs)
```

---

## Troubleshooting

### "Project not found"

```bash
# Search for the correct project name
c7 search <library-name>
```

### Empty or minimal results

```bash
# Try different query terms
c7 nextjs "server side rendering"  # vs
c7 nextjs SSR getServerSideProps
```

### Rate limiting

If you get rate-limited:
1. Wait a few seconds between requests
2. Cache results for repeated queries
3. Use `--save` to store responses locally

### Network errors

The API may be temporarily unavailable. Implement retry logic:

```bash
# Simple retry in bash
for i in {1..3}; do
    c7 react hooks && break
    sleep 2
done
```

---

## When to Use Context7 vs. Other Sources

| Scenario | Use Context7 | Use Web Search |
|----------|--------------|----------------|
| Library-specific docs | ✅ | |
| Current API syntax | ✅ | |
| General programming concepts | | ✅ |
| Stack Overflow solutions | | ✅ |
| Library comparisons | | ✅ |
| Latest release notes | ✅ | ✅ |

---

## Summary

Context7 is your go-to tool for **current, accurate library documentation**. Use it when:

1. You need to verify API syntax or usage
2. A user asks about library-specific features
3. You're uncertain about documentation from training data
4. You need focused documentation on a specific topic

**Key commands to remember:**

```bash
c7 search <term>           # Find projects
c7 <project> <query>       # Get documentation
c7 info <project>          # Get project metadata
```
