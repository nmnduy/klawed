/*
 * memvid.h - Memvid FFI integration interface
 *
 * This header defines the interface for memvid integration.
 * When HAVE_MEMVID is defined, links against libmemvid_ffi.
 * Otherwise, functions return appropriate error values.
 */

#ifndef MEMVID_H
#define MEMVID_H

#include <stdint.h>
#include <stddef.h>

/* Opaque handle for memvid database */
typedef struct MemvidHandle MemvidHandle;

/* Memory kind enum values (match Rust MemoryKind) */
#define MEMVID_KIND_FACT         0
#define MEMVID_KIND_PREFERENCE   1
#define MEMVID_KIND_EVENT        2
#define MEMVID_KIND_PROFILE      3
#define MEMVID_KIND_RELATIONSHIP 4
#define MEMVID_KIND_GOAL         5

/* Relation enum values (match Rust Relation) */
#define MEMVID_RELATION_SETS     0
#define MEMVID_RELATION_UPDATES  1
#define MEMVID_RELATION_EXTENDS  2
#define MEMVID_RELATION_RETRACTS 3

/* Doctor status codes (match Rust DoctorStatus) */
#define MEMVID_DOCTOR_STATUS_CLEAN     0   /* File was already healthy */
#define MEMVID_DOCTOR_STATUS_HEALED    1   /* File was repaired successfully */
#define MEMVID_DOCTOR_STATUS_PARTIAL   2   /* Some repairs succeeded, some failed */
#define MEMVID_DOCTOR_STATUS_FAILED    3   /* Repair failed */
#define MEMVID_DOCTOR_STATUS_PLAN_ONLY 4   /* Dry run - only planned repairs */
#define MEMVID_DOCTOR_STATUS_ERROR    -1   /* Error running doctor */

/*
 * Global instance management (thread-safe)
 */

/*
 * Initialize the global memvid instance.
 *
 * @param path Path to memory file, or NULL for default (.klawed/memory.mv2)
 * @return 0 on success, -1 on failure
 */
int memvid_init_global(const char *path);

/*
 * Clean up the global memvid instance.
 */
void memvid_cleanup_global(void);

/*
 * Get the global memvid handle.
 *
 * @return Handle pointer, or NULL if not initialized
 */
MemvidHandle* memvid_get_global(void);

/*
 * Check if memvid support is available.
 *
 * @return 1 if available (built with HAVE_MEMVID), 0 otherwise
 */
int memvid_is_available(void);

/*
 * Core memvid operations
 */

/*
 * Open a memvid database.
 *
 * @param path Path to the .mv2 file
 * @return Handle pointer on success, NULL on failure
 */
MemvidHandle* memvid_open(const char *path);

/*
 * Close a memvid database.
 *
 * @param handle Handle to close
 */
void memvid_close(MemvidHandle *handle);

/*
 * Store a memory card.
 *
 * @param handle Memvid handle
 * @param entity Entity identifier (e.g., "user", "project.klawed")
 * @param slot Attribute slot (e.g., "employer", "preferred_language")
 * @param value Value to store
 * @param kind Memory kind (MEMVID_KIND_*)
 * @param relation Relation type (MEMVID_RELATION_*)
 * @return Card ID on success (>= 0), -1 on failure
 */
int64_t memvid_put_memory(MemvidHandle *handle, const char *entity,
                          const char *slot, const char *value,
                          uint8_t kind, uint8_t relation);

/*
 * Get the current value for an entity:slot pair.
 *
 * @param handle Memvid handle
 * @param entity Entity identifier
 * @param slot Attribute slot
 * @return JSON string on success (caller must free with memvid_free_string), NULL if not found
 */
char* memvid_get_current(MemvidHandle *handle, const char *entity, const char *slot);

/*
 * Search memories by text query.
 *
 * @param handle Memvid handle
 * @param query Search query string
 * @param top_k Maximum number of results
 * @return JSON array string on success (caller must free with memvid_free_string), NULL on error
 */
char* memvid_search(MemvidHandle *handle, const char *query, uint32_t top_k);

/*
 * Get all memories for an entity.
 *
 * @param handle Memvid handle
 * @param entity Entity identifier
 * @return JSON array string on success (caller must free with memvid_free_string), NULL on error
 */
char* memvid_get_entity_memories(MemvidHandle *handle, const char *entity);

/*
 * Commit pending changes to disk.
 *
 * @param handle Memvid handle
 * @return 0 on success, -1 on failure
 */
int memvid_commit(MemvidHandle *handle);

/*
 * Free a string returned by memvid functions.
 *
 * @param s String to free (can be NULL)
 */
void memvid_free_string(char *s);

/*
 * Get the last error message.
 *
 * @return Error message string (do not free)
 */
const char* memvid_last_error(void);

/*
 * Run doctor on a memvid file to detect and repair corruption.
 *
 * This function attempts to repair a corrupted .mv2 file by:
 * - Detecting and fixing header/footer pointer corruption
 * - Replaying pending WAL records
 * - Rebuilding indices if requested
 * - Zeroing corrupted WAL regions
 *
 * @param path Path to the .mv2 file to repair
 * @param rebuild_time_index If true (non-zero), force rebuild time index
 * @param rebuild_lex_index If true (non-zero), force rebuild lexical index
 * @param rebuild_vec_index If true (non-zero), force rebuild vector index
 * @return MEMVID_DOCTOR_STATUS_* code (see defines above)
 */
int memvid_doctor(const char *path, int rebuild_time_index,
                  int rebuild_lex_index, int rebuild_vec_index);

#endif /* MEMVID_H */
