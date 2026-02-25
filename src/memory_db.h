/*
 * memory_db.h - SQLite-based persistent memory system with FTS and vector search
 *
 * This module provides memory storage using SQLite3 with:
 * - Full-text search (FTS5) for text-based memory retrieval
 * - sqlite-vector extension for similarity-based vector search
 *
 * Replaces the previous memvid-based memory system.
 */

#ifndef MEMORY_DB_H
#define MEMORY_DB_H

#include <stdint.h>
#include <stddef.h>
#include <sqlite3.h>

/* Memory kind enum values */
typedef enum {
    MEMORY_KIND_FACT = 0,
    MEMORY_KIND_PREFERENCE = 1,
    MEMORY_KIND_EVENT = 2,
    MEMORY_KIND_PROFILE = 3,
    MEMORY_KIND_RELATIONSHIP = 4,
    MEMORY_KIND_GOAL = 5
} MemoryKind;

/* Relation enum values */
typedef enum {
    MEMORY_RELATION_SETS = 0,
    MEMORY_RELATION_UPDATES = 1,
    MEMORY_RELATION_EXTENDS = 2,
    MEMORY_RELATION_RETRACTS = 3
} MemoryRelation;

/* Opaque handle for memory database */
typedef struct MemoryDB MemoryDB;

/* Memory card structure for search results */
typedef struct {
    int64_t card_id;
    char *entity;
    char *slot;
    char *value;
    char *kind;
    char *relation;
    char *timestamp;
    double score;       /* For search ranking */
} MemoryCard;

/* Search result structure */
typedef struct {
    MemoryCard *cards;
    size_t count;
    size_t capacity;
} MemorySearchResult;

/*
 * Global instance management (thread-safe)
 */

/*
 * Initialize the global memory database instance.
 *
 * @param path Path to database file, or NULL for default (.klawed/memory.db)
 * @return 0 on success, -1 on failure
 */
int memory_db_init_global(const char *path);

/*
 * Clean up the global memory database instance.
 */
void memory_db_cleanup_global(void);

/*
 * Get the global memory database handle.
 *
 * @return Handle pointer, or NULL if not initialized
 */
MemoryDB* memory_db_get_global(void);

/*
 * Check if memory database is available (always returns 1 now).
 *
 * @return 1 if available, 0 otherwise
 */
int memory_db_is_available(void);

/*
 * Open a specific memory database file for one-time use.
 * The caller is responsible for closing the returned handle.
 *
 * @param path Path to the database file. If NULL, returns the global handle.
 * @return Handle pointer on success (caller must close), global handle if path is NULL, NULL on failure
 */
MemoryDB* memory_db_open_for_path(const char *path);

/*
 * Core memory database operations
 */

/*
 * Open a memory database.
 *
 * @param path Path to the .db file
 * @return Handle pointer on success, NULL on failure
 */
MemoryDB* memory_db_open(const char *path);

/*
 * Close a memory database.
 *
 * @param db Handle to close
 */
void memory_db_close(MemoryDB *db);

/*
 * Store a memory card.
 *
 * @param db Memory database handle
 * @param entity Entity identifier (e.g., "user", "project.klawed")
 * @param slot Attribute slot (e.g., "employer", "preferred_language")
 * @param value Value to store
 * @param kind Memory kind
 * @param relation Relation type
 * @return Card ID on success (>= 0), -1 on failure
 */
int64_t memory_db_store(MemoryDB *db, const char *entity,
                        const char *slot, const char *value,
                        MemoryKind kind, MemoryRelation relation);

/*
 * Get the current value for an entity:slot pair.
 *
 * @param db Memory database handle
 * @param entity Entity identifier
 * @param slot Attribute slot
 * @return MemoryCard on success (caller must free with memory_db_free_card), NULL if not found
 */
MemoryCard* memory_db_get_current(MemoryDB *db, const char *entity, const char *slot);

/*
 * Search memories by text query using FTS5.
 *
 * @param db Memory database handle
 * @param query Search query string
 * @param top_k Maximum number of results
 * @return Search result structure on success (caller must free with memory_db_free_result), NULL on error
 */
MemorySearchResult* memory_db_search(MemoryDB *db, const char *query, uint32_t top_k);

/*
 * Get all memories for an entity.
 *
 * @param db Memory database handle
 * @param entity Entity identifier
 * @return Search result structure on success (caller must free with memory_db_free_result), NULL on error
 */
MemorySearchResult* memory_db_get_entity_memories(MemoryDB *db, const char *entity);

/*
 * Free a memory card.
 *
 * @param card Card to free (can be NULL)
 */
void memory_db_free_card(MemoryCard *card);

/*
 * Free a search result.
 *
 * @param result Result to free (can be NULL)
 */
void memory_db_free_result(MemorySearchResult *result);

/*
 * Get the last error message.
 *
 * @param db Memory database handle (can be NULL for global error)
 * @return Error message string (do not free)
 */
const char* memory_db_last_error(MemoryDB *db);

/*
 * Convert kind enum to string.
 *
 * @param kind Memory kind
 * @return String representation
 */
const char* memory_db_kind_to_string(MemoryKind kind);

/*
 * Convert string to kind enum.
 *
 * @param str Kind string
 * @return Memory kind, or MEMORY_KIND_FACT if invalid
 */
MemoryKind memory_db_string_to_kind(const char *str);

/*
 * Convert relation enum to string.
 *
 * @param relation Memory relation
 * @return String representation
 */
const char* memory_db_relation_to_string(MemoryRelation relation);

/*
 * Convert string to relation enum.
 *
 * @param str Relation string
 * @return Memory relation, or MEMORY_RELATION_SETS if invalid
 */
MemoryRelation memory_db_string_to_relation(const char *str);

/*
 * Vector search operations (requires sqlite-vector extension)
 */

/*
 * Check if sqlite-vector extension is available.
 *
 * @param db Memory database handle
 * @return 1 if available, 0 otherwise
 */
int memory_db_vector_available(MemoryDB *db);

/*
 * Store a memory with vector embedding for similarity search.
 *
 * @param db Memory database handle
 * @param entity Entity identifier
 * @param slot Attribute slot
 * @param value Value to store
 * @param kind Memory kind
 * @param relation Relation type
 * @param embedding Vector embedding as array of floats (can be NULL)
 * @param embedding_dim Dimension of embedding (0 if no embedding)
 * @return Card ID on success (>= 0), -1 on failure
 */
int64_t memory_db_store_with_embedding(MemoryDB *db, const char *entity,
                                       const char *slot, const char *value,
                                       MemoryKind kind, MemoryRelation relation,
                                       const float *embedding, size_t embedding_dim);

/*
 * Search memories by vector similarity.
 *
 * @param db Memory database handle
 * @param query_embedding Query vector embedding
 * @param embedding_dim Dimension of embedding
 * @param top_k Maximum number of results
 * @return Search result structure on success (caller must free with memory_db_free_result), NULL on error
 */
MemorySearchResult* memory_db_vector_search(MemoryDB *db, const float *query_embedding,
                                            size_t embedding_dim, uint32_t top_k);

/*
 * Initialize vector search for a memory database (creates vector table and index).
 * Must be called before using vector operations.
 *
 * @param db Memory database handle
 * @param embedding_dim Dimension of embeddings to store
 * @return 0 on success, -1 on failure
 */
int memory_db_vector_init(MemoryDB *db, size_t embedding_dim);

#endif /* MEMORY_DB_H */
