#!/bin/bash

# Connector Template Generator
# Generate n8n connector templates based on analyzed patterns

set -euo pipefail

# Default configuration
CACHE_DIR="${CACHE_DIR:-$HOME/.cache/filesurf-connectors}"
PATTERNS_DIR="${PATTERNS_DIR:-$CACHE_DIR/patterns}"
OUTPUT_DIR="${OUTPUT_DIR:-.}"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
MAGENTA='\033[0;35m'
NC='\033[0m' # No Color

# Create directories
mkdir -p "$CACHE_DIR" "$PATTERNS_DIR"

# Function to print usage
usage() {
    cat << EOF
Usage: $0 <connector_name> [options]

Generate an n8n connector template based on analyzed patterns.

Arguments:
  connector_name     Name of the new connector (e.g., MyService, MyAPI)

Options:
  -b, --based-on <connector>   Base template on existing connector (default: Airtable)
  -o, --output <dir>           Output directory (default: current directory)
  -t, --type <type>            Connector type: api, webhook, trigger (default: api)
  -f, --format <format>        Output format: typescript, javascript, json (default: typescript)
  -p, --properties             Generate properties only (no full code)
  -c, --copy-to-clipboard      Copy generated template to clipboard
  -s, --save-pattern           Save as pattern for future use
  -r, --refresh                Refresh pattern cache before generating
  -h, --help                   Show this help message

Examples:
  $0 MyNewAPI
  $0 StripePayment --based-on PayPal --output ./generated
  $0 MyWebhook --type webhook
  $0 DataService --based-on Airtable --properties
  $0 --copy-to-clipboard CustomAPI --based-on Slack

Environment Variables:
  CACHE_DIR         Cache directory (default: ~/.cache/filesurf-connectors)
  OUTPUT_DIR        Default output directory (default: current directory)
EOF
    exit 1
}

# Function to get script directory
get_script_dir() {
    local script_path="${BASH_SOURCE[0]}"
    local script_dir
    script_dir=$(cd "$(dirname "$script_path")" && pwd)
    echo "$script_dir"
}

# Helper function to convert name to PascalCase
to_pascal_case() {
    echo "$1" | sed 's/[-_ ]/ /g' | awk '{for(i=1;i<=NF;i++)sub(/^./,toupper(substr($i,1,1)),$i)}1' | tr -d ' '
}

# Helper function to convert PascalCase to camelCase
to_camel_case() {
    local pascal="$1"
    echo "$pascal" | sed 's/^./\L&/'
}

# Helper function to convert PascalCase to snake_case
to_snake_case() {
    local pascal="$1"
    echo "$pascal" | sed 's/\([a-z0-9]\)\([A-Z]\)/\1_\2/g' | tr '[:upper:]' '[:lower:]'
}

# Function to get connector pattern
get_connector_pattern() {
    local connector="$1"
    local force_refresh="${2:-false}"
    
    local pattern_file="${PATTERNS_DIR}/${connector}.json"
    
    # Check if pattern exists
    if [ "$force_refresh" = "false" ] && [ -f "$pattern_file" ]; then
        cat "$pattern_file"
        return 0
    fi
    
    # Generate pattern using analyze script
    local script_dir
    script_dir=$(get_script_dir)
    
    if [ -f "${script_dir}/analyze_connector_pattern.sh" ]; then
        "${script_dir}/analyze_connector_pattern.sh" "$connector" --output "$pattern_file" --format json --refresh 2>/dev/null
        if [ -f "$pattern_file" ]; then
            cat "$pattern_file"
            return 0
        fi
    fi
    
    # Fallback to hardcoded patterns
    echo "null"
}

# Function to generate Node.ts template
generate_node_template() {
    local connector_name="$1"
    local base_connector="${2:-Airtable}"
    local connector_type="${3:-api}"
    
    local pascal_name
    pascal_name=$(to_pascal_case "$connector_name")
    
    local camel_name
    camel_name=$(to_camel_case "$pascal_name")
    
    local snake_name
    snake_name=$(to_snake_case "$pascal_name")
    
    local version="1"
    local date
    date=$(date +%Y-%m-%d)
    
    cat << EOF
/**
 * n8n node for ${pascal_name}
 * 
 * Generated template for ${connector_type} connector
 * Based on ${base_connector} patterns
 * Created: ${date}
 */

import {
	IExecuteFunctions,
	IDataObject,
	ILoadOptionsFunctions,
	INodeExecutionData,
	INodePropertyOptions,
	INodeType,
	INodeTypeDescription,
	NodeApiError,
	NodeOperationError,
} from 'n8n-workflow';

import {
	${camel_name}ApiRequest,
} from './GenericFunctions';

export class ${pascal_name} implements INodeType {
	description: INodeTypeDescription = {
		displayName: '${pascal_name}',
		name: '${camel_name}',
		icon: 'file:${snake_name}.svg',
		group: ['transform'],
		version: ${version},
		subtitle: '={{ \$parameter["operation"] + ": " + \$parameter["resource"] }}',
		description: 'Consume ${pascal_name} API',
		defaults: {
			name: '${pascal_name}',
		},
		inputs: ['main'],
		outputs: ['main'],
		credentials: [
			{
				name: '${camel_name}Api',
				required: true,
			},
		],
		properties: [
			{
				displayName: 'Resource',
				name: 'resource',
				type: 'options',
				noDataExpression: true,
				options: [
					{
						name: 'Record',
						value: 'record',
					},
				],
				default: 'record',
			},
			{
				displayName: 'Operation',
				name: 'operation',
				type: 'options',
				noDataExpression: true,
				displayOptions: {
					show: {
						resource: ['record'],
					},
				},
				options: [
					{
						name: 'Create',
						value: 'create',
						description: 'Create a new record',
						action: 'Create a record',
					},
					{
						name: 'Delete',
						value: 'delete',
						description: 'Delete a record',
						action: 'Delete a record',
					},
					{
						name: 'Get',
						value: 'get',
						description: 'Get a record',
						action: 'Get a record',
					},
					{
						name: 'Get Many',
						value: 'getMany',
						description: 'Get many records',
						action: 'Get many records',
					},
					{
						name: 'Update',
						value: 'update',
						description: 'Update a record',
						action: 'Update a record',
					},
				],
				default: 'get',
			},
			// Record ID field for get, update, delete operations
			{
				displayName: 'Record ID',
				name: 'recordId',
				type: 'string',
				displayOptions: {
					show: {
						resource: ['record'],
						operation: ['get', 'update', 'delete'],
					},
				},
				default: '',
				required: true,
				description: 'ID of the record',
			},
			// Fields for create/update
			{
				displayName: 'Fields',
				name: 'fields',
				type: 'fixedCollection',
				typeOptions: {
					multipleValues: true,
				},
				displayOptions: {
					show: {
						resource: ['record'],
						operation: ['create', 'update'],
					},
				},
				default: {},
				options: [
					{
						name: 'fieldValues',
						displayName: 'Field',
						values: [
							{
								displayName: 'Field Name',
								name: 'fieldName',
								type: 'string',
								default: '',
							},
							{
								displayName: 'Field Value',
								name: 'fieldValue',
								type: 'string',
								default: '',
							},
						],
					},
				],
			},
			// Options for getMany
			{
				displayName: 'Return All',
				name: 'returnAll',
				type: 'boolean',
				displayOptions: {
					show: {
						resource: ['record'],
						operation: ['getMany'],
					},
				},
				default: false,
				description: 'Whether to return all results or only up to a given limit',
			},
			{
				displayName: 'Limit',
				name: 'limit',
				type: 'number',
				displayOptions: {
					show: {
						resource: ['record'],
						operation: ['getMany'],
						returnAll: [false],
					},
				},
				typeOptions: {
					minValue: 1,
				},
				default: 50,
				description: 'Max number of results to return',
			},
		],
	};

	methods = {
		loadOptions: {
			// Dynamic option loaders can be added here
		},
	};

	async execute(this: IExecuteFunctions): Promise<INodeExecutionData[][]> {
		const items = this.getInputData();
		const returnData: INodeExecutionData[] = [];
		
		const resource = this.getNodeParameter('resource', 0) as string;
		const operation = this.getNodeParameter('operation', 0) as string;

		for (let i = 0; i < items.length; i++) {
			try {
				let responseData;

				if (resource === 'record') {
					if (operation === 'create') {
						const fields = this.getNodeParameter('fields.fieldValues', i, []) as Array<{
							fieldName: string;
							fieldValue: string;
						}>;
						
						const body: IDataObject = {};
						for (const field of fields) {
							body[field.fieldName] = field.fieldValue;
						}
						
						responseData = await ${camel_name}ApiRequest.call(this, 'POST', '/records', body);
					}
					
					if (operation === 'get') {
						const recordId = this.getNodeParameter('recordId', i) as string;
						responseData = await ${camel_name}ApiRequest.call(this, 'GET', \`/records/\${recordId}\`);
					}
					
					if (operation === 'getMany') {
						const returnAll = this.getNodeParameter('returnAll', i) as boolean;
						const limit = this.getNodeParameter('limit', i, 50) as number;
						
						if (returnAll) {
							// Implement pagination logic here
							responseData = await ${camel_name}ApiRequest.call(this, 'GET', '/records');
						} else {
							responseData = await ${camel_name}ApiRequest.call(this, 'GET', '/records', undefined, { limit });
						}
					}
					
					if (operation === 'update') {
						const recordId = this.getNodeParameter('recordId', i) as string;
						const fields = this.getNodeParameter('fields.fieldValues', i, []) as Array<{
							fieldName: string;
							fieldValue: string;
						}>;
						
						const body: IDataObject = {};
						for (const field of fields) {
							body[field.fieldName] = field.fieldValue;
						}
						
						responseData = await ${camel_name}ApiRequest.call(this, 'PATCH', \`/records/\${recordId}\`, body);
					}
					
					if (operation === 'delete') {
						const recordId = this.getNodeParameter('recordId', i) as string;
						responseData = await ${camel_name}ApiRequest.call(this, 'DELETE', \`/records/\${recordId}\`);
					}
				}

				const executionData = this.helpers.constructExecutionMetaData(
					this.helpers.returnJsonArray(responseData as IDataObject),
					{ itemData: { item: i } },
				);
				
				returnData.push(...executionData);
			} catch (error) {
				if (this.continueOnFail()) {
					returnData.push({ json: { error: (error as Error).message }, pairedItem: { item: i } });
					continue;
				}
				throw error;
			}
		}

		return [returnData];
	}
}
EOF
}

# Function to generate description template
generate_description_template() {
    local connector_name="$1"
    
    local pascal_name
    pascal_name=$(to_pascal_case "$connector_name")
    
    local camel_name
    camel_name=$(to_camel_case "$pascal_name")

    cat << EOF
import {
	INodeProperties,
} from 'n8n-workflow';

export const ${camel_name}Operations: INodeProperties[] = [
	{
		displayName: 'Resource',
		name: 'resource',
		type: 'options',
		noDataExpression: true,
		options: [
			{
				name: 'Record',
				value: 'record',
			},
		],
		default: 'record',
		description: 'Resource to consume',
	},
];

export const ${camel_name}Fields: INodeProperties[] = [
	/* Record operations */
	{
		displayName: 'Operation',
		name: 'operation',
		type: 'options',
		noDataExpression: true,
		displayOptions: {
			show: {
				resource: ['record'],
			},
		},
		options: [
			{
				name: 'Create',
				value: 'create',
				description: 'Create a new record',
				action: 'Create a record',
			},
			{
				name: 'Get',
				value: 'get',
				description: 'Get a record',
				action: 'Get a record',
			},
		],
		default: 'get',
	},
];
EOF
}

# Function to generate credentials template
generate_credentials_template() {
    local connector_name="$1"
    
    local pascal_name
    pascal_name=$(to_pascal_case "$connector_name")
    
    local camel_name
    camel_name=$(to_camel_case "$pascal_name")
    
    local snake_name
    snake_name=$(to_snake_case "$pascal_name")

    cat << EOF
import {
	IAuthenticateGeneric,
	ICredentialTestRequest,
	ICredentialType,
	INodeProperties,
} from 'n8n-workflow';

export class ${pascal_name}Api implements ICredentialType {
	name = '${camel_name}Api';
	displayName = '${pascal_name} API';
	documentationUrl = 'https://docs.n8n.io/integrations/builtin/credentials/${snake_name}api/';
	
	properties: INodeProperties[] = [
		{
			displayName: 'API Key',
			name: 'apiKey',
			type: 'string',
			typeOptions: {
				password: true,
			},
			default: '',
			required: true,
			description: 'Your ${pascal_name} API key',
		},
		{
			displayName: 'Base URL',
			name: 'baseUrl',
			type: 'string',
			default: 'https://api.example.com/v1',
			description: 'Base URL for the ${pascal_name} API',
		},
	];

	authenticate: IAuthenticateGeneric = {
		type: 'generic',
		properties: {
			headers: {
				Authorization: '=Bearer {{\$credentials.apiKey}}',
			},
		},
	};

	test: ICredentialTestRequest = {
		request: {
			baseURL: '={{\$credentials.baseUrl}}',
			url: '/me',
		},
	};
}
EOF
}

# Function to generate GenericFunctions.ts template
generate_generic_functions_template() {
    local connector_name="$1"
    
    local pascal_name
    pascal_name=$(to_pascal_case "$connector_name")
    
    local camel_name
    camel_name=$(to_camel_case "$pascal_name")

    cat << EOF
import type {
	IDataObject,
	IExecuteFunctions,
	IHookFunctions,
	ILoadOptionsFunctions,
	IHttpRequestMethods,
	IHttpRequestOptions,
	INodeExecutionData,
	JsonObject,
} from 'n8n-workflow';
import { NodeApiError } from 'n8n-workflow';

/**
 * Make an API request to ${pascal_name}
 */
export async function ${camel_name}ApiRequest(
	this: IExecuteFunctions | ILoadOptionsFunctions | IHookFunctions,
	method: IHttpRequestMethods,
	endpoint: string,
	body?: IDataObject,
	qs?: IDataObject,
): Promise<any> {
	const credentials = await this.getCredentials('${camel_name}Api');

	const options: IHttpRequestOptions = {
		method,
		url: \`\${credentials.baseUrl}\${endpoint}\`,
		headers: {
			'Content-Type': 'application/json',
		},
		qs,
		body,
		json: true,
	};

	try {
		if (Object.keys(body || {}).length === 0) {
			delete options.body;
		}
		return await this.helpers.httpRequestWithAuthentication.call(
			this,
			'${camel_name}Api',
			options,
		);
	} catch (error) {
		throw new NodeApiError(this.getNode(), error as JsonObject);
	}
}

/**
 * Make an API request and return all results (handles pagination)
 */
export async function ${camel_name}ApiRequestAllItems(
	this: IExecuteFunctions | ILoadOptionsFunctions,
	method: IHttpRequestMethods,
	endpoint: string,
	body: IDataObject = {},
	qs: IDataObject = {},
): Promise<any[]> {
	const returnData: any[] = [];
	let responseData;
	let nextCursor: string | undefined;

	do {
		if (nextCursor) {
			qs.cursor = nextCursor;
		}

		responseData = await ${camel_name}ApiRequest.call(this, method, endpoint, body, qs);

		// Adjust based on actual API response structure
		if (responseData.data) {
			returnData.push(...responseData.data);
		} else if (Array.isArray(responseData)) {
			returnData.push(...responseData);
		}

		nextCursor = responseData.next_cursor || responseData.nextCursor;
	} while (nextCursor);

	return returnData;
}
EOF
}

# Function to generate test file template
generate_test_template() {
    local connector_name="$1"
    
    local pascal_name
    pascal_name=$(to_pascal_case "$connector_name")
    
    local camel_name
    camel_name=$(to_camel_case "$pascal_name")

    cat << EOF
import { ${pascal_name} } from './${camel_name}.node';

describe('${pascal_name}', () => {
	describe('execute', () => {
		it('should be defined', () => {
			const node = new ${pascal_name}();
			expect(node).toBeDefined();
			expect(node.description).toBeDefined();
			expect(node.description.name).toBe('${camel_name}');
		});

		it('should have correct properties', () => {
			const node = new ${pascal_name}();
			expect(node.description.properties).toBeDefined();
			expect(node.description.properties.length).toBeGreaterThan(0);
		});

		// Add more test cases here
		// For example:
		// it('should create a record', async () => {
		//   // Mock implementation
		// });
	});
});
EOF
}

# Function to generate package.json
generate_package_template() {
    local connector_name="$1"
    
    local pascal_name
    pascal_name=$(to_pascal_case "$connector_name")
    
    local camel_name
    camel_name=$(to_camel_case "$pascal_name")
    
    local snake_name
    snake_name=$(to_snake_case "$pascal_name")
    
    local version="0.1.0"

    cat << EOF
{
	"name": "n8n-nodes-${snake_name}",
	"version": "${version}",
	"description": "n8n community node for ${pascal_name}",
	"keywords": [
		"n8n-community-node-package",
		"n8n",
		"${snake_name}",
		"${pascal_name}"
	],
	"license": "MIT",
	"homepage": "",
	"author": {
		"name": "Your Name",
		"email": "your.email@example.com"
	},
	"repository": {
		"type": "git",
		"url": "git+https://github.com/username/n8n-nodes-${snake_name}.git"
	},
	"main": "index.js",
	"scripts": {
		"build": "tsc && gulp build:icons",
		"dev": "tsc --watch",
		"format": "prettier nodes credentials --write",
		"lint": "eslint nodes credentials --ext .ts",
		"lintfix": "eslint nodes credentials --ext .ts --fix",
		"prepublishOnly": "npm run build && npm run lint -dependencies",
		"test": "jest"
	},
	"files": [
		"dist"
	],
	"n8n": {
		"n8nNodesApiVersion": 1,
		"credentials": [
			"dist/credentials/${pascal_name}Api.credentials.js"
		],
		"nodes": [
			"dist/nodes/${pascal_name}/${pascal_name}.node.js"
		]
	},
	"devDependencies": {
		"@types/jest": "^29.5.0",
		"@types/node": "^18.0.0",
		"@typescript-eslint/eslint-plugin": "^5.59.0",
		"@typescript-eslint/parser": "^5.59.0",
		"eslint": "^8.40.0",
		"eslint-plugin-n8n-nodes-base": "^1.11.0",
		"gulp": "^4.0.2",
		"jest": "^29.5.0",
		"n8n-workflow": "*",
		"prettier": "^2.8.8",
		"ts-jest": "^29.1.0",
		"typescript": "~5.0.0"
	},
	"peerDependencies": {
		"n8n-workflow": "*"
	}
}
EOF
}

# Function to generate tsconfig.json
generate_tsconfig_template() {
    cat << 'EOF'
{
	"compilerOptions": {
		"strict": true,
		"module": "commonjs",
		"target": "es2019",
		"lib": ["es2019"],
		"declaration": true,
		"skipLibCheck": true,
		"sourceMap": true,
		"outDir": "./dist",
		"rootDir": ".",
		"esModuleInterop": true,
		"resolveJsonModule": true,
		"forceConsistentCasingInFileNames": true
	},
	"include": [
		"nodes/**/*",
		"credentials/**/*"
	],
	"exclude": [
		"node_modules/**/*"
	]
}
EOF
}

# Function to generate README
generate_readme_template() {
    local connector_name="$1"
    
    local pascal_name
    pascal_name=$(to_pascal_case "$connector_name")
    
    local snake_name
    snake_name=$(to_snake_case "$pascal_name")

    cat << EOF
# n8n-nodes-${snake_name}

This is an n8n community node for [${pascal_name}](https://example.com).

[${pascal_name}](https://example.com) is a service that provides...

[n8n](https://n8n.io/) is a [fair-code licensed](https://docs.n8n.io/reference/license/) workflow automation platform.

## Installation

Follow the [installation guide](https://docs.n8n.io/integrations/community-nodes/installation/) in the n8n community nodes documentation.

## Operations

### Record
- **Create**: Create a new record
- **Delete**: Delete a record
- **Get**: Get a record by ID
- **Get Many**: Get multiple records
- **Update**: Update a record

## Credentials

To use this node, you need to configure the following credentials:

1. **API Key**: Your ${pascal_name} API key
2. **Base URL**: The base URL for the ${pascal_name} API (default: https://api.example.com/v1)

## Compatibility

Tested against n8n version 1.0+

## Usage

1. Add your ${pascal_name} credentials in n8n
2. Add the ${pascal_name} node to your workflow
3. Select the resource and operation
4. Configure the required parameters

## Resources

* [n8n community nodes documentation](https://docs.n8n.io/integrations/community-nodes/)
* [${pascal_name} API documentation](https://example.com/docs/api)

## License

[MIT](LICENSE.md)
EOF
}

# Function to generate full connector structure
generate_connector_structure() {
    local connector_name="$1"
    local base_connector="${2:-Airtable}"
    local connector_type="${3:-api}"
    local output_dir="$4"
    
    local pascal_name
    pascal_name=$(to_pascal_case "$connector_name")
    
    local camel_name
    camel_name=$(to_camel_case "$pascal_name")
    
    local snake_name
    snake_name=$(to_snake_case "$pascal_name")
    
    echo -e "${CYAN}Generating connector structure for ${pascal_name}...${NC}" >&2
    
    # Create output directory structure
    local connector_dir="${output_dir}/n8n-nodes-${snake_name}"
    mkdir -p "$connector_dir/nodes/${pascal_name}"
    mkdir -p "$connector_dir/credentials"
    
    # Generate files with progress
    echo -e "${GREEN}Creating ${pascal_name} directory structure:${NC}" >&2
    echo "  📁 n8n-nodes-${snake_name}/" >&2
    echo "    📄 package.json" >&2
    echo "    📄 tsconfig.json" >&2
    echo "    📄 README.md" >&2
    echo "    📁 nodes/${pascal_name}/" >&2
    echo "      📄 ${pascal_name}.node.ts" >&2
    echo "      📄 GenericFunctions.ts" >&2
    echo "    📁 credentials/" >&2
    echo "      📄 ${pascal_name}Api.credentials.ts" >&2
    
    # Generate package.json
    generate_package_template "$connector_name" > "${connector_dir}/package.json"
    
    # Generate tsconfig.json
    generate_tsconfig_template > "${connector_dir}/tsconfig.json"
    
    # Generate README
    generate_readme_template "$connector_name" > "${connector_dir}/README.md"
    
    # Generate main node file
    generate_node_template "$connector_name" "$base_connector" "$connector_type" > "${connector_dir}/nodes/${pascal_name}/${pascal_name}.node.ts"
    
    # Generate GenericFunctions
    generate_generic_functions_template "$connector_name" > "${connector_dir}/nodes/${pascal_name}/GenericFunctions.ts"
    
    # Generate credentials
    generate_credentials_template "$connector_name" > "${connector_dir}/credentials/${pascal_name}Api.credentials.ts"
    
    # Generate test file
    generate_test_template "$connector_name" > "${connector_dir}/nodes/${pascal_name}/${pascal_name}.node.test.ts"
    
    echo "" >&2
    echo -e "${GREEN}✓ Connector template generated successfully!${NC}" >&2
    echo "" >&2
    echo "Files generated in: ${connector_dir}" >&2
    echo "" >&2
    echo "Next steps:" >&2
    echo "  cd ${connector_dir}" >&2
    echo "  npm install" >&2
    echo "  npm run build" >&2
    echo "" >&2
    echo "Then link to n8n:" >&2
    echo "  npm link" >&2
    echo "  cd ~/.n8n" >&2
    echo "  npm link n8n-nodes-${snake_name}" >&2
    
    return 0
}

# Function to copy to clipboard
copy_to_clipboard() {
    local content="$1"
    
    if command -v xclip &> /dev/null; then
        echo "$content" | xclip -selection clipboard
        echo -e "${GREEN}Copied to clipboard!${NC}" >&2
    elif command -v pbcopy &> /dev/null; then
        echo "$content" | pbcopy
        echo -e "${GREEN}Copied to clipboard!${NC}" >&2
    elif command -v xsel &> /dev/null; then
        echo "$content" | xsel --clipboard
        echo -e "${GREEN}Copied to clipboard!${NC}" >&2
    else
        echo -e "${YELLOW}Clipboard not available. Outputting to stdout instead.${NC}" >&2
        echo "$content"
    fi
}

# Function to save pattern
save_pattern() {
    local connector_name="$1"
    local template="$2"
    
    local pattern_file="${PATTERNS_DIR}/${connector_name}.template.json"
    echo "$template" > "$pattern_file"
    echo -e "${GREEN}Pattern saved to: ${pattern_file}${NC}" >&2
}

# Main script logic
main() {
    if [ $# -eq 0 ]; then
        usage
    fi
    
    local connector_name=""
    local base_connector="Airtable"
    local connector_type="api"
    local output_format="typescript"
    local output_dir="$OUTPUT_DIR"
    local properties_only=false
    local copy_clipboard=false
    local save_pattern_flag=false
    local force_refresh=false
    
    # Parse arguments
    while [ $# -gt 0 ]; do
        case "$1" in
            -h|--help)
                usage
                ;;
            -b|--based-on)
                if [ -z "${2:-}" ]; then
                    echo -e "${RED}Error: --based-on requires a connector name${NC}" >&2
                    exit 1
                fi
                base_connector="$2"
                shift 2
                ;;
            -o|--output)
                if [ -z "${2:-}" ]; then
                    echo -e "${RED}Error: --output requires a directory${NC}" >&2
                    exit 1
                fi
                output_dir="$2"
                shift 2
                ;;
            -t|--type)
                if [ -z "${2:-}" ]; then
                    echo -e "${RED}Error: --type requires a type${NC}" >&2
                    exit 1
                fi
                connector_type="$2"
                shift 2
                ;;
            -f|--format)
                if [ -z "${2:-}" ]; then
                    echo -e "${RED}Error: --format requires a format${NC}" >&2
                    exit 1
                fi
                output_format="$2"
                shift 2
                ;;
            -p|--properties)
                properties_only=true
                shift
                ;;
            -c|--copy-to-clipboard)
                copy_clipboard=true
                shift
                ;;
            -s|--save-pattern)
                save_pattern_flag=true
                shift
                ;;
            -r|--refresh)
                force_refresh=true
                shift
                ;;
            -*)
                echo -e "${RED}Error: Unknown option: $1${NC}" >&2
                usage
                ;;
            *)
                if [ -z "$connector_name" ]; then
                    connector_name="$1"
                else
                    echo -e "${RED}Error: Multiple connector names specified${NC}" >&2
                    usage
                fi
                shift
                ;;
        esac
    done
    
    if [ -z "$connector_name" ]; then
        echo -e "${RED}Error: Connector name is required${NC}" >&2
        usage
    fi
    
    # Validate connector type
    case "$connector_type" in
        api|webhook|trigger)
            ;;
        *)
            echo -e "${RED}Error: Invalid connector type: $connector_type${NC}" >&2
            echo "Valid types: api, webhook, trigger" >&2
            exit 1
            ;;
    esac
    
    # Get base connector pattern (for future use)
    local base_pattern=""
    if [ "$base_connector" != "generic" ]; then
        base_pattern=$(get_connector_pattern "$base_connector" "$force_refresh")
        if [ "$base_pattern" = "null" ]; then
            echo -e "${YELLOW}Warning: Base connector pattern not found, using generic template${NC}" >&2
            base_connector="generic"
        fi
    fi
    
    # Generate template based on options
    if [ "$properties_only" = "true" ]; then
        # Generate only the main node template
        local template
        template=$(generate_node_template "$connector_name" "$base_connector" "$connector_type")
        
        if [ "$copy_clipboard" = "true" ]; then
            copy_to_clipboard "$template"
        elif [ "$save_pattern_flag" = "true" ]; then
            save_pattern "$connector_name" "$template"
        else
            echo "$template"
        fi
    else
        # Generate full connector structure
        generate_connector_structure "$connector_name" "$base_connector" "$connector_type" "$output_dir"
    fi
}

# Run main function
main "$@"
