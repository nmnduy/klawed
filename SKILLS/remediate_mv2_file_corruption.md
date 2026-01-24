# Remediate MV2 File Corruption

## Overview

MV2 files are single-file memory containers for AI agents, containing headers, WAL (Write-Ahead Log), data segments, search indices, and a TOC (Table of Contents). This guide covers diagnosing and repairing various corruption scenarios.

## MV2 File Structure

```
┌─────────────────────────────────────────────────────────────┐
│ Header                 │ 4 KB (bytes 0-4095)                │
├─────────────────────────────────────────────────────────────┤
│ Embedded WAL           │ 1-64 MB (capacity-dependent)       │
├─────────────────────────────────────────────────────────────┤
│ Data Segments          │ Variable (frame payloads)          │
├─────────────────────────────────────────────────────────────┤
│ Lex Index Segment      │ Tantivy index (optional)           │
├─────────────────────────────────────────────────────────────┤
│ Vec Index Segment      │ HNSW vectors (optional)            │
├─────────────────────────────────────────────────────────────┤
│ Time Index Segment     │ Chronological ordering             │
├─────────────────────────────────────────────────────────────┤
│ TOC (Footer)           │ Segment catalog + checksums        │
│ Commit Footer          │ "MV2FOOT!" magic (56 bytes)        │
└─────────────────────────────────────────────────────────────┘
```

### Key Header Fields

| Offset | Size | Field | Description |
|--------|------|-------|-------------|
| 0 | 4 | `magic` | `MV2\0` (0x4D 0x56 0x32 0x00) |
| 8 | 8 | `footer_offset` | Byte offset to TOC |
| 16 | 8 | `wal_offset` | Byte offset to WAL (always 4096) |
| 24 | 8 | `wal_size` | WAL region size in bytes |
| 32 | 8 | `wal_checkpoint_pos` | Last checkpointed position |
| 40 | 8 | `wal_sequence` | Current WAL sequence number |
| 48 | 32 | `toc_checksum` | SHA-256 of TOC segment |

## Using the Doctor Command

The primary tool for remediation is `Memvid::doctor()`:

```rust
use memvid::{Memvid, DoctorOptions};

// Basic repair
let options = DoctorOptions::default();
let report = Memvid::doctor("corrupted.mv2", options)?;

// With index rebuilds
let options = DoctorOptions {
    rebuild_time_index: true,
    rebuild_lex_index: true,
    rebuild_vec_index: true,
    vacuum: false,
    dry_run: false,
    quiet: false,
};
let report = Memvid::doctor("corrupted.mv2", options)?;

// Dry run (plan only, no modifications)
let options = DoctorOptions {
    dry_run: true,
    ..Default::default()
};
let plan = Memvid::doctor("corrupted.mv2", options)?;
```

### Doctor Phases

1. **HeaderHealing** - Fix header pointers and checksums
2. **WalReplay** - Replay pending WAL records
3. **Vacuum** - Compact active payloads (if requested)
4. **IndexRebuild** - Rebuild time/lex/vec indices
5. **Finalize** - Rewrite TOC and header
6. **Verify** - Run deep verification

## Common Corruption Scenarios

### 1. WAL Corruption

**Symptoms:**
- `WalCorruption` error on open
- `WalChecksumMismatch` findings

**Repair Strategy:**
The doctor automatically detects WAL corruption and attempts recovery by:
1. Zeroing out the corrupted WAL region
2. Resetting `wal_checkpoint_pos` and `wal_sequence` to 0
3. Persisting the repaired header

**Manual WAL Reset (if doctor fails):**
```rust
use std::fs::OpenOptions;
use std::io::{Seek, SeekFrom, Write};

let mut file = OpenOptions::new()
    .read(true)
    .write(true)
    .open("file.mv2")?;

// Read header to get WAL offset/size
let header = HeaderCodec::read(&mut file)?;

// Zero WAL region
let zeros = vec![0u8; header.wal_size as usize];
file.seek(SeekFrom::Start(header.wal_offset))?;
file.write_all(&zeros)?;

// Reset header WAL fields
header.wal_checkpoint_pos = 0;
header.wal_sequence = 0;
HeaderCodec::write(&mut file, &header)?;
file.sync_all()?;
```

### 2. Header/Footer Pointer Corruption

**Symptoms:**
- `InvalidToc` error: "footer not found"
- `HeaderFooterOffsetMismatch` finding
- File won't open normally

**Repair Strategy - Tier 2 Aggressive Repair:**
The doctor scans backwards from file end looking for `MV2FOOT!` magic:

```rust
// The footer magic is at file_size - 56 bytes (expected location)
const FOOTER_MAGIC: &[u8] = b"MV2FOOT!";
const FOOTER_SIZE: u64 = 56;

// Scan for footer and repair header
let actual_footer_offset = scan_for_footer(path)?;

// Fix header's footer_offset field at byte offset 8
file.seek(SeekFrom::Start(8))?;
file.write_all(&actual_footer_offset.to_le_bytes())?;
file.sync_all()?;
```

### 3. TOC Checksum Mismatch

**Symptoms:**
- `TocChecksumMismatch` finding
- `HeaderTocChecksumMismatch` finding

**Repair Strategy:**
Doctor automatically heals by:
1. Reading the actual TOC from file
2. Computing correct checksum
3. Updating header's `toc_checksum` field

### 4. Index Corruption

**Symptoms:**
- `TimeIndexChecksumMismatch` finding
- `LexIndexCorrupt` finding
- `VecIndexCorrupt` finding
- Search results missing or incorrect

**Repair Strategy:**
Rebuild indices from frame data:

```rust
let options = DoctorOptions {
    rebuild_time_index: true,
    rebuild_lex_index: true,
    rebuild_vec_index: true,
    ..Default::default()
};
Memvid::doctor("file.mv2", options)?;
```

### 5. Pending WAL Records

**Symptoms:**
- `WalHasPendingRecords` finding
- File crashed mid-write

**Repair Strategy:**
Doctor replays pending WAL records to data segments:

```rust
// This happens automatically in doctor
mem.recover_wal()?;
```

## Verification

After repair, verify file integrity:

```rust
// Shallow verification (header + TOC structure)
let report = Memvid::verify("file.mv2", false)?;

// Deep verification (all checksums + indices)
let report = Memvid::verify("file.mv2", true)?;

assert_eq!(report.overall_status, VerificationStatus::Passed);
```

## Low-Level Inspection

### Hex Dump Header

```bash
# Check magic bytes and header structure
xxd -l 128 file.mv2

# Expected start: 4d56 3200 (MV2\0)
```

### Find Footer Location

```bash
# Search for footer magic near end
xxd file.mv2 | grep -i "4d56 3246 4f4f 5421"  # MV2FOOT!

# Or use strings
strings -t d file.mv2 | grep "MV2FOOT"
```

### Inspect Footer Offset in Header

```bash
# Read bytes 8-15 (footer_offset, little-endian u64)
xxd -s 8 -l 8 file.mv2
```

## Error Types Reference

| Error | Description | Recovery |
|-------|-------------|----------|
| `WalCorruption` | WAL region damaged | Zero WAL, reset header |
| `InvalidHeader` | Header parse failure | Aggressive repair scan |
| `InvalidToc` | TOC parse/find failure | Scan for footer magic |
| `ChecksumMismatch` | Data integrity failure | Rebuild affected index |

## DoctorFinding Codes

| Code | Severity | Description |
|------|----------|-------------|
| `HeaderDecodeFailure` | Error | Cannot parse header |
| `HeaderFooterOffsetMismatch` | Warning | footer_offset wrong |
| `HeaderTocChecksumMismatch` | Warning | toc_checksum wrong |
| `TocDecodeFailure` | Error | Cannot parse TOC |
| `TocChecksumMismatch` | Error | TOC integrity failure |
| `WalHasPendingRecords` | Warning | Uncommitted data |
| `WalChecksumMismatch` | Error | WAL corruption |
| `TimeIndexMissing` | Warning | No time index |
| `TimeIndexChecksumMismatch` | Warning | Time index corrupt |
| `LexIndexCorrupt` | Warning | Full-text index bad |
| `VecIndexCorrupt` | Warning | Vector index bad |

## Best Practices

1. **Always backup before repair:**
   ```bash
   cp corrupted.mv2 corrupted.mv2.bak
   ```

2. **Use dry_run first:**
   ```rust
   let options = DoctorOptions { dry_run: true, ..Default::default() };
   let plan = Memvid::doctor("file.mv2", options)?;
   println!("{:?}", plan.findings);
   ```

3. **Check for sidecar files (should not exist):**
   ```bash
   ls -la file.mv2*
   # Only file.mv2 should exist - no .wal, .lock, .shm files
   ```

4. **Ensure exclusive access:**
   The doctor requires exclusive file lock. Close all other processes using the file.

5. **For catastrophic corruption:**
   If doctor fails, try extracting recoverable frames manually by scanning for frame headers in the data segment region.

## Related Files

- `src/memvid/doctor.rs` - Main doctor implementation
- `src/memvid/maintenance.rs` - Verification logic
- `src/io/wal.rs` - WAL implementation
- `src/io/header.rs` - Header codec
- `src/toc.rs` - TOC structure
- `MV2_SPEC.md` - File format specification
