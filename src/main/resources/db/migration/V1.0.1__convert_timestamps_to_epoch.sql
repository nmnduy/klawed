-- Migration: Convert TIMESTAMP columns to INTEGER (epoch seconds)
-- This migration converts existing TIMESTAMP columns to INTEGER for consistent epoch-based timestamps
-- Safe to run: Uses CREATE TABLE + INSERT pattern which works whether columns are TIMESTAMP or INTEGER

-- Step 1: Migrate authors table
CREATE TABLE IF NOT EXISTS authors_new (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    name TEXT NOT NULL,
    email TEXT UNIQUE,
    bio TEXT,
    avatar_url TEXT,
    created_at INTEGER NOT NULL DEFAULT (strftime('%s', 'now')),
    updated_at INTEGER NOT NULL DEFAULT (strftime('%s', 'now'))
);

INSERT INTO authors_new (id, name, email, bio, avatar_url, created_at, updated_at)
SELECT 
    id, 
    name, 
    email, 
    bio, 
    avatar_url,
    CASE 
        WHEN typeof(created_at) = 'integer' THEN created_at
        ELSE strftime('%s', created_at)
    END,
    CASE 
        WHEN typeof(updated_at) = 'integer' THEN updated_at
        ELSE strftime('%s', updated_at)
    END
FROM authors;

DROP TABLE IF EXISTS authors;
ALTER TABLE authors_new RENAME TO authors;

-- Step 2: Migrate categories table
CREATE TABLE IF NOT EXISTS categories_new (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    name TEXT NOT NULL UNIQUE,
    slug TEXT NOT NULL UNIQUE,
    description TEXT,
    created_at INTEGER NOT NULL DEFAULT (strftime('%s', 'now'))
);

INSERT INTO categories_new (id, name, slug, description, created_at)
SELECT 
    id, 
    name, 
    slug, 
    description,
    CASE 
        WHEN typeof(created_at) = 'integer' THEN created_at
        ELSE strftime('%s', created_at)
    END
FROM categories;

DROP TABLE IF EXISTS categories;
ALTER TABLE categories_new RENAME TO categories;

-- Step 3: Migrate tags table
CREATE TABLE IF NOT EXISTS tags_new (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    name TEXT NOT NULL UNIQUE,
    slug TEXT NOT NULL UNIQUE,
    created_at INTEGER NOT NULL DEFAULT (strftime('%s', 'now'))
);

INSERT INTO tags_new (id, name, slug, created_at)
SELECT 
    id, 
    name, 
    slug,
    CASE 
        WHEN typeof(created_at) = 'integer' THEN created_at
        ELSE strftime('%s', created_at)
    END
FROM tags;

DROP TABLE IF EXISTS tags;
ALTER TABLE tags_new RENAME TO tags;

-- Step 4: Migrate blog_posts table
CREATE TABLE IF NOT EXISTS blog_posts_new (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    title TEXT NOT NULL,
    slug TEXT NOT NULL UNIQUE,
    excerpt TEXT,
    content TEXT NOT NULL,
    author_id INTEGER NOT NULL,
    category_id INTEGER,
    featured_image_url TEXT,
    meta_title TEXT,
    meta_description TEXT,
    meta_keywords TEXT,
    canonical_url TEXT,
    status TEXT NOT NULL DEFAULT 'draft',
    published_at INTEGER,
    created_at INTEGER NOT NULL DEFAULT (strftime('%s', 'now')),
    updated_at INTEGER NOT NULL DEFAULT (strftime('%s', 'now')),
    views INTEGER NOT NULL DEFAULT 0,
    reading_time_minutes INTEGER,
    FOREIGN KEY (author_id) REFERENCES authors(id) ON DELETE CASCADE,
    FOREIGN KEY (category_id) REFERENCES categories(id) ON DELETE SET NULL
);

INSERT INTO blog_posts_new (id, title, slug, excerpt, content, author_id, category_id, 
    featured_image_url, meta_title, meta_description, meta_keywords, canonical_url, 
    status, published_at, created_at, updated_at, views, reading_time_minutes)
SELECT 
    id, 
    title, 
    slug, 
    excerpt, 
    content, 
    author_id, 
    category_id,
    featured_image_url,
    meta_title,
    meta_description,
    meta_keywords,
    canonical_url,
    status,
    CASE 
        WHEN published_at IS NULL THEN NULL
        WHEN typeof(published_at) = 'integer' THEN published_at
        ELSE strftime('%s', published_at)
    END,
    CASE 
        WHEN typeof(created_at) = 'integer' THEN created_at
        ELSE strftime('%s', created_at)
    END,
    CASE 
        WHEN typeof(updated_at) = 'integer' THEN updated_at
        ELSE strftime('%s', updated_at)
    END,
    views,
    reading_time_minutes
FROM blog_posts;

DROP TABLE IF EXISTS blog_posts;
ALTER TABLE blog_posts_new RENAME TO blog_posts;

-- Step 5: Migrate post_tags table
CREATE TABLE IF NOT EXISTS post_tags_new (
    post_id INTEGER NOT NULL,
    tag_id INTEGER NOT NULL,
    created_at INTEGER NOT NULL DEFAULT (strftime('%s', 'now')),
    PRIMARY KEY (post_id, tag_id),
    FOREIGN KEY (post_id) REFERENCES blog_posts(id) ON DELETE CASCADE,
    FOREIGN KEY (tag_id) REFERENCES tags(id) ON DELETE CASCADE
);

INSERT INTO post_tags_new (post_id, tag_id, created_at)
SELECT 
    post_id, 
    tag_id,
    CASE 
        WHEN typeof(created_at) = 'integer' THEN created_at
        ELSE strftime('%s', created_at)
    END
FROM post_tags;

DROP TABLE IF EXISTS post_tags;
ALTER TABLE post_tags_new RENAME TO post_tags;

-- Step 6: Migrate post_analytics table
CREATE TABLE IF NOT EXISTS post_analytics_new (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    post_id INTEGER NOT NULL,
    date DATE NOT NULL,
    views INTEGER NOT NULL DEFAULT 0,
    unique_visitors INTEGER NOT NULL DEFAULT 0,
    created_at INTEGER NOT NULL DEFAULT (strftime('%s', 'now')),
    updated_at INTEGER NOT NULL DEFAULT (strftime('%s', 'now')),
    UNIQUE(post_id, date),
    FOREIGN KEY (post_id) REFERENCES blog_posts(id) ON DELETE CASCADE
);

INSERT INTO post_analytics_new (id, post_id, date, views, unique_visitors, created_at, updated_at)
SELECT 
    id, 
    post_id, 
    date, 
    views, 
    unique_visitors,
    CASE 
        WHEN typeof(created_at) = 'integer' THEN created_at
        ELSE strftime('%s', created_at)
    END,
    CASE 
        WHEN typeof(updated_at) = 'integer' THEN updated_at
        ELSE strftime('%s', updated_at)
    END
FROM post_analytics;

DROP TABLE IF EXISTS post_analytics;
ALTER TABLE post_analytics_new RENAME TO post_analytics;

-- Step 7: Recreate indexes
CREATE INDEX IF NOT EXISTS idx_blog_posts_slug ON blog_posts(slug);
CREATE INDEX IF NOT EXISTS idx_blog_posts_status_published ON blog_posts(status, published_at);
CREATE INDEX IF NOT EXISTS idx_blog_posts_category ON blog_posts(category_id);
CREATE INDEX IF NOT EXISTS idx_blog_posts_author ON blog_posts(author_id);
CREATE INDEX IF NOT EXISTS idx_blog_posts_created_at ON blog_posts(created_at);
CREATE INDEX IF NOT EXISTS idx_post_tags_post ON post_tags(post_id);
CREATE INDEX IF NOT EXISTS idx_post_tags_tag ON post_tags(tag_id);
CREATE INDEX IF NOT EXISTS idx_categories_slug ON categories(slug);
CREATE INDEX IF NOT EXISTS idx_tags_slug ON tags(slug);
CREATE INDEX IF NOT EXISTS idx_post_analytics_post_date ON post_analytics(post_id, date);

-- Step 8: Recreate triggers
DROP TRIGGER IF EXISTS update_blog_posts_timestamp;
CREATE TRIGGER update_blog_posts_timestamp 
AFTER UPDATE ON blog_posts 
BEGIN
    UPDATE blog_posts SET updated_at = strftime('%s', 'now') WHERE id = NEW.id;
END;

DROP TRIGGER IF EXISTS update_authors_timestamp;
CREATE TRIGGER update_authors_timestamp 
AFTER UPDATE ON authors 
BEGIN
    UPDATE authors SET updated_at = strftime('%s', 'now') WHERE id = NEW.id;
END;

DROP TRIGGER IF EXISTS update_post_analytics_timestamp;
CREATE TRIGGER update_post_analytics_timestamp 
AFTER UPDATE ON post_analytics 
BEGIN
    UPDATE post_analytics SET updated_at = strftime('%s', 'now') WHERE id = NEW.id;
END;
