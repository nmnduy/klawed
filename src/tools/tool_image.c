/*
 * tool_image.c - UploadImage tool implementation
 */

#include "tool_image.h"
#include "../klawed_internal.h"
#include "../base64.h"
#include "../util/file_utils.h"
#include "../logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <limits.h>
#include <ctype.h>
#include <sys/stat.h>
#include <fcntl.h>

cJSON* tool_upload_image(cJSON *params, ConversationState *state) {
    const cJSON *path_json = cJSON_GetObjectItem(params, "file_path");

    if (!path_json || !cJSON_IsString(path_json)) {
        cJSON *error = cJSON_CreateObject();
        cJSON_AddStringToObject(error, "error", "Missing 'file_path' parameter");
        return error;
    }

    // Check for interrupt before starting
    if (state && state->interrupt_requested) {
        cJSON *error = cJSON_CreateObject();
        cJSON_AddStringToObject(error, "error", "Operation interrupted by user");
        return error;
    }

    // Clean the path - remove newlines and trailing whitespace
    // Users might copy paths that include newlines
    char cleaned_path[PATH_MAX];
    const char *src_ptr = path_json->valuestring;
    char *dst_ptr = cleaned_path;
    size_t j = 0;

    while (*src_ptr && j < sizeof(cleaned_path) - 1) {
        if (*src_ptr != '\n' && *src_ptr != '\r') {
            *dst_ptr++ = *src_ptr;
            j++;
        }
        src_ptr++;
    }
    *dst_ptr = '\0';

    // Also trim trailing whitespace
    while (dst_ptr > cleaned_path && (*(dst_ptr-1) == ' ' || *(dst_ptr-1) == '\t')) {
        *(--dst_ptr) = '\0';
    }

    // Resolve the path relative to working directory
    if (!state) {
        cJSON *error = cJSON_CreateObject();
        cJSON_AddStringToObject(error, "error", "Internal error: null state");
        return error;
    }
    char *resolved_path = resolve_path(cleaned_path, state->working_dir);
    if (!resolved_path) {
        cJSON *error = cJSON_CreateObject();
        cJSON_AddStringToObject(error, "error", "Failed to resolve path");
        return error;
    }

    // Check if file exists and is readable
    if (access(resolved_path, R_OK) != 0) {
        cJSON *error = cJSON_CreateObject();
        char err_msg[256];
        snprintf(err_msg, sizeof(err_msg), "Cannot read image file '%s': %s",
                 resolved_path, strerror(errno));
        cJSON_AddStringToObject(error, "error", err_msg);
        free(resolved_path);
        return error;
    }

    // Check if this is a macOS temporary screenshot file
    // macOS temporary screenshots are in paths like:
    // /var/folders/xx/xxxxxxxxxxxxxxxxxxxx/T/TemporaryItems/NSIRD_screencaptureui_xxxxxx/
    int created_temp_copy = 0;
    char *temp_copy_path = NULL;
    char *path_to_read = resolved_path; // This will point to either original or temp copy

    if (strstr(resolved_path, "/var/folders/") == resolved_path ||
        strstr(resolved_path, "/private/var/folders/") == resolved_path) {
        // This is a macOS temporary file - create a copy in a safe location

        // Create a temporary filename in /tmp
        // Use mkstemp to create a secure temp file
        char template[PATH_MAX];
        snprintf(template, sizeof(template), "/tmp/klawed-upload-XXXXXX");

        int fd = mkstemp(template);
        if (fd == -1) {
            LOG_WARN("Failed to create temp file for macOS screenshot copy: %s", strerror(errno));
            // Continue with original path - it might work
        } else {
            close(fd);
            temp_copy_path = strdup(template);

            // Copy the file
            FILE *src = fopen(resolved_path, "rb");
            FILE *dst = fopen(temp_copy_path, "wb");

            if (src && dst) {
                char buffer[8192];
                size_t bytes;
                int copy_success = 1;

                while (!feof(src) && !ferror(src)) {
                    bytes = fread(buffer, 1, sizeof(buffer), src);
                    if (bytes > 0) {
                        if (fwrite(buffer, 1, bytes, dst) != bytes) {
                            copy_success = 0;
                            break;
                        }
                    }
                }

                // Check for read/write errors
                if (ferror(src) || ferror(dst)) {
                    copy_success = 0;
                }

                // Always close the streams
                fclose(src);
                fclose(dst);

                if (copy_success) {
                    LOG_DEBUG("Copied macOS temporary screenshot from '%s' to '%s'",
                             resolved_path, temp_copy_path);

                    // Use the temp copy for reading
                    path_to_read = temp_copy_path;
                    created_temp_copy = 1;
                } else {
                    LOG_WARN("Failed to copy macOS temporary screenshot");
                    unlink(temp_copy_path); // Clean up failed copy
                    free(temp_copy_path);
                    temp_copy_path = NULL;
                    // Continue with original path
                }
            } else {
                if (src) fclose(src);
                if (dst) fclose(dst);
                unlink(template); // Clean up failed copy
                free(temp_copy_path);
                temp_copy_path = NULL;
                LOG_WARN("Failed to copy macOS temporary screenshot");
                // Continue with original path
            }
        }
    }

    // Read the image file as binary
    FILE *f = fopen(path_to_read, "rb");
    if (!f) {
        cJSON *error = cJSON_CreateObject();
        char err_msg[256];
        snprintf(err_msg, sizeof(err_msg), "Failed to open image file '%s': %s",
                 path_to_read, strerror(errno));
        cJSON_AddStringToObject(error, "error", err_msg);
        free(resolved_path);
        if (temp_copy_path) {
            free(temp_copy_path);
        }
        return error;
    }

    // Get file size
    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (file_size <= 0) {
        fclose(f);
        free(resolved_path);
        if (temp_copy_path) {
            free(temp_copy_path);
        }
        cJSON *error = cJSON_CreateObject();
        cJSON_AddStringToObject(error, "error", "Image file is empty or invalid");
        return error;
    }

    // Read file content
    unsigned char *image_data = malloc((size_t)file_size);
    if (!image_data) {
        fclose(f);
        free(resolved_path);
        if (temp_copy_path) {
            free(temp_copy_path);
        }
        cJSON *error = cJSON_CreateObject();
        cJSON_AddStringToObject(error, "error", "Memory allocation failed");
        return error;
    }

    size_t bytes_read = fread(image_data, 1, (size_t)file_size, f);
    fclose(f);

    if (bytes_read != (size_t)file_size) {
        free(image_data);
        free(resolved_path);
        if (temp_copy_path) {
            free(temp_copy_path);
        }
        cJSON *error = cJSON_CreateObject();
        cJSON_AddStringToObject(error, "error", "Failed to read entire image file");
        return error;
    }

    // Determine MIME type from file extension first
    // Use the cleaned path for extension detection, not the temp copy
    const char *mime_type = "image/jpeg"; // default
    const char *ext = strrchr(cleaned_path, '.');
    if (!ext) {
        // Fall back to resolved path
        ext = strrchr(resolved_path, '.');
    }
    if (ext) {
        // Convert to lowercase for case-insensitive comparison
        char lower_ext[16];
        size_t ext_len = strlen(ext);
        if (ext_len < sizeof(lower_ext)) {
            for (size_t k = 0; k < ext_len; k++) {
                lower_ext[k] = (char)tolower((unsigned char)ext[k]);
            }
            lower_ext[ext_len] = '\0';

            if (strcmp(lower_ext, ".png") == 0) {
                mime_type = "image/png";
            } else if (strcmp(lower_ext, ".jpg") == 0 || strcmp(lower_ext, ".jpeg") == 0) {
                mime_type = "image/jpeg";
            } else if (strcmp(lower_ext, ".gif") == 0) {
                mime_type = "image/gif";
            } else if (strcmp(lower_ext, ".webp") == 0) {
                mime_type = "image/webp";
            } else if (strcmp(lower_ext, ".bmp") == 0) {
                mime_type = "image/bmp";
            } else if (strcmp(lower_ext, ".tiff") == 0 || strcmp(lower_ext, ".tif") == 0) {
                mime_type = "image/tiff";
            } else if (strcmp(lower_ext, ".svg") == 0) {
                mime_type = "image/svg+xml";
            }
        }
    }

    // Try to detect image type from magic numbers (file signatures)
    // This helps with temporary files that might have no extension or wrong extension
    if (file_size >= 8) {  // Need at least 8 bytes for most magic numbers
        unsigned char magic[8];
        memcpy(magic, image_data, 8);

        // PNG: \x89PNG\r\n\x1a\n
        if (magic[0] == 0x89 && magic[1] == 'P' && magic[2] == 'N' && magic[3] == 'G' &&
            magic[4] == 0x0D && magic[5] == 0x0A && magic[6] == 0x1A && magic[7] == 0x0A) {
            mime_type = "image/png";
        }
        // JPEG: \xff\xd8\xff
        else if (magic[0] == 0xFF && magic[1] == 0xD8 && magic[2] == 0xFF) {
            mime_type = "image/jpeg";
        }
        // GIF: "GIF87a" or "GIF89a"
        else if (magic[0] == 'G' && magic[1] == 'I' && magic[2] == 'F' && magic[3] == '8' &&
                (magic[4] == '7' || magic[4] == '9') && magic[5] == 'a') {
            mime_type = "image/gif";
        }
        // WebP: "RIFF" + "WEBP" (need to check more bytes)
        else if (file_size >= 12) {
            // Check first 4 bytes for "RIFF"
            if (magic[0] == 'R' && magic[1] == 'I' && magic[2] == 'F' && magic[3] == 'F') {
                // Check bytes 8-11 for "WEBP"
                if (image_data[8] == 'W' && image_data[9] == 'E' &&
                    image_data[10] == 'B' && image_data[11] == 'P') {
                    mime_type = "image/webp";
                }
            }
        }
        // BMP: "BM"
        else if (magic[0] == 'B' && magic[1] == 'M') {
            mime_type = "image/bmp";
        }
        // TIFF: "II" (little-endian) or "MM" (big-endian)
        else if ((magic[0] == 'I' && magic[1] == 'I') || (magic[0] == 'M' && magic[1] == 'M')) {
            mime_type = "image/tiff";
        }
    }

    // Base64 encode the image (must happen after magic number detection)
    size_t encoded_size = 0;
    char *base64_data = base64_encode(image_data, (size_t)file_size, &encoded_size);
    free(image_data);

    if (!base64_data) {
        free(resolved_path);
        if (temp_copy_path) {
            free(temp_copy_path);
        }
        cJSON *error = cJSON_CreateObject();
        cJSON_AddStringToObject(error, "error", "Failed to encode image as base64");
        return error;
    }

    // Create an image content block instead of a regular tool result
    // This will be handled specially in the message building logic
    cJSON *result = cJSON_CreateObject();
    cJSON_AddStringToObject(result, "status", "success");
    cJSON_AddStringToObject(result, "message", "Image uploaded successfully");
    // Store the original resolved path, not the temp copy path
    cJSON_AddStringToObject(result, "file_path", resolved_path);
    cJSON_AddStringToObject(result, "original_path", path_json->valuestring);
    cJSON_AddStringToObject(result, "mime_type", mime_type);
    cJSON_AddNumberToObject(result, "file_size_bytes", (double)file_size);
    cJSON_AddStringToObject(result, "base64_data", base64_data);
    cJSON_AddStringToObject(result, "content_type", "image"); // Special marker for image content

    free(base64_data);

    // Clean up temp file if we created one
    if (created_temp_copy && temp_copy_path) {
        LOG_DEBUG("Cleaning up temporary screenshot copy: %s", temp_copy_path);
        unlink(temp_copy_path);
    }

    // Free paths
    free(resolved_path);
    if (temp_copy_path) {
        free(temp_copy_path);
    }

    return result;
}

/**
 * tool_view_image - Views a local image from the filesystem
 *
 * This is similar to tool_upload_image but uses the "path" parameter name
 * to match Codex's view_image tool interface.
 */
cJSON* tool_view_image(cJSON *params, ConversationState *state) {
    const cJSON *path_json = cJSON_GetObjectItem(params, "path");

    if (!path_json || !cJSON_IsString(path_json)) {
        cJSON *error = cJSON_CreateObject();
        cJSON_AddStringToObject(error, "error", "Missing 'path' parameter");
        return error;
    }

    // Check for interrupt before starting
    if (state && state->interrupt_requested) {
        cJSON *error = cJSON_CreateObject();
        cJSON_AddStringToObject(error, "error", "Operation interrupted by user");
        return error;
    }

    // Resolve the path relative to working directory
    if (!state) {
        cJSON *error = cJSON_CreateObject();
        cJSON_AddStringToObject(error, "error", "Internal error: null state");
        return error;
    }
    char *resolved_path = resolve_path(path_json->valuestring, state->working_dir);
    if (!resolved_path) {
        cJSON *error = cJSON_CreateObject();
        cJSON_AddStringToObject(error, "error", "Failed to resolve path");
        return error;
    }

    // Check if file exists and is readable
    if (access(resolved_path, R_OK) != 0) {
        cJSON *error = cJSON_CreateObject();
        char err_msg[256];
        snprintf(err_msg, sizeof(err_msg), "Cannot read image file '%s': %s",
                 resolved_path, strerror(errno));
        cJSON_AddStringToObject(error, "error", err_msg);
        free(resolved_path);
        return error;
    }

    // Read the file
    unsigned char *image_data = NULL;
    off_t file_size = 0;

    int fd = open(resolved_path, O_RDONLY);
    if (fd < 0) {
        cJSON *error = cJSON_CreateObject();
        char err_msg[256];
        snprintf(err_msg, sizeof(err_msg), "Failed to open image file '%s': %s",
                 resolved_path, strerror(errno));
        cJSON_AddStringToObject(error, "error", err_msg);
        free(resolved_path);
        return error;
    }

    // Get file size
    struct stat st;
    if (fstat(fd, &st) != 0) {
        close(fd);
        cJSON *error = cJSON_CreateObject();
        char err_msg[256];
        snprintf(err_msg, sizeof(err_msg), "Failed to get file size for '%s': %s",
                 resolved_path, strerror(errno));
        cJSON_AddStringToObject(error, "error", err_msg);
        free(resolved_path);
        return error;
    }
    file_size = st.st_size;

    // Check file size limit (20MB for view_image)
    if (file_size > 20 * 1024 * 1024) {
        close(fd);
        cJSON *error = cJSON_CreateObject();
        cJSON_AddStringToObject(error, "error", "Image file too large (max 20MB)");
        free(resolved_path);
        return error;
    }

    // Read file content
    image_data = (unsigned char *)malloc((size_t)file_size);
    if (!image_data) {
        close(fd);
        cJSON *error = cJSON_CreateObject();
        cJSON_AddStringToObject(error, "error", "Failed to allocate memory for image");
        free(resolved_path);
        return error;
    }

    ssize_t bytes_read = read(fd, image_data, (size_t)file_size);
    close(fd);

    if (bytes_read != file_size) {
        free(image_data);
        cJSON *error = cJSON_CreateObject();
        cJSON_AddStringToObject(error, "error", "Failed to read image file");
        free(resolved_path);
        return error;
    }

    // Detect MIME type from extension
    const char *mime_type = "application/octet-stream";
    const char *ext = strrchr(resolved_path, '.');
    if (ext) {
        char lower_ext[16];
        size_t ext_len = strlen(ext);
        if (ext_len < sizeof(lower_ext)) {
            for (size_t i = 0; i <= ext_len; i++) {
                lower_ext[i] = (char)tolower((unsigned char)ext[i]);
            }

            if (strcmp(lower_ext, ".png") == 0) {
                mime_type = "image/png";
            } else if (strcmp(lower_ext, ".jpg") == 0 || strcmp(lower_ext, ".jpeg") == 0) {
                mime_type = "image/jpeg";
            } else if (strcmp(lower_ext, ".gif") == 0) {
                mime_type = "image/gif";
            } else if (strcmp(lower_ext, ".webp") == 0) {
                mime_type = "image/webp";
            } else if (strcmp(lower_ext, ".bmp") == 0) {
                mime_type = "image/bmp";
            } else if (strcmp(lower_ext, ".tiff") == 0 || strcmp(lower_ext, ".tif") == 0) {
                mime_type = "image/tiff";
            } else if (strcmp(lower_ext, ".svg") == 0) {
                mime_type = "image/svg+xml";
            }
        }
    }

    // Try to detect image type from magic numbers
    if (file_size >= 8) {
        unsigned char magic[8];
        memcpy(magic, image_data, 8);

        // PNG
        if (magic[0] == 0x89 && magic[1] == 'P' && magic[2] == 'N' && magic[3] == 'G' &&
            magic[4] == 0x0D && magic[5] == 0x0A && magic[6] == 0x1A && magic[7] == 0x0A) {
            mime_type = "image/png";
        }
        // JPEG
        else if (magic[0] == 0xFF && magic[1] == 0xD8 && magic[2] == 0xFF) {
            mime_type = "image/jpeg";
        }
        // GIF
        else if (magic[0] == 'G' && magic[1] == 'I' && magic[2] == 'F' && magic[3] == '8' &&
                (magic[4] == '7' || magic[4] == '9') && magic[5] == 'a') {
            mime_type = "image/gif";
        }
        // WebP
        else if (file_size >= 12) {
            if (magic[0] == 'R' && magic[1] == 'I' && magic[2] == 'F' && magic[3] == 'F') {
                if (image_data[8] == 'W' && image_data[9] == 'E' &&
                    image_data[10] == 'B' && image_data[11] == 'P') {
                    mime_type = "image/webp";
                }
            }
        }
        // BMP
        else if (magic[0] == 'B' && magic[1] == 'M') {
            mime_type = "image/bmp";
        }
        // TIFF
        else if ((magic[0] == 'I' && magic[1] == 'I') || (magic[0] == 'M' && magic[1] == 'M')) {
            mime_type = "image/tiff";
        }
    }

    // Base64 encode the image
    size_t encoded_size = 0;
    char *base64_data = base64_encode(image_data, (size_t)file_size, &encoded_size);
    free(image_data);

    if (!base64_data) {
        free(resolved_path);
        cJSON *error = cJSON_CreateObject();
        cJSON_AddStringToObject(error, "error", "Failed to encode image as base64");
        return error;
    }

    // Create result with image content
    cJSON *result = cJSON_CreateObject();
    cJSON_AddStringToObject(result, "status", "success");
    cJSON_AddStringToObject(result, "file_path", resolved_path);
    cJSON_AddStringToObject(result, "mime_type", mime_type);
    cJSON_AddNumberToObject(result, "file_size_bytes", (double)file_size);
    cJSON_AddStringToObject(result, "base64_data", base64_data);
    cJSON_AddStringToObject(result, "content_type", "image");

    free(base64_data);
    free(resolved_path);

    return result;
}
