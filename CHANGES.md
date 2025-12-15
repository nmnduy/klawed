# Improved Error Messages for API Failures

## Problem
When the backend returns a 429 error (or other HTTP errors), the user only sees generic messages like:
- "mcp tool call failed" 
- "Maximum retry duration exceeded"

This doesn't help the user understand what actually went wrong (e.g., rate limiting, specific API errors).

## Solution

### 1. API Retry Error Messages (src/claude.c)

**Before:**
- When retries are exhausted: "Maximum retry duration exceeded" (no context)
- When MCP tool fails: "MCP tool call failed" (no details)

**After:**
- Track last error message and HTTP status during retries
- Show detailed message when retries exhausted:
  - "Maximum retry duration exceeded. Last error: Too many tokens, please wait before trying again. (HTTP 429)"
- Better context for debugging and user understanding

**Changes:**
- Added `last_error` and `last_http_status` tracking in retry loop
- Updated two locations where timeout is detected to include error details
- Properly free allocated error strings on all exit paths

### 2. MCP Tool Error Propagation (src/mcp.c)

**Before:**
- `mcp_call_tool()` returns NULL on error (loses error context)
- Caller sees NULL and shows generic "MCP tool call failed"

**After:**
- `mcp_call_tool()` returns `MCPToolResult` with `is_error=1` and detailed message
- Error messages include:
  - "MCP: Invalid parameters (server not connected or no tool name)"
  - "MCP: No response from server (timeout or connection error)"
  - "MCP: Invalid response from server (no result field)"
  - "MCP: Memory allocation failed"

**Changes:**
- Updated `mcp_call_tool()` to always return MCPToolResult (never NULL)
- Set `is_error=1` and populate `result` field with error message
- Updated `tool_call_mcp_tool()` in claude.c to handle NULL check differently

### 3. Tool Handler Updates (src/claude.c)

**Before:**
```c
if (!call_result) {
    cJSON_AddStringToObject(result, "error", "MCP tool call failed");
    return result;
}
```

**After:**
```c
if (!call_result) {
    cJSON_AddStringToObject(result, "error", "MCP tool call failed: memory allocation error");
    return result;
}
if (call_result->is_error) {
    // Include the detailed error message
    cJSON_AddStringToObject(result, "error", call_result->result ? call_result->result : "MCP tool error");
}
```

## Example Error Messages

### Rate Limit (429)
**Before:** "Maximum retry duration exceeded"  
**After:** "Maximum retry duration exceeded. Last error: Too many tokens, please wait before trying again. (HTTP 429)"

### MCP Server Timeout
**Before:** "MCP tool call failed"  
**After:** "MCP: No response from server (timeout or connection error)"

### MCP Invalid Response
**Before:** "MCP tool call failed"  
**After:** "MCP: Invalid response from server (no result field)"

## Testing

All existing tests pass. The changes are backward compatible:
- Error handling paths updated to include more context
- Memory management properly handled (free on all paths)
- No functional behavior changes, only improved messaging

Build: ✓ Clean build with -Werror
Tests: ✓ All test suites pass
