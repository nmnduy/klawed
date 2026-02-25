//! Root test file for klawed Zig modules (Phase 2 + Phase 3 + Phase 4 + Phase 5 + Phase 6 + Phase 7).
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

// Pull all test blocks from every imported module into this compilation unit.
test {
    std.testing.refAllDecls(@This());
}
