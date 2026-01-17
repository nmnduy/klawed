# Semrush Market Research Progress

## Current SQLite files (keyword-related)
- `data/semrush_keywords.db` — size 28 KB

### `semrush_keywords.db`
```sql
CREATE TABLE semrush_keywords (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    keyword TEXT NOT NULL UNIQUE,
    kd REAL NOT NULL,
    monthly_searches INTEGER,
    monthly_clicks INTEGER,
    source TEXT DEFAULT 'semrush',
    notes TEXT,
    created_at TEXT NOT NULL DEFAULT (datetime('now')),
    updated_at TEXT NOT NULL DEFAULT (datetime('now'))
);
CREATE INDEX idx_semrush_keywords_kd ON semrush_keywords(kd);
CREATE INDEX idx_semrush_keywords_searches ON semrush_keywords(monthly_searches);
CREATE INDEX idx_semrush_keywords_clicks ON semrush_keywords(monthly_clicks);
CREATE TRIGGER trg_semrush_keywords_updated_at
AFTER UPDATE ON semrush_keywords
FOR EACH ROW
BEGIN
    UPDATE semrush_keywords SET updated_at = datetime('now') WHERE id = OLD.id;
END;
```
