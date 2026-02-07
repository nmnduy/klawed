#ifndef ONESHOT_OUTPUT_H
#define ONESHOT_OUTPUT_H

#include <cjson/cJSON.h>

/**
 * Oneshot Output Formatting
 *
 * Functions for formatting and printing tool execution results in oneshot mode.
 * Supports both human-readable and machine-readable (HTML+JSON) output formats.
 */

/**
 * Output format modes for oneshot execution
 */
typedef enum {
    ONESHOT_FORMAT_HUMAN = 0,    // Human-readable output (default)
    ONESHOT_FORMAT_MACHINE = 1   // Machine-readable HTML+JSON output
} OneshotFormat;

/**
 * Parse output format from environment variable
 * Checks KLAWED_ONESHOT_FORMAT environment variable
 * @return Parsed format, defaults to ONESHOT_FORMAT_HUMAN if invalid/unset
 */
OneshotFormat oneshot_get_output_format(void);

/**
 * Print tool execution in machine-readable format (HTML+JSON)
 * Outputs: <tool name="..." details="...">JSON</tool>
 * @param tool_name Name of the executed tool
 * @param tool_details Optional details string (can be NULL or empty)
 * @param tool_result JSON result from tool execution (can be NULL)
 */
void oneshot_print_machine_format(const char *tool_name,
                                   const char *tool_details,
                                   cJSON *tool_result);

/**
 * Print tool execution in human-readable format
 * Wrapper around print_human_readable_tool_output from ui/tool_output_display.h
 * @param tool_name Name of the executed tool
 * @param tool_details Optional details string (can be NULL or empty)
 * @param tool_result JSON result from tool execution (can be NULL)
 */
void oneshot_print_human_format(const char *tool_name,
                                 const char *tool_details,
                                 cJSON *tool_result);

/**
 * Get current timestamp as a formatted string
 * Format: "YYYY-MM-DD HH:MM:SS"
 * @param buffer Output buffer for timestamp
 * @param buffer_size Size of output buffer
 */
void oneshot_get_timestamp(char *buffer, size_t buffer_size);

/**
 * Print tool output formatted based on tool type
 * Helper function for formatting different tool results
 * @param tool_name Name of the tool
 * @param tool_result JSON result from tool
 * @param summary Output buffer for summary text
 * @param summary_size Size of summary buffer
 * @param is_compact Use compact output style
 */
void print_tool_output_formatted(const char *tool_name, cJSON *tool_result,
                                  char *summary, size_t summary_size, int is_compact);

#endif // ONESHOT_OUTPUT_H
