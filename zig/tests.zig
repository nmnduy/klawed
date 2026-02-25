//! Root test file for klawed Zig modules (Phase 2 + Phase 3 + Phase 4).
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

// Pull all test blocks from every imported module into this compilation unit.
test {
    std.testing.refAllDecls(@This());
}
