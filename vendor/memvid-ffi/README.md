# memvid-ffi

C FFI bindings for memvid-core, enabling memory feature in klawed.

## What's tracked here

This directory contains **only** the FFI bindings code that we wrote:
- `Cargo.toml` - Rust project configuration (points to memvid-core on crates.io)
- `src/lib.rs` - C FFI implementation

The actual memvid-core library (version 2.0.131) is fetched from crates.io during build time.

## Building

The memvid-ffi library is built as part of klawed's build process when `MEMVID=1` is set:

```bash
make MEMVID=1
```

This will:
1. Build the FFI library (cdylib or staticlib)
2. Link it with klawed's C code

## Development

To update the FFI bindings:
1. Modify `src/lib.rs` 
2. Update the `memvid-core` version in `Cargo.toml` if needed
3. Rebuild with `make clean && make MEMVID=1`

## Source

The FFI code is maintained in the klawed repository. The memvid-core library
is maintained separately at https://github.com/memvid/memvid
