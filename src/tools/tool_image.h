/*
 * tool_image.h - UploadImage tool implementation
 */

#ifndef TOOL_IMAGE_H
#define TOOL_IMAGE_H

#include <cjson/cJSON.h>

// Forward declaration of ConversationState
typedef struct ConversationState ConversationState;

/**
 * tool_upload_image - Uploads an image file to be included in the conversation context
 *
 * @param params JSON object with: { "file_path": <path> }
 * @param state Conversation state containing working_dir and interrupt flag
 * @return JSON object with status, file info, mime type, and base64 encoded image data
 */
cJSON* tool_upload_image(cJSON *params, ConversationState *state);

#endif // TOOL_IMAGE_H
