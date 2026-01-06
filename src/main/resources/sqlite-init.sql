-- SQLite initialization script for optimal performance
-- These PRAGMAs are set on each new database connection

-- Memory mapping for faster I/O (256MB)
PRAGMA mmap_size = 268435456;

-- Optional: Set page size for better performance (usually default is fine)
-- PRAGMA page_size = 4096;

-- Optional: Temp store in memory for faster temporary operations
-- PRAGMA temp_store = MEMORY;