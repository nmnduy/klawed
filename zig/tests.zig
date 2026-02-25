//! Root test file for klawed Zig modules (Phase 2–8).
//!
//! Running `zig build test` compiles this file and executes all `test` blocks
//! found in each imported module.  `std.testing.refAllDecls` is used to also
//! run tests on any public declarations in scope.

const std = @import("std");

// Utility leaf modules (Phase 2)
pub const string_utils = @import("util/string_utils.zig");
pub const timestamp_utils = @import("util/timestamp_utils.zig");
pub const format_utils = @import("util/format_utils.zig");
pub const env_utils = @import("util/env_utils.zig");
pub const file_utils = @import("util/file_utils.zig");
pub const diff_utils = @import("util/diff_utils.zig");
pub const output_utils = @import("util/output_utils.zig");
pub const arena = @import("util/arena.zig");
pub const array_list = @import("util/array_list.zig");
pub const base64 = @import("base64.zig");
pub const logger = @import("logger.zig");
pub const version_mod = @import("version.zig");

// Data & persistence layer (Phase 3)
pub const sqlite = @import("sqlite.zig");
pub const data_dir = @import("data_dir.zig");
pub const history_file = @import("history_file.zig");
pub const migrations = @import("migrations.zig");
pub const token_usage_db = @import("token_usage_db.zig");
pub const persistence = @import("persistence.zig");
pub const sqlite_queue = @import("sqlite_queue.zig");
pub const memory_db = @import("memory_db.zig");
pub const session = @import("session.zig");

// Configuration & providers (Phase 4)
pub const config = @import("config.zig");
pub const provider_config_loader = @import("provider_config_loader.zig");
pub const provider = @import("provider.zig");
pub const openai_provider = @import("providers/openai.zig");
pub const anthropic_provider = @import("providers/anthropic.zig");
pub const bedrock_provider = @import("providers/bedrock.zig");
pub const deepseek_provider = @import("providers/deepseek.zig");
pub const moonshot_provider = @import("providers/moonshot.zig");
pub const kimi_provider = @import("providers/kimi.zig");

// HTTP & Streaming (Phase 5)
pub const retry_logic = @import("retry_logic.zig");
pub const http_client = @import("http_client.zig");
pub const sse_parser = @import("api/sse_parser.zig");
pub const api_response = @import("api/api_response.zig");
pub const api_client = @import("api/api_client.zig");

// Conversation & Context (Phase 6)
pub const content_types = @import("conversation/content_types.zig");
pub const conversation_state = @import("conversation/state.zig");
pub const conversation_message = @import("conversation/message.zig");
pub const conversation_processor = @import("conversation/processor.zig");
pub const context_environment = @import("context/environment.zig");
pub const context_klawed_md = @import("context/klawed_md.zig");
pub const context_system_prompt = @import("context/system_prompt.zig");
pub const context_memory_injection = @import("context/memory_injection.zig");
pub const compaction = @import("compaction.zig");

// Tools (Phase 7)
pub const tool_utils = @import("tools/utils.zig");
pub const tool_sleep = @import("tools/sleep.zig");
pub const tool_bash = @import("tools/bash.zig");
pub const tool_filesystem = @import("tools/filesystem.zig");
pub const tool_search = @import("tools/search.zig");
pub const tool_todo = @import("tools/todo.zig");
pub const tool_subagent = @import("tools/subagent.zig");
pub const tool_image = @import("tools/image.zig");
pub const tool_dynamic = @import("tools/dynamic.zig");
pub const tool_registry = @import("tools/registry.zig");
pub const subagent_manager = @import("subagent_manager.zig");
pub const explore_tools = @import("explore_tools.zig");
pub const mcp = @import("mcp.zig");

// Agent core & entry point (Phase 8)
pub const message_queue = @import("message_queue.zig");
pub const ai_worker = @import("ai_worker.zig");
pub const completion = @import("completion.zig");
pub const dump_utils = @import("dump_utils.zig");
pub const process_utils = @import("process_utils.zig");
pub const commands = @import("commands.zig");
pub const interactive_input_handler = @import("interactive/input_handler.zig");
pub const interactive_command_dispatch = @import("interactive/command_dispatch.zig");
pub const interactive_response_processor = @import("interactive/response_processor.zig");
pub const interactive_loop = @import("interactive/interactive_loop.zig");
pub const oneshot_output = @import("oneshot/output.zig");
pub const oneshot_processor = @import("oneshot/processor.zig");
pub const oneshot_mode = @import("oneshot/mode.zig");
pub const websocket = @import("websocket.zig");
pub const websocket_mode = @import("websocket_mode.zig");
pub const main_mod = @import("main.zig");

// TUI modules (Phase 9)
pub const tui_input = @import("tui/input.zig");
pub const tui_core = @import("tui/core.zig");

// Ported C test suites (Phase 10)
pub const test_base64 = @import("tests/test_base64.zig");
pub const test_todo = @import("tests/test_todo.zig");
pub const test_config = @import("tests/test_config.zig");
pub const test_data_dir = @import("tests/test_data_dir.zig");
pub const test_dump_utils = @import("tests/test_dump_utils.zig");
pub const test_compaction = @import("tests/test_compaction.zig");
pub const test_edit = @import("tests/test_edit.zig");
pub const test_diff_colors = @import("tests/test_diff_colors.zig");
// Phase 10 — bash / filesystem tools
pub const test_bash_timeout = @import("tests/test_bash_timeout.zig");
pub const test_bash_stderr = @import("tests/test_bash_stderr.zig");
pub const test_bash_truncation = @import("tests/test_bash_truncation.zig");
pub const test_read = @import("tests/test_read.zig");
pub const test_edit_diff_integration = @import("tests/test_edit_diff_integration.zig");
pub const test_edit_regex_enhancements = @import("tests/test_edit_regex_enhancements.zig");
pub const test_write_diff_integration = @import("tests/test_write_diff_integration.zig");
pub const test_utf8_truncate = @import("tests/test_utf8_truncate.zig");
pub const test_text_wrap = @import("tests/test_text_wrap.zig");
pub const test_spacing_simple = @import("tests/test_spacing_simple.zig");
// Phase 10 — persistence / database
pub const test_memory_db = @import("tests/test_memory_db.zig");
pub const test_memory_null_fix = @import("tests/test_memory_null_fix.zig");
pub const test_memory_retract = @import("tests/test_memory_retract.zig");
pub const test_sqlite_queue = @import("tests/test_sqlite_queue.zig");
pub const test_sqlite_queue_threading = @import("tests/test_sqlite_queue_threading.zig");
pub const test_history_file = @import("tests/test_history_file.zig");
pub const test_token_usage = @import("tests/test_token_usage.zig");
pub const test_token_usage_comprehensive = @import("tests/test_token_usage_comprehensive.zig");
pub const test_token_usage_session_totals = @import("tests/test_token_usage_session_totals.zig");
pub const test_rotation = @import("tests/test_rotation.zig");
// Phase 10 — providers / API
pub const test_openai_format = @import("tests/test_openai_format.zig");
pub const test_openai_response_parsing = @import("tests/test_openai_response_parsing.zig");
pub const test_openai_responses = @import("tests/test_openai_responses.zig");
pub const test_bedrock_auth = @import("tests/test_bedrock_auth.zig");
pub const test_bedrock_converse = @import("tests/test_bedrock_converse.zig");
pub const test_provider_init = @import("tests/test_provider_init.zig");
pub const test_provider_init_from_config = @import("tests/test_provider_init_from_config.zig");
pub const test_retry_jitter = @import("tests/test_retry_jitter.zig");
pub const test_json_parsing = @import("tests/test_json_parsing.zig");
pub const test_aws_credential_rotation = @import("tests/test_aws_credential_rotation.zig");
// Phase 10 — agent core / conversation
pub const test_message_queue = @import("tests/test_message_queue.zig");
pub const test_conversation_free = @import("tests/test_conversation_free.zig");
pub const test_tool_message_ordering = @import("tests/test_tool_message_ordering.zig");
pub const test_tool_results_regression = @import("tests/test_tool_results_regression.zig");
pub const test_insert_system_message = @import("tests/test_insert_system_message.zig");
pub const test_mcp = @import("tests/test_mcp.zig");
// Phase 10 — TUI / window manager
pub const test_window_manager = @import("tests/test_window_manager.zig");
pub const test_window_manager_border_calculations = @import("tests/test_window_manager_border_calculations.zig");
pub const test_tui_auto_scroll = @import("tests/test_tui_auto_scroll.zig");
pub const test_tui_scrolling_calculations = @import("tests/test_tui_scrolling_calculations.zig");
pub const test_tui_input_buffer = @import("tests/test_tui_input_buffer.zig");
pub const test_file_search = @import("tests/test_file_search.zig");
pub const test_colorscheme = @import("tests/test_colorscheme.zig");
// Phase 10 — tool validation / misc
pub const test_tool_definition_parity = @import("tests/test_tool_definition_parity.zig");
pub const test_tool_details_simple = @import("tests/test_tool_details_simple.zig");

// Pull all test blocks from every imported module into this compilation unit.
test {
    std.testing.refAllDecls(@This());
}
