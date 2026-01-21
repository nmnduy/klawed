# GitHub Connector Browsing SKILL

This SKILL enables AI agents to browse GitHub repositories (specifically n8n connectors) without downloading the entire repository. It provides tools to:
1. Browse GitHub API to read connector code on-demand
2. Cache locally only what's needed
3. Analyze connector patterns for implementation
4. Generate new connectors based on existing patterns

## Why This Approach?

- **n8n repository**: ~500MB with ~300 connectors
- **Traditional approach**: Download entire repo → 500MB per workspace
- **Our approach**: Browse GitHub API → Download only needed files → Minimal storage

## Quick Start - AI Agent Workflow

Here's how an AI agent should use these tools to create a new n8n connector:

### Step 1: Find Similar Connectors
```bash
# List all available connectors
./github_search_connectors.sh --list-all

# Search for connectors by name
./github_search_connectors.sh stripe
./github_search_connectors.sh notion

# Search by category (database, payment, social, etc.)
./github_search_connectors.sh database --category
./github_search_connectors.sh payment --category
```

### Step 2: Browse Connector Code
```bash
# List files in a connector
./github_browse_connector.sh Airtable --list

# View the main node implementation
./github_browse_connector.sh Airtable --file Airtable.node.ts --raw

# View helper functions
./github_browse_connector.sh Airtable --file GenericFunctions.ts --raw
```

### Step 3: Analyze Patterns
```bash
# Analyze a single connector
./analyze_connector_pattern.sh Airtable --format json

# Deep analysis (fetches more files)
./analyze_connector_pattern.sh Notion --deep --format json

# Compare multiple connectors
./analyze_connector_pattern.sh Airtable Notion GoogleSheets --compare

# Save analysis to file
./analyze_connector_pattern.sh Stripe PayPal --output payment_patterns.json --format json
```

### Step 4: Cache Useful Connectors
```bash
# Cache a connector for offline access
./cache_connector.sh add Airtable

# Cache with refresh
./cache_connector.sh add GoogleSheets --refresh

# Check cache status
./cache_connector.sh status

# List cached connectors
./cache_connector.sh list
```

### Step 5: Generate New Connector
```bash
# Generate basic connector structure
./generate_connector_template.sh MyNewService

# Generate based on existing connector patterns
./generate_connector_template.sh MyDatabase --based-on Airtable

# Generate to specific directory
./generate_connector_template.sh PaymentService --based-on Stripe --output ./my-connectors

# Generate just the node code (print to stdout)
./generate_connector_template.sh QuickAPI --properties
```

## Available Scripts

### 1. `github_search_connectors.sh`
Search for connectors by name, category, or functionality.

```bash
Options:
  -l, --list-all    List all available connectors
  -c, --category    Search by category (google, microsoft, database, social, etc.)
  -p, --pattern     Search by file pattern
  -m, --match-case  Case-sensitive search
  -r, --refresh     Refresh connector list cache
```

### 2. `github_browse_connector.sh`
Browse a specific connector from n8n repository via GitHub API.

```bash
Options:
  -l, --list        List files in the connector directory
  -f, --file <path> Get specific file content
  -r, --raw         Get raw file content (no JSON wrapper)
  -c, --cache       Force cache refresh
```

### 3. `analyze_connector_pattern.sh`
Analyze connector patterns to understand implementation structure.

```bash
Options:
  -o, --output <file>     Output analysis to file
  -f, --format <format>   Output format: json, yaml, markdown
  -c, --compare           Compare patterns between connectors
  -p, --patterns-only     Show only extracted patterns
  -d, --deep              Deep analysis (slower, more thorough)
  -r, --refresh           Refresh cached connector data
```

**Analysis Output (JSON format):**
```json
{
  "connector": "Airtable",
  "structure": {
    "main_file": "Airtable.node.ts",
    "typescript_files": 12,
    "has_actions_directory": true,
    "has_methods_directory": true
  },
  "authentication": {
    "type": "apiKey"
  },
  "api": {
    "resources": "record,table,base",
    "operations": "Create,Get,Update,Delete,List",
    "supports_pagination": true,
    "supports_batch": true
  },
  "properties": {
    "types_used": "string (15),options (8),boolean (3)"
  }
}
```

### 4. `cache_connector.sh`
Cache frequently accessed connectors locally for faster offline access.

```bash
Commands:
  add <connector>         Cache a connector (all files)
  add-file <connector> <path>  Cache specific file
  list                    List cached connectors
  list-files <connector>  List cached files for a connector
  get <connector>         Get cached connector data
  remove <connector>      Remove connector from cache
  clean                   Remove expired cache entries
  clear                   Clear all cache
  status                  Show cache statistics
  sync                    Sync cached connectors with remote
```

### 5. `generate_connector_template.sh`
Generate a new connector template based on analyzed patterns.

```bash
Options:
  -b, --based-on <connector>   Base template on existing connector
  -o, --output <dir>           Output directory
  -t, --type <type>            Connector type: api, webhook, trigger
  -p, --properties             Generate properties only (stdout)
  -c, --copy-to-clipboard      Copy to clipboard
  -s, --save-pattern           Save as pattern for future use
```

**Generated Structure:**
```
n8n-nodes-my_service/
├── package.json
├── tsconfig.json
├── README.md
├── nodes/
│   └── MyService/
│       ├── MyService.node.ts
│       ├── GenericFunctions.ts
│       └── MyService.node.test.ts
└── credentials/
    └── MyServiceApi.credentials.ts
```

## GitHub API Access

### Authentication (Optional but Recommended)
For higher rate limits, set a GitHub token:
```bash
export GITHUB_TOKEN="your_github_token_here"
```

### Rate Limits
- Without token: 60 requests/hour
- With token: 5,000 requests/hour

## n8n Repository Structure

The n8n connectors are located at:
```
https://github.com/n8n-io/n8n/tree/main/packages/nodes-base/nodes/
```

Each connector typically contains:
- `ConnectorName.node.ts` - Main node implementation
- `GenericFunctions.ts` - API helper functions
- `actions/` - Action implementations (CRUD operations)
- `methods/` - Load option methods
- `test/` - Test files

## Caching Strategy

1. **API Response cache**: Recent API responses (24 hours default)
2. **Raw file cache**: Downloaded file contents (24 hours)
3. **Pattern cache**: Analyzed connector patterns (persistent)

Cache location: `~/.cache/filesurf-connectors/`

## Exit Codes

- 0: Success
- 1: Invalid arguments
- 2: GitHub API error
- 3: Connector not found
- 4: Network error
- 5: Cache error

## Dependencies

- `curl` for HTTP requests
- `jq` for JSON parsing
- `bash` 4.0+ for associative arrays

## Environment Variables

| Variable | Default | Description |
|----------|---------|-------------|
| `GITHUB_TOKEN` | (none) | GitHub API token for higher rate limits |
| `CACHE_DIR` | `~/.cache/filesurf-connectors` | Cache directory |
| `CACHE_TTL` | `86400` | Cache TTL in seconds (24 hours) |
| `N8N_REPO` | `n8n-io/n8n` | n8n repository |
| `ANALYSIS_DIR` | `$CACHE_DIR/analysis` | Analysis output directory |
| `PATTERNS_DIR` | `$CACHE_DIR/patterns` | Pattern storage directory |
| `OUTPUT_DIR` | `.` | Default output directory for generated connectors |

## Example: Creating a Todoist Connector

```bash
# 1. Check if similar connectors exist
./github_search_connectors.sh todo
# Output: Todoist, Trello, Asana, ...

# 2. Todoist exists! Let's analyze it
./analyze_connector_pattern.sh Todoist --deep --format json

# 3. But let's say we want to create a connector for a similar service
#    Let's analyze multiple task management connectors
./analyze_connector_pattern.sh Todoist Trello Asana --compare

# 4. Cache the most relevant one for reference
./cache_connector.sh add Todoist

# 5. Generate our new connector
./generate_connector_template.sh MyTaskApp --based-on Todoist --output ./connectors

# 6. Now customize the generated files...
cd ./connectors/n8n-nodes-my_task_app
# Edit nodes/MyTaskApp/MyTaskApp.node.ts
# Edit credentials/MyTaskAppApi.credentials.ts
```

## Security Notes

1. **No credentials in cache**: Only public code is cached
2. **Rate limiting**: Scripts respect GitHub API rate limits
3. **Local storage**: Cache is user-specific and isolated
4. **Cleanup**: Use `cache_connector.sh clean` to remove old entries

## Troubleshooting

### "Rate limit exceeded"
```bash
# Set a GitHub token
export GITHUB_TOKEN="ghp_..."
```

### "Connector not found"
```bash
# Search for the correct name
./github_search_connectors.sh --list-all | grep -i "partial-name"
```

### "Failed to get file list"
```bash
# Force refresh the cache
./github_browse_connector.sh ConnectorName --list -c
```
