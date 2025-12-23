# TODO: ZMQ Input/Output Protocol Updates

## Overview
This document outlines the changes needed to update the ZMQ socket protocol to match the SQLite queue protocol changes.

## Changes Required

### 1. Remove COMPLETED Message Type
**File**: `src/zmq_socket.c`
**Location**: Line ~490
**Current code**:
```c
snprintf(response, sizeof(response), 
        "{\"messageType\": \"COMPLETED\", \"content\": \"Interactive processing completed successfully\"}");
```
**Change to**:
```c
// No completion message - client detects completion when no pending TOOL messages
// without corresponding TOOL_RESULT messages
response[0] = '\0';  // Empty response
```

### 2. Add TOOL Message Type
**File**: `src/zmq_socket.c`
**Location**: After `zmq_send_tool_result` function (around line 620)

**New function to add**:
```c
// Helper function to send a tool execution request
static int zmq_send_tool_request(ZMQContext *ctx, const char *tool_name, const char *tool_id,
                                 cJSON *tool_parameters) {
#ifdef HAVE_ZMQ
    if (!ctx || !tool_name || !tool_id) {
        LOG_ERROR("ZMQ: Invalid parameters for send_tool_request");
        return -1;
    }
    
    cJSON *request_json = cJSON_CreateObject();
    if (!request_json) {
        LOG_ERROR("ZMQ: Failed to create tool request JSON object");
        return -1;
    }
    
    cJSON_AddStringToObject(request_json, "messageType", "TOOL");
    cJSON_AddStringToObject(request_json, "toolName", tool_name);
    cJSON_AddStringToObject(request_json, "toolId", tool_id);
    
    if (tool_parameters) {
        cJSON_AddItemToObject(request_json, "toolParameters", cJSON_Duplicate(tool_parameters, 1));
    } else {
        cJSON_AddNullToObject(request_json, "toolParameters");
    }
    
    char *request_str = cJSON_PrintUnformatted(request_json);
    if (!request_str) {
        LOG_ERROR("ZMQ: Failed to serialize tool request JSON");
        cJSON_Delete(request_json);
        return -1;
    }
    
    LOG_INFO("ZMQ: Sending TOOL request for %s (id: %s)", tool_name, tool_id);
    int result = zmq_socket_send(ctx, request_str, strlen(request_str));
    free(request_str);
    cJSON_Delete(request_json);
    
    return result;
#else
    (void)ctx;
    (void)tool_name;
    (void)tool_id;
    (void)tool_parameters;
    return -1;
#endif
}
```

### 3. Update Tool Execution Logic
**File**: `src/zmq_socket.c`
**Location**: In `zmq_process_interactive` function (around line 965)

**Current code**:
```c
// Execute tool synchronously
cJSON *tool_result = execute_tool(tool->name, input, state);

// Send tool result response
zmq_send_tool_result(ctx, tool->name, tool->id, tool_result, 0);
```

**Change to**:
```c
// Send TOOL request message before execution
zmq_send_tool_request(ctx, tool->name, tool->id, input);

// Execute tool synchronously
cJSON *tool_result = execute_tool(tool->name, input, state);

// Send tool result response
zmq_send_tool_result(ctx, tool->name, tool->id, tool_result, 0);
```

### 4. Update Error Handling for Tool Validation
**File**: `src/zmq_socket.c`
**Location**: In `zmq_process_interactive` function (around line 950)

**Current code**:
```c
// Send error response
zmq_send_tool_result(ctx, tool->name, tool->id, results[i].tool_output, 1);
continue;
```

**Change to**:
```c
// Send TOOL request message (even though it will fail)
zmq_send_tool_request(ctx, tool->name, tool->id, NULL);

// Send error response
zmq_send_tool_result(ctx, tool->name, tool->id, results[i].tool_output, 1);
continue;
```

### 5. Update ZMQ Documentation
**File**: `docs/zmq_input_output.md`
**Changes needed**:
1. Remove COMPLETED message type from message types table
2. Add TOOL message type to message types table
3. Update message format examples
4. Update client code examples to handle TOOL messages
5. Update completion detection logic documentation

### 6. Update Client Examples
**Files**:
- `examples/zmq_interactive_example.c`
- Any other ZMQ client examples

**Changes needed**:
1. Remove COMPLETED message handling
2. Add TOOL message handling
3. Update completion detection logic to track pending TOOL messages

## Testing Requirements

### Unit Tests
1. Test that COMPLETED messages are no longer sent
2. Test that TOOL messages are sent before tool execution
3. Test that TOOL_RESULT messages are still sent after tool execution
4. Test error handling with invalid tools

### Integration Tests
1. Test with existing ZMQ clients to ensure backward compatibility (may break)
2. Test new completion detection logic

## Notes

1. **Backward Compatibility**: These changes will break existing ZMQ clients that rely on COMPLETED messages. Clients need to be updated to use the new completion detection logic.

2. **Completion Detection**: Clients must track pending TOOL messages and consider processing complete when all TOOL messages have corresponding TOOL_RESULT messages.

3. **Consistency**: These changes bring ZMQ protocol in line with SQLite queue protocol.

4. **Error Cases**: Ensure TOOL messages are sent even for failed tool validations so clients can track all tool attempts.

## Implementation Priority
1. Update ZMQ socket implementation (src/zmq_socket.c)
2. Update ZMQ documentation (docs/zmq_input_output.md)
3. Update client examples
4. Run tests to verify changes
5. Update any dependent code or documentation