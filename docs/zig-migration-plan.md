# Zig Migration Plan

> **Status**: Phase 8 complete — Phase 9 next (TUI)  
> **Scope**: Full rewrite of klawed from C11 to Zig  
> **Estimated effort**: Large (months of focused engineering)  
> **Primary motivation**: Eliminate manual memory management, leverage comptime type safety, and replace the growing `libbsd` band-aids with first-class language guarantees.

---

## Why Zig?

| Pain point in current C code | Zig equivalent |
|---|---|
| `malloc`/`free` mismatches, use-after-free | Explicit allocator threading + `defer` cleanup |
| `strlcpy`/`strlcat` wrappers to avoid buffer overflows | Slice-bounded `[]u8` — no null-terminator bugs |
| Manual arena allocator (`src/arena.h`, ~767 lines) | Built-in `std.heap.ArenaAllocator` |
| `reallocarray` to avoid integer overflow in resizes | `ArrayList(T).append` with safe growth |
| Unchecked error codes from every POSIX call | Mandatory error unions (`!T`) enforced at compile time |
| `#ifdef` soup for macOS vs Linux | Comptime `builtin.os.tag` branches |
| `cJSON` dependency for all JSON parsing | `std.json` in stdlib (or `zzon` for streaming) |
| 227 source files spread across C + headers | Single-file modules with clear `pub` exports |
| ASan/Valgrind needed to catch bugs | Safety-mode builds catch OOB/UB at runtime by default |
| No tagged unions — stringly-typed content variants | `union(enum)` for `ContentType`, provider responses, etc. |

---

## Scope

### In scope
- All C source under `src/` (108 `.c` + 119 `.h` files, ~57 k lines)
- Build system: replace `Makefile` with `build.zig`
- Unit tests: replace `tests/test_*.c` with Zig's built-in test runner
- C FFI bridges to external libraries kept as minimal `@cImport` shims until native Zig alternatives exist

### Out of scope (unchanged)
- `tools/web_browse_agent/` — already Go; stays Go
- `docs/` — no changes needed
- External dependencies: libcurl, sqlite3, ncursesw, openssl, pthreads remain as C libs called via `@cImport` during transition

### External library strategy
| Library | Strategy |
|---|---|
| `libcurl` | Keep via `@cImport` initially; later replace with `std.http.Client` once HTTP/2 SSE support is solid |
| `sqlite3` | Keep via `@cImport`; wrap in a thin Zig struct for error unions |
| `ncursesw` | Keep via `@cImport`; consider `libvaxis` (pure Zig TUI) as a later stretch goal |
| `libssl/libcrypto` | Keep for AWS Bedrock signing; Zig `std.crypto` covers HMAC-SHA256 natively |
| `libbsd` | **Eliminated entirely** — all its functions have Zig-native equivalents |
| `cJSON` | **Eliminated** — replaced by `std.json` |
| `pthread` | **Eliminated** — replaced by `std.Thread` |

---

## Migration Strategy

A **module-by-module port with a parallel build** approach:

1. A `zig/` directory lives alongside `src/` during the transition.
2. The existing C binary continues to ship until a phase is complete and parity is confirmed.
3. Each phase ends with a working binary (either hybrid C+Zig via `build.zig` C-source inclusion, or pure Zig).
4. Feature-flag a `ZIG_BUILD=1` make/zig-build switch to opt into the new binary.

**Toolchain version pin**: Zig `0.12.1` (installed system-wide). All Zig code targets this version.

---

## Phases

### Phase 1 — Foundation & Build System
**Goal**: `build.zig` produces a working binary that compiles the existing C files.  
This de-risks the toolchain switch before any Zig code is written.

- [x] Add `build.zig` that replicates the existing `Makefile` targets (`klawed`, `test`, `debug`, `install`)
- [x] Wire all existing C source files through `build.zig` using `addCSourceFiles`
- [x] Confirm all flags (`-Wall -Werror -std=c11 -lbsd` etc.) are preserved
- [x] Add CI check: `zig build` must succeed
- [x] Document Zig toolchain version pin (`0.12.1`) in plan
- [x] Add `.zig-version` file to lock the version

---

### Phase 2 — Low-level Utilities (Leaf Modules, No Dependencies)
**Goal**: Port the bottom of the dependency tree first — no risk of cascading breakage.

Modules to port (in order):
- [x] `src/util/string_utils.c` → `zig/util/string_utils.zig`  
  Key wins: `std.mem`, `std.ascii`, slice bounds — no `strlcpy` needed
- [x] `src/util/timestamp_utils.c` → `zig/util/timestamp_utils.zig`
- [x] `src/util/format_utils.c` → `zig/util/format_utils.zig`  
  Use `std.fmt.allocPrint` instead of `snprintf` + manual size checks
- [x] `src/util/env_utils.c` → `zig/util/env_utils.zig`  
  Use `std.process.getEnvVarOwned`
- [x] `src/util/file_utils.c` → `zig/util/file_utils.zig`  
  Use `std.fs` — directory creation, path joining, recursive mkdir
- [x] `src/util/diff_utils.c` → `zig/util/diff_utils.zig`
- [x] `src/util/output_utils.c` → `zig/util/output_utils.zig`
- [x] `src/base64.c` → `zig/base64.zig`  
  Use `std.base64`
- [x] `src/logger.c` → `zig/logger.zig`  
  Use `std.Thread.Mutex` instead of pthread; `std.fmt` for log formatting
- [x] `src/version.h` → `zig/version.zig` (comptime constant baked in at build time)
- [x] Port `src/arena.h` → **no port**; documented in `zig/util/arena.zig` as usage guide for `std.heap.ArenaAllocator`
- [x] Port `src/array_resize.c` → **no port**; documented in `zig/util/array_list.zig` as usage guide for `std.ArrayList`
- [x] Port `src/util/alloc_utils.h` → **no port**; Zig allocator interface replaces it entirely
- [x] Wire `zig build test` step in `build.zig` to run `zig/tests.zig` (79 tests, all pass)

Each module: write tests alongside the port using `test "name" { ... }` blocks.

---

### Phase 3 — Data & Persistence Layer
**Goal**: All database and serialization code in Zig with proper error unions.

- [x] `src/migrations.c` / `src/token_usage_db_migrations.c` → `zig-src/migrations.zig`  
  Wrap `sqlite3` with a thin Zig error-union API: `fn exec(db: *Db, sql: []const u8) !void`
- [x] `src/persistence.c` → `zig-src/persistence.zig`
- [x] `src/sqlite_queue.c` → `zig-src/sqlite_queue.zig`  
  Replace pthread mutex with `std.Thread.Mutex`
- [x] `src/token_usage_db.c` → `zig-src/token_usage_db.zig`
- [x] `src/memory_db.c` → `zig-src/memory_db.zig`  
  FTS5 queries stay in SQL strings; Zig error unions replace `int` return codes
- [x] `src/session.c` + `src/session_persistence.c` → `zig-src/session.zig`
- [x] `src/history_file.c` → `zig-src/history_file.zig`
- [x] `src/data_dir.c` → `zig-src/data_dir.zig`

---

### Phase 4 — Configuration & Providers
**Goal**: JSON config loading and all LLM provider implementations in Zig.

- [x] `src/config.c` / `src/config_command.c` → `zig/config.zig`  
  Replace `cJSON` with `std.json`; use `std.json.parseFromSlice` with arena allocation
- [x] `src/provider_config_loader.c` → `zig/provider_config_loader.zig`
- [x] Define `Provider` as a `union(enum)` — eliminates the current stringly-typed dispatch
- [x] `src/openai_provider.c` + `src/openai_messages.c` + `src/openai_responses.c` → `zig/providers/openai.zig`
- [x] `src/anthropic_provider.c` → `zig/providers/anthropic.zig`
- [x] `src/bedrock_provider.c` + `src/bedrock_converse.c` + `src/aws_bedrock.c` → `zig/providers/bedrock.zig`  
  Replace OpenSSL HMAC with `std.crypto.auth.hmac.sha2.HmacSha256`
- [x] `src/deepseek_provider.c` → `zig/providers/deepseek.zig`
- [x] `src/moonshot_provider.c` → `zig/providers/moonshot.zig`
- [x] `src/kimi_oauth.c` + `src/kimi_coding_plan_provider.c` → `zig/providers/kimi.zig`
- [x] `src/provider.c` → `zig/provider.zig` (dispatch table / vtable pattern using Zig interfaces)

---

### Phase 5 — HTTP & Streaming
**Goal**: Network layer with proper error handling and streaming SSE.

- [x] `src/http_client.c` → `zig/http_client.zig`  
  Keep libcurl via `@cImport` initially; wrap in Zig error unions
- [x] `src/retry_logic.c` → `zig/retry_logic.zig`
- [x] SSE streaming: define a proper `StreamEvent` tagged union (`zig/api/sse_parser.zig`)
- [x] `src/api/api_builder.c` + `src/api/api_client.c` + `src/api/api_response.c` → `zig/api/`  
  - `zig/api/sse_parser.zig` — pure SSE line parser emitting `OwnedEvent` values  
  - `zig/api/api_response.zig` — unified `ApiResponse` type + per-provider JSON parsing  
  - `zig/api/api_client.zig` — HTTP dispatch with header building and exponential-backoff retry
- [x] Wire `OpenAIProvider.sendRequest` and `AnthropicProvider.sendRequest` to `http_client`

---

### Phase 6 — Conversation & Context
**Goal**: Core agent message model in Zig with tagged unions for content types.

- [x] `src/conversation/content_types.c` → `zig-src/conversation/content_types.zig`  
  Define `ContentBlock = union(enum) { text, tool_use, tool_result, image, ... }`
- [x] `src/conversation/conversation_state.c` → `zig-src/conversation/state.zig`
- [x] `src/conversation/message_builder.c` + `message_parser.c` → `zig-src/conversation/message.zig`
- [x] `src/conversation/conversation_processor.c` → `zig-src/conversation/processor.zig`
- [x] `src/context/system_prompt.c` → `zig-src/context/system_prompt.zig`
- [x] `src/context/environment.c` → `zig-src/context/environment.zig`
- [x] `src/context/klawed_md.c` → `zig-src/context/klawed_md.zig`
- [x] `src/context/memory_injection.c` → `zig-src/context/memory_injection.zig`
- [x] `src/compaction.c` → `zig-src/compaction.zig`

---

### Phase 7 — Tools
**Goal**: All built-in tool implementations in Zig.

- [x] `src/tools/tool_bash.c` → `zig/tools/bash.zig`  
  Use `std.process.Child` for subprocess management + timeout via killer thread
- [x] `src/tools/tool_filesystem.c` → `zig/tools/filesystem.zig`  
  (Read, Write, Edit, MultiEdit, Glob) — use `std.fs`, `std.mem.indexOf`
- [x] `src/tools/tool_search.c` → `zig/tools/search.zig`  
  (Grep) — delegates to system `rg`/`ag`/`grep` via subprocess
- [x] `src/tools/tool_subagent.c` → `zig/tools/subagent.zig`
- [x] `src/tools/tool_todo.c` + `src/todo.c` → `zig/tools/todo.zig`
- [x] `src/tools/tool_image.c` → `zig/tools/image.zig`
- [x] `src/tools/tool_sleep.c` → `zig/tools/sleep.zig`
- [x] `src/tools/tool_definitions.c` + `tool_executor.c` + `tool_registry.c` → `zig/tools/registry.zig`
- [x] `src/dynamic_tools.c` → `zig/tools/dynamic.zig`
- [x] `src/tool_utils.c` → `zig/tools/utils.zig`
- [x] `src/subagent_manager.c` → `zig/subagent_manager.zig`
- [x] `src/explore_tools.c` → `zig/explore_tools.zig`
- [x] `src/mcp.c` → `zig/mcp.zig`  
  Replace pthread with `std.Thread`; JSON-RPC via `std.json`

---

### Phase 8 — Agent Core Loop
**Goal**: The main agent loop and interactive/oneshot modes in Zig.

- [x] `src/interactive/input_handler.c` + `command_dispatch.c` + `interactive_loop.c` → `zig/interactive/`
- [x] `src/interactive/response_processor.c` → `zig/interactive/response_processor.zig`
- [x] `src/oneshot_mode.c` + `oneshot_output.c` + `oneshot_processor.c` + `oneshot_ui.c` → `zig/oneshot/`
- [x] `src/commands.c` + `src/config_command.c` + `src/provider_command.c` → `zig/commands.zig`
- [x] `src/ai_worker.c` + `src/background_init.c` → `zig/ai_worker.zig`  
  `std.Thread` + `std.Thread.Mutex` + `std.Thread.Condition`
- [x] `src/message_queue.c` → `zig/message_queue.zig`
- [x] `src/completion.c` → `zig/completion.zig`
- [x] `src/dump_utils.c` → `zig/dump_utils.zig`
- [x] `src/process_utils.c` → `zig/process_utils.zig`
- [x] `src/klawed.c` → `zig/main.zig` (entry point, ~370 lines)

---

### Phase 9 — TUI
**Goal**: Terminal UI fully in Zig. This is the largest, most ncurses-dependent phase.

- [ ] `src/colorscheme.h` + `src/builtin_themes.c` + `src/theme_explorer.c` → `zig-src/tui/colorscheme.zig`
- [ ] `src/ncurses_input.c` → `zig-src/tui/input.zig`
- [ ] `src/tui_core.c` → `zig-src/tui/core.zig`
- [ ] `src/tui_render.c` → `zig-src/tui/render.zig`
- [ ] `src/tui_conversation.c` → `zig-src/tui/conversation.zig`
- [ ] `src/tui_modes.c` → `zig-src/tui/modes.zig`
- [ ] `src/tui_input.c` → `zig-src/tui/input_handler.zig`
- [ ] `src/tui_completion.c` → `zig-src/tui/completion.zig`
- [ ] `src/tui_history.c` → `zig-src/tui/history.zig`
- [ ] `src/tui_paste.c` → `zig-src/tui/paste.zig`
- [ ] `src/tui_search.c` → `zig-src/tui/search.zig`
- [ ] `src/tui_window.c` → `zig-src/tui/window.zig`
- [ ] `src/window_manager.c` → `zig-src/tui/window_manager.zig`
- [ ] `src/file_search.c` → `zig-src/tui/file_search.zig`
- [ ] `src/history_search.c` → `zig-src/tui/history_search.zig`
- [ ] `src/help_modal.c` → `zig-src/tui/help_modal.zig`
- [ ] `src/tui.c` → `zig-src/tui/tui.zig` (top-level TUI init/teardown)
- [ ] `src/ui/print_helpers.c` + `tool_output_display.c` + `ui_output.c` → `zig-src/ui/`
- [ ] `src/indicators.h` → `zig-src/tui/indicators.zig`
- [ ] `src/vltrn_banner.c` → `zig-src/tui/banner.zig`
- [ ] **Stretch**: evaluate `libvaxis` (pure Zig TUI) to eliminate the ncurses `@cImport`

---

### Phase 10 — Tests, Cleanup & Cutover
**Goal**: Full parity, all tests passing, C source deleted.

- [ ] Port all `tests/test_*.c` to Zig test blocks (`zig-src/tests/`)
- [ ] Achieve zero failing tests with `zig build test`
- [ ] Remove `src/` C source tree
- [ ] Remove `Makefile` (or keep as thin shim calling `zig build`)
- [ ] Update `KLAWED.md` with Zig coding standards and new quick-start commands
- [ ] Update all `docs/` references that mention C, `gcc`, `make`, `libbsd`, etc.
- [ ] Tag release `v2.0.0-zig`

---

## Key Zig Patterns to Establish Early

These should be decided in Phase 1/2 and applied consistently throughout:

### Allocator threading
Every function that allocates takes an `allocator: std.mem.Allocator` parameter. The top-level `main` creates a `std.heap.GeneralPurposeAllocator` (debug) or `std.heap.c_allocator` (release). Short-lived scopes use `std.heap.ArenaAllocator`.

### Error handling
- Return `!T` from all fallible functions
- Use `errdefer` to clean up allocations on failure paths
- Never use sentinel error codes (`-1`, `0`, `NULL`) as the primary signal

### String handling
- All strings are `[]const u8` (slices, not null-terminated)
- C interop: use `std.mem.sliceTo(ptr, 0)` to convert `*const u8` → slice
- Dynamic strings: `std.ArrayList(u8)` as a string builder

### Tagged unions for variants
Replace all `int type` + `union` pairs in C structs with `union(enum)` — especially `ContentBlock`, `ToolResult`, `StreamEvent`, `Provider`.

### Comptime for platform differences
```zig
const is_macos = builtin.os.tag == .macos;
const ncurses_lib = if (is_macos) "ncurses" else "ncursesw";
```

### C FFI shim pattern
```zig
// zig-src/ffi/sqlite.zig
const c = @cImport(@cInclude("sqlite3.h"));
pub const Db = struct {
    handle: *c.sqlite3,
    pub fn exec(self: *Db, sql: []const u8) !void {
        const rc = c.sqlite3_exec(self.handle, sql.ptr, null, null, null);
        if (rc != c.SQLITE_OK) return error.SqliteError;
    }
};
```

---

## Risks & Mitigations

| Risk | Likelihood | Mitigation |
|---|---|---|
| ncurses has no ergonomic Zig wrapper | High | Use `@cImport` for ncurses throughout Phase 9; evaluate `libvaxis` as a post-cutover stretch goal |
| libcurl SSE streaming is complex to wrap safely | Medium | Keep libcurl via `@cImport` through Phase 5; only replace with `std.http.Client` once Zig HTTP/2 + SSE is production-ready |
| Zig version instability (pre-1.0 API churn) | Medium | Pin a specific release (0.14.0), track changelog, do migration in a single commit per breaking change |
| Test coverage gaps discovered during port | Medium | Port tests in lockstep with each module — don't defer tests to Phase 10 |
| Zig sqlite3 binding gaps | Low | `zig-sqlite` library exists as a fallback; or hand-roll a thin wrapper (sqlite API is stable) |
| Thread model differences (pthreads → `std.Thread`) | Low | `std.Thread` maps directly; `Mutex`, `Condition`, `ResetEvent` all exist |

---

## Effort Estimates

| Phase | Modules | Approx. C lines | Estimated effort |
|---|---|---|---|
| 1 — Build system | `Makefile` → `build.zig` | — | 1–2 days |
| 2 — Utilities | 13 leaf modules | ~2 000 | 1 week |
| 3 — Persistence | 8 modules | ~5 000 | 1.5 weeks |
| 4 — Config & Providers | 12 modules | ~8 000 | 2–3 weeks |
| 5 — HTTP & Streaming | 5 modules | ~3 000 | 1.5 weeks |
| 6 — Conversation & Context | 10 modules | ~4 000 | 1.5 weeks |
| 7 — Tools | 14 modules | ~5 000 | 2 weeks |
| 8 — Agent Core | 12 modules | ~6 000 | 2 weeks |
| 9 — TUI | 22 modules | ~12 000 | 3–4 weeks |
| 10 — Tests & cutover | 80+ test files | ~10 000 | 2 weeks |
| **Total** | **~108 modules** | **~55 000 lines** | **~4–5 months** |

*Estimates assume one experienced engineer familiar with both C and Zig. Zig familiarity ramp-up adds 1–2 weeks if starting from zero.*

---

## Definition of Done

A phase is **complete** when:
1. All C files for that phase are deleted from `src/`
2. `zig build` produces a binary with identical CLI behaviour
3. All existing tests for that phase pass under `zig build test`
4. `zig build test` runs with ReleaseSafe mode (bounds checking + overflow detection on)
5. No `@panic` calls in production paths — only `unreachable` for truly impossible branches

---

## References

- [Zig stdlib docs](https://ziglang.org/documentation/master/std/)
- [Zig build system docs](https://ziglang.org/learn/build-system/)
- [libvaxis — pure Zig TUI](https://github.com/rockorager/libvaxis)
- [zig-sqlite](https://github.com/vrischmann/zig-sqlite)
- [Zig C interop guide](https://ziglang.org/documentation/master/#C)
