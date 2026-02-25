# zig/

This directory contains the Zig port of klawed, currently in progress.

## Status

**Phase 1 complete** — `build.zig` at the repo root compiles all existing C sources via Zig's build system.  
Zig source files will be added here as each module is ported (Phase 2+).

## Structure (planned)

```
zig/
├── README.md               ← you are here
├── main.zig                ← entry point (Phase 8)
├── util/                   ← string/format/env/file utilities (Phase 2)
├── providers/              ← LLM provider implementations (Phase 4)
├── conversation/           ← message model (Phase 6)
├── tools/                  ← built-in tools (Phase 7)
├── tui/                    ← terminal UI (Phase 9)
└── ...
```

## Toolchain

Pinned to **Zig 0.12.1**. See `.zig-version` in the repo root.

To build with the Zig build system:

```bash
zig build          # produces zig-out/bin/klawed
zig build -Dvoice  # build with voice input enabled
```

## Migration plan

See `docs/zig-migration-plan.md` for the full phase-by-phase migration plan.
