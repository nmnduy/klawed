//! C FFI bindings for memvid-core
//!
//! This crate provides a C-compatible interface to the memvid-core library,
//! enabling C programs like klawed to use memvid's persistent memory capabilities.
#![allow(unsafe_op_in_unsafe_fn)]

use std::ffi::{CStr, CString};
use std::os::raw::c_char;
use std::ptr;
use std::sync::Mutex;

use memvid_core::{Memvid, MemoryCard, MemoryCardBuilder, MemoryKind, VersionRelation};

/// Thread-local storage for the last error message
thread_local! {
    static LAST_ERROR: std::cell::RefCell<Option<CString>> = const { std::cell::RefCell::new(None) };
}

/// Set the last error message
fn set_last_error(msg: &str) {
    LAST_ERROR.with(|e| {
        *e.borrow_mut() = CString::new(msg).ok();
    });
}

/// Opaque handle wrapping a Memvid instance
pub struct MemvidHandle {
    inner: Mutex<Memvid>,
}

/// Open or create a memvid database file
///
/// # Safety
/// - `path` must be a valid null-terminated UTF-8 string
/// - The returned handle must be freed with `memvid_close`
#[no_mangle]
pub unsafe extern "C" fn memvid_open(path: *const c_char) -> *mut MemvidHandle {
    if path.is_null() {
        set_last_error("path is null");
        return ptr::null_mut();
    }

    let path_str = match CStr::from_ptr(path).to_str() {
        Ok(s) => s,
        Err(e) => {
            set_last_error(&format!("invalid UTF-8 in path: {}", e));
            return ptr::null_mut();
        }
    };

    // Try to open existing file first
    let mv = match Memvid::open(path_str) {
        Ok(mv) => mv,
        Err(e) => {
            // If file doesn't exist, try to create it
            let err_str = e.to_string();
            if err_str.contains("No such file") || err_str.contains("not found") {
                match Memvid::create(path_str) {
                    Ok(mv) => mv,
                    Err(create_err) => {
                        set_last_error(&format!("failed to create memvid: {}", create_err));
                        return ptr::null_mut();
                    }
                }
            } else {
                set_last_error(&format!("failed to open memvid: {}", e));
                return ptr::null_mut();
            }
        }
    };

    Box::into_raw(Box::new(MemvidHandle {
        inner: Mutex::new(mv),
    }))
}

/// Close a memvid handle and free resources
///
/// # Safety
/// - `handle` must be a valid pointer returned by `memvid_open`, or null
/// - The handle must not be used after this call
#[no_mangle]
pub unsafe extern "C" fn memvid_close(handle: *mut MemvidHandle) {
    if !handle.is_null() {
        // Commit any pending changes before closing
        if let Ok(mut mv) = (*handle).inner.lock() {
            let _ = mv.commit();
        }
        let _ = Box::from_raw(handle);
    }
}

/// Convert MemoryKind from u8
fn memory_kind_from_u8(kind: u8) -> MemoryKind {
    match kind {
        0 => MemoryKind::Fact,
        1 => MemoryKind::Preference,
        2 => MemoryKind::Event,
        3 => MemoryKind::Profile,
        4 => MemoryKind::Relationship,
        5 => MemoryKind::Goal,
        _ => MemoryKind::Other,
    }
}

/// Store a memory card
///
/// # Safety
/// - `handle` must be a valid pointer returned by `memvid_open`
/// - `entity`, `slot`, and `value` must be valid null-terminated UTF-8 strings
///
/// Returns the card ID on success, -1 on error
#[no_mangle]
pub unsafe extern "C" fn memvid_put_memory(
    handle: *mut MemvidHandle,
    entity: *const c_char,
    slot: *const c_char,
    value: *const c_char,
    kind: u8,
    relation: u8,
) -> i64 {
    if handle.is_null() {
        set_last_error("handle is null");
        return -1;
    }

    let entity_str = match ptr_to_str(entity) {
        Some(s) => s,
        None => {
            set_last_error("invalid entity string");
            return -1;
        }
    };

    let slot_str = match ptr_to_str(slot) {
        Some(s) => s,
        None => {
            set_last_error("invalid slot string");
            return -1;
        }
    };

    let value_str = match ptr_to_str(value) {
        Some(s) => s,
        None => {
            set_last_error("invalid value string");
            return -1;
        }
    };

    let mut mv = match (*handle).inner.lock() {
        Ok(guard) => guard,
        Err(e) => {
            set_last_error(&format!("failed to acquire lock: {}", e));
            return -1;
        }
    };

    // Build the memory card with version relation
    let mut builder = MemoryCardBuilder::new()
        .kind(memory_kind_from_u8(kind))
        .entity(entity_str)
        .slot(slot_str)
        .value(value_str)
        .source(0, None) // No source frame for FFI-created cards
        .engine("klawed-ffi", "1.0.0");

    // Apply version relation
    builder = match relation {
        1 => builder.updates(),
        2 => builder.extends(),
        3 => builder.retracts(),
        _ => builder, // 0 = Sets (default)
    };

    let card = match builder.build(0) {
        // ID will be assigned by put_memory_card
        Ok(card) => card,
        Err(e) => {
            set_last_error(&format!("failed to build memory card: {}", e));
            return -1;
        }
    };

    match mv.put_memory_card(card) {
        Ok(id) => id as i64,
        Err(e) => {
            set_last_error(&format!("failed to store memory: {}", e));
            -1
        }
    }
}

/// Get the current value for an entity's slot
///
/// # Safety
/// - `handle` must be a valid pointer returned by `memvid_open`
/// - `entity` and `slot` must be valid null-terminated UTF-8 strings
/// - The returned string must be freed with `memvid_free_string`
///
/// Returns JSON string or NULL if not found
#[no_mangle]
pub unsafe extern "C" fn memvid_get_current(
    handle: *mut MemvidHandle,
    entity: *const c_char,
    slot: *const c_char,
) -> *mut c_char {
    if handle.is_null() {
        set_last_error("handle is null");
        return ptr::null_mut();
    }

    let entity_str = match ptr_to_str(entity) {
        Some(s) => s,
        None => {
            set_last_error("invalid entity string");
            return ptr::null_mut();
        }
    };

    let slot_str = match ptr_to_str(slot) {
        Some(s) => s,
        None => {
            set_last_error("invalid slot string");
            return ptr::null_mut();
        }
    };

    let mv = match (*handle).inner.lock() {
        Ok(guard) => guard,
        Err(e) => {
            set_last_error(&format!("failed to acquire lock: {}", e));
            return ptr::null_mut();
        }
    };

    match mv.memories().get_current(entity_str, slot_str) {
        Some(card) => card_to_json_string(card),
        None => ptr::null_mut(),
    }
}

/// Search memories by text query
///
/// # Safety
/// - `handle` must be a valid pointer returned by `memvid_open`
/// - `query` must be a valid null-terminated UTF-8 string
/// - The returned string must be freed with `memvid_free_string`
///
/// Returns JSON array of matching memories
#[no_mangle]
pub unsafe extern "C" fn memvid_search(
    handle: *mut MemvidHandle,
    query: *const c_char,
    top_k: u32,
) -> *mut c_char {
    if handle.is_null() {
        set_last_error("handle is null");
        return ptr::null_mut();
    }

    let query_str = match ptr_to_str(query) {
        Some(s) => s,
        None => {
            set_last_error("invalid query string");
            return ptr::null_mut();
        }
    };

    let mv = match (*handle).inner.lock() {
        Ok(guard) => guard,
        Err(e) => {
            set_last_error(&format!("failed to acquire lock: {}", e));
            return ptr::null_mut();
        }
    };

    // Search through all memory cards using simple text matching
    // (Full-text search requires the lex feature and tantivy index)
    let query_lower = query_str.to_lowercase();
    let mut results: Vec<&MemoryCard> = mv
        .memories()
        .cards()
        .iter()
        .filter(|card| {
            card.entity.to_lowercase().contains(&query_lower)
                || card.slot.to_lowercase().contains(&query_lower)
                || card.value.to_lowercase().contains(&query_lower)
        })
        .take(top_k as usize)
        .collect();

    // Sort by ID (newest first)
    results.sort_by(|a, b| b.id.cmp(&a.id));

    cards_to_json_array(&results)
}

/// Get all memories for an entity
///
/// # Safety
/// - `handle` must be a valid pointer returned by `memvid_open`
/// - `entity` must be a valid null-terminated UTF-8 string
/// - The returned string must be freed with `memvid_free_string`
///
/// Returns JSON array of entity's memories
#[no_mangle]
pub unsafe extern "C" fn memvid_get_entity_memories(
    handle: *mut MemvidHandle,
    entity: *const c_char,
) -> *mut c_char {
    if handle.is_null() {
        set_last_error("handle is null");
        return ptr::null_mut();
    }

    let entity_str = match ptr_to_str(entity) {
        Some(s) => s,
        None => {
            set_last_error("invalid entity string");
            return ptr::null_mut();
        }
    };

    let mv = match (*handle).inner.lock() {
        Ok(guard) => guard,
        Err(e) => {
            set_last_error(&format!("failed to acquire lock: {}", e));
            return ptr::null_mut();
        }
    };

    let cards = mv.memories().get_entity_cards(entity_str);
    cards_to_json_array(&cards)
}

/// Commit pending changes to disk
///
/// # Safety
/// - `handle` must be a valid pointer returned by `memvid_open`
///
/// Returns 0 on success, -1 on error
#[no_mangle]
pub unsafe extern "C" fn memvid_commit(handle: *mut MemvidHandle) -> i32 {
    if handle.is_null() {
        set_last_error("handle is null");
        return -1;
    }

    let mut mv = match (*handle).inner.lock() {
        Ok(guard) => guard,
        Err(e) => {
            set_last_error(&format!("failed to acquire lock: {}", e));
            return -1;
        }
    };

    match mv.commit() {
        Ok(_) => 0,
        Err(e) => {
            set_last_error(&format!("failed to commit: {}", e));
            -1
        }
    }
}

/// Free a string returned by memvid functions
///
/// # Safety
/// - `s` must be a valid pointer returned by a memvid function, or null
#[no_mangle]
pub unsafe extern "C" fn memvid_free_string(s: *mut c_char) {
    if !s.is_null() {
        let _ = CString::from_raw(s);
    }
}

/// Get the last error message
///
/// # Safety
/// The returned string is valid until the next memvid call on the same thread
#[no_mangle]
pub extern "C" fn memvid_last_error() -> *const c_char {
    LAST_ERROR.with(|e| {
        e.borrow()
            .as_ref()
            .map(|s| s.as_ptr())
            .unwrap_or(ptr::null())
    })
}

// Helper functions

unsafe fn ptr_to_str(ptr: *const c_char) -> Option<&'static str> {
    if ptr.is_null() {
        return None;
    }
    CStr::from_ptr(ptr).to_str().ok()
}

fn card_to_json_string(card: &MemoryCard) -> *mut c_char {
    let json = serde_json::json!({
        "id": card.id,
        "kind": card.kind.as_str(),
        "entity": card.entity,
        "slot": card.slot,
        "value": card.value,
        "version_relation": card.version_relation.as_str(),
        "polarity": card.polarity.map(|p| p.to_string()),
        "event_date": card.event_date,
        "created_at": card.created_at,
    });

    match CString::new(json.to_string()) {
        Ok(cstr) => cstr.into_raw(),
        Err(_) => ptr::null_mut(),
    }
}

fn cards_to_json_array(cards: &[&MemoryCard]) -> *mut c_char {
    let json_array: Vec<serde_json::Value> = cards
        .iter()
        .map(|card| {
            serde_json::json!({
                "id": card.id,
                "kind": card.kind.as_str(),
                "entity": card.entity,
                "slot": card.slot,
                "value": card.value,
                "version_relation": card.version_relation.as_str(),
                "polarity": card.polarity.map(|p| p.to_string()),
                "event_date": card.event_date,
                "created_at": card.created_at,
            })
        })
        .collect();

    match CString::new(serde_json::to_string(&json_array).unwrap_or_else(|_| "[]".to_string())) {
        Ok(cstr) => cstr.into_raw(),
        Err(_) => ptr::null_mut(),
    }
}
