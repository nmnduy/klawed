package com.filesurf.db;

import com.filesurf.model.*;
import jakarta.annotation.PostConstruct;
import jakarta.annotation.PreDestroy;
import jakarta.enterprise.context.ApplicationScoped;
import org.eclipse.microprofile.config.inject.ConfigProperty;

import java.io.BufferedReader;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.sql.*;
import java.util.ArrayList;
import java.util.List;
import java.util.logging.Logger;
import java.util.stream.Collectors;

@ApplicationScoped
public class BlogDatabaseManager {
    private static final Logger LOGGER = Logger.getLogger(BlogDatabaseManager.class.getName());

    @ConfigProperty(name = "blog.db.path", defaultValue = "./data/blog.db")
    String databasePath;

    private Connection connection;
    private final Object lock = new Object();

    @PostConstruct
    void init() throws SQLException {
        LOGGER.info("Initializing BlogDatabaseManager with database: " + databasePath);
        
        // Ensure parent directory exists
        java.nio.file.Path dbPath = java.nio.file.Paths.get(databasePath);
        if (dbPath.getParent() != null) {
            java.nio.file.Files.createDirectories(dbPath.getParent());
        }
        
        // Create single connection for blog database
        connection = DriverManager.getConnection("jdbc:sqlite:" + databasePath);

        // Set PRAGMAs for optimal SQLite performance
        try (Statement stmt = connection.createStatement()) {
            stmt.execute("PRAGMA journal_mode = WAL");
            stmt.execute("PRAGMA synchronous = NORMAL");
            stmt.execute("PRAGMA busy_timeout = 5000");
            stmt.execute("PRAGMA foreign_keys = ON");
            stmt.execute("PRAGMA cache_size = -2000");
            stmt.execute("PRAGMA mmap_size = 268435456");
            stmt.execute("PRAGMA temp_store = MEMORY");
            stmt.execute("PRAGMA encoding = 'UTF-8'");
        }

        // Initialize schema
        initializeSchema();

        LOGGER.info("BlogDatabaseManager initialized successfully");
    }

    private void initializeSchema() throws SQLException {
        try (InputStream is = getClass().getResourceAsStream("/db/migration/V1.0.0__create_blog_tables.sql")) {
            if (is == null) {
                throw new SQLException("Blog schema migration file not found");
            }
            String sql = new BufferedReader(new InputStreamReader(is))
                    .lines()
                    .collect(Collectors.joining("\n"));
            
            try (Statement stmt = connection.createStatement()) {
                stmt.execute(sql);
            }
        }
        LOGGER.info("Blog database schema initialized");
    }

    @PreDestroy
    void cleanup() {
        LOGGER.info("Cleaning up BlogDatabaseManager...");
        if (connection != null) {
            try {
                connection.close();
            } catch (SQLException e) {
                LOGGER.warning("Error closing blog SQLite connection: " + e.getMessage());
            }
        }
    }

    public <T> T execute(ConnectionConsumer<T> operation) throws SQLException {
        synchronized(lock) {
            return operation.accept(connection);
        }
    }

    public void execute(ConnectionOperation operation) throws SQLException {
        synchronized(lock) {
            operation.accept(connection);
        }
    }

    public <T> T executeInTransaction(ConnectionConsumer<T> operation) throws SQLException {
        return execute(conn -> {
            boolean autoCommit = conn.getAutoCommit();
            try {
                conn.setAutoCommit(false);
                T result = operation.accept(conn);
                conn.commit();
                return result;
            } catch (SQLException e) {
                conn.rollback();
                throw e;
            } finally {
                conn.setAutoCommit(autoCommit);
            }
        });
    }

    // ==================== AUTHOR OPERATIONS ====================

    public BlogAuthor createAuthor(BlogAuthor author) throws SQLException {
        return execute(conn -> {
            String sql = "INSERT INTO authors (name, email, bio, avatar_url) VALUES (?, ?, ?, ?)";
            try (PreparedStatement stmt = conn.prepareStatement(sql, Statement.RETURN_GENERATED_KEYS)) {
                stmt.setString(1, author.getName());
                stmt.setString(2, author.getEmail());
                stmt.setString(3, author.getBio());
                stmt.setString(4, author.getAvatarUrl());
                stmt.executeUpdate();
                
                try (ResultSet rs = stmt.getGeneratedKeys()) {
                    if (rs.next()) {
                        author.setId(rs.getInt(1));
                    }
                }
            }
            return author;
        });
    }

    public BlogAuthor getAuthorById(int id) throws SQLException {
        return execute(conn -> {
            String sql = "SELECT * FROM authors WHERE id = ?";
            try (PreparedStatement stmt = conn.prepareStatement(sql)) {
                stmt.setInt(1, id);
                try (ResultSet rs = stmt.executeQuery()) {
                    if (rs.next()) {
                        return mapAuthor(rs);
                    }
                }
            }
            return null;
        });
    }

    public BlogAuthor getAuthorByEmail(String email) throws SQLException {
        return execute(conn -> {
            String sql = "SELECT * FROM authors WHERE email = ?";
            try (PreparedStatement stmt = conn.prepareStatement(sql)) {
                stmt.setString(1, email);
                try (ResultSet rs = stmt.executeQuery()) {
                    if (rs.next()) {
                        return mapAuthor(rs);
                    }
                }
            }
            return null;
        });
    }

    public List<BlogAuthor> listAuthors() throws SQLException {
        return execute(conn -> {
            String sql = "SELECT * FROM authors ORDER BY name";
            try (PreparedStatement stmt = conn.prepareStatement(sql)) {
                try (ResultSet rs = stmt.executeQuery()) {
                    List<BlogAuthor> authors = new ArrayList<>();
                    while (rs.next()) {
                        authors.add(mapAuthor(rs));
                    }
                    return authors;
                }
            }
        });
    }

    public void updateAuthor(BlogAuthor author) throws SQLException {
        execute(conn -> {
            String sql = "UPDATE authors SET name = ?, email = ?, bio = ?, avatar_url = ? WHERE id = ?";
            try (PreparedStatement stmt = conn.prepareStatement(sql)) {
                stmt.setString(1, author.getName());
                stmt.setString(2, author.getEmail());
                stmt.setString(3, author.getBio());
                stmt.setString(4, author.getAvatarUrl());
                stmt.setInt(5, author.getId());
                stmt.executeUpdate();
            }
            return null;
        });
    }

    public void deleteAuthor(int id) throws SQLException {
        execute(conn -> {
            String sql = "DELETE FROM authors WHERE id = ?";
            try (PreparedStatement stmt = conn.prepareStatement(sql)) {
                stmt.setInt(1, id);
                stmt.executeUpdate();
            }
            return null;
        });
    }

    // ==================== CATEGORY OPERATIONS ====================

    public BlogCategory createCategory(BlogCategory category) throws SQLException {
        return execute(conn -> {
            String sql = "INSERT INTO categories (name, slug, description) VALUES (?, ?, ?)";
            try (PreparedStatement stmt = conn.prepareStatement(sql, Statement.RETURN_GENERATED_KEYS)) {
                stmt.setString(1, category.getName());
                stmt.setString(2, category.getSlug());
                stmt.setString(3, category.getDescription());
                stmt.executeUpdate();
                
                try (ResultSet rs = stmt.getGeneratedKeys()) {
                    if (rs.next()) {
                        category.setId(rs.getInt(1));
                    }
                }
            }
            return category;
        });
    }

    public BlogCategory getCategoryById(int id) throws SQLException {
        return execute(conn -> {
            String sql = "SELECT * FROM categories WHERE id = ?";
            try (PreparedStatement stmt = conn.prepareStatement(sql)) {
                stmt.setInt(1, id);
                try (ResultSet rs = stmt.executeQuery()) {
                    if (rs.next()) {
                        return mapCategory(rs);
                    }
                }
            }
            return null;
        });
    }

    public BlogCategory getCategoryBySlug(String slug) throws SQLException {
        return execute(conn -> {
            String sql = "SELECT * FROM categories WHERE slug = ?";
            try (PreparedStatement stmt = conn.prepareStatement(sql)) {
                stmt.setString(1, slug);
                try (ResultSet rs = stmt.executeQuery()) {
                    if (rs.next()) {
                        return mapCategory(rs);
                    }
                }
            }
            return null;
        });
    }

    public List<BlogCategory> listCategories() throws SQLException {
        return execute(conn -> {
            String sql = "SELECT * FROM categories ORDER BY name";
            try (PreparedStatement stmt = conn.prepareStatement(sql)) {
                try (ResultSet rs = stmt.executeQuery()) {
                    List<BlogCategory> categories = new ArrayList<>();
                    while (rs.next()) {
                        categories.add(mapCategory(rs));
                    }
                    return categories;
                }
            }
        });
    }

    public void updateCategory(BlogCategory category) throws SQLException {
        execute(conn -> {
            String sql = "UPDATE categories SET name = ?, slug = ?, description = ? WHERE id = ?";
            try (PreparedStatement stmt = conn.prepareStatement(sql)) {
                stmt.setString(1, category.getName());
                stmt.setString(2, category.getSlug());
                stmt.setString(3, category.getDescription());
                stmt.setInt(4, category.getId());
                stmt.executeUpdate();
            }
            return null;
        });
    }

    public void deleteCategory(int id) throws SQLException {
        execute(conn -> {
            String sql = "DELETE FROM categories WHERE id = ?";
            try (PreparedStatement stmt = conn.prepareStatement(sql)) {
                stmt.setInt(1, id);
                stmt.executeUpdate();
            }
            return null;
        });
    }

    // ==================== TAG OPERATIONS ====================

    public BlogTag createTag(BlogTag tag) throws SQLException {
        return execute(conn -> {
            String sql = "INSERT INTO tags (name, slug) VALUES (?, ?)";
            try (PreparedStatement stmt = conn.prepareStatement(sql, Statement.RETURN_GENERATED_KEYS)) {
                stmt.setString(1, tag.getName());
                stmt.setString(2, tag.getSlug());
                stmt.executeUpdate();
                
                try (ResultSet rs = stmt.getGeneratedKeys()) {
                    if (rs.next()) {
                        tag.setId(rs.getInt(1));
                    }
                }
            }
            return tag;
        });
    }

    public BlogTag getTagById(int id) throws SQLException {
        return execute(conn -> {
            String sql = "SELECT * FROM tags WHERE id = ?";
            try (PreparedStatement stmt = conn.prepareStatement(sql)) {
                stmt.setInt(1, id);
                try (ResultSet rs = stmt.executeQuery()) {
                    if (rs.next()) {
                        return mapTag(rs);
                    }
                }
            }
            return null;
        });
    }

    public BlogTag getTagBySlug(String slug) throws SQLException {
        return execute(conn -> {
            String sql = "SELECT * FROM tags WHERE slug = ?";
            try (PreparedStatement stmt = conn.prepareStatement(sql)) {
                stmt.setString(1, slug);
                try (ResultSet rs = stmt.executeQuery()) {
                    if (rs.next()) {
                        return mapTag(rs);
                    }
                }
            }
            return null;
        });
    }

    public List<BlogTag> listTags() throws SQLException {
        return execute(conn -> {
            String sql = "SELECT * FROM tags ORDER BY name";
            try (PreparedStatement stmt = conn.prepareStatement(sql)) {
                try (ResultSet rs = stmt.executeQuery()) {
                    List<BlogTag> tags = new ArrayList<>();
                    while (rs.next()) {
                        tags.add(mapTag(rs));
                    }
                    return tags;
                }
            }
        });
    }

    public List<BlogTag> getTagsByPost(int postId) throws SQLException {
        return execute(conn -> {
            String sql = "SELECT t.* FROM tags t " +
                        "JOIN post_tags pt ON t.id = pt.tag_id " +
                        "WHERE pt.post_id = ? ORDER BY t.name";
            try (PreparedStatement stmt = conn.prepareStatement(sql)) {
                stmt.setInt(1, postId);
                try (ResultSet rs = stmt.executeQuery()) {
                    List<BlogTag> tags = new ArrayList<>();
                    while (rs.next()) {
                        tags.add(mapTag(rs));
                    }
                    return tags;
                }
            }
        });
    }

    public void updateTag(BlogTag tag) throws SQLException {
        execute(conn -> {
            String sql = "UPDATE tags SET name = ?, slug = ? WHERE id = ?";
            try (PreparedStatement stmt = conn.prepareStatement(sql)) {
                stmt.setString(1, tag.getName());
                stmt.setString(2, tag.getSlug());
                stmt.setInt(3, tag.getId());
                stmt.executeUpdate();
            }
            return null;
        });
    }

    public void deleteTag(int id) throws SQLException {
        execute(conn -> {
            String sql = "DELETE FROM tags WHERE id = ?";
            try (PreparedStatement stmt = conn.prepareStatement(sql)) {
                stmt.setInt(1, id);
                stmt.executeUpdate();
            }
            return null;
        });
    }

    // ==================== POST OPERATIONS ====================

    public BlogPost createPost(BlogPost post) throws SQLException {
        return executeInTransaction(conn -> {
            String sql = "INSERT INTO blog_posts (title, slug, excerpt, content, author_id, category_id, " +
                        "featured_image_url, meta_title, meta_description, meta_keywords, canonical_url, " +
                        "status, published_at, reading_time_minutes) " +
                        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)";
            try (PreparedStatement stmt = conn.prepareStatement(sql, Statement.RETURN_GENERATED_KEYS)) {
                stmt.setString(1, post.getTitle());
                stmt.setString(2, post.getSlug());
                stmt.setString(3, post.getExcerpt());
                stmt.setString(4, post.getContent());
                stmt.setInt(5, post.getAuthorId());
                stmt.setInt(6, post.getCategoryId());
                stmt.setString(7, post.getFeaturedImageUrl());
                stmt.setString(8, post.getMetaTitle());
                stmt.setString(9, post.getMetaDescription());
                stmt.setString(10, post.getMetaKeywords());
                stmt.setString(11, post.getCanonicalUrl());
                stmt.setString(12, post.getStatus());
                if (post.getPublishedAt() != null) {
                    stmt.setTimestamp(13, Timestamp.valueOf(post.getPublishedAt()));
                } else {
                    stmt.setNull(13, Types.TIMESTAMP);
                }
                stmt.setInt(14, post.getReadingTimeMinutes());
                stmt.executeUpdate();
                
                try (ResultSet rs = stmt.getGeneratedKeys()) {
                    if (rs.next()) {
                        post.setId(rs.getInt(1));
                    }
                }
            }
            return post;
        });
    }

    public BlogPost getPostById(int id) throws SQLException {
        return execute(conn -> {
            String sql = "SELECT * FROM blog_posts WHERE id = ?";
            try (PreparedStatement stmt = conn.prepareStatement(sql)) {
                stmt.setInt(1, id);
                try (ResultSet rs = stmt.executeQuery()) {
                    if (rs.next()) {
                        BlogPost post = mapPost(rs);
                        post.setAuthor(getAuthorById(post.getAuthorId()));
                        if (post.getCategoryId() != null) {
                            post.setCategory(getCategoryById(post.getCategoryId()));
                        }
                        post.setTags(getTagsByPost(post.getId()));
                        return post;
                    }
                }
            }
            return null;
        });
    }

    public BlogPost getPostBySlug(String slug) throws SQLException {
        return execute(conn -> {
            String sql = "SELECT * FROM blog_posts WHERE slug = ?";
            try (PreparedStatement stmt = conn.prepareStatement(sql)) {
                stmt.setString(1, slug);
                try (ResultSet rs = stmt.executeQuery()) {
                    if (rs.next()) {
                        BlogPost post = mapPost(rs);
                        post.setAuthor(getAuthorById(post.getAuthorId()));
                        if (post.getCategoryId() != null) {
                            post.setCategory(getCategoryById(post.getCategoryId()));
                        }
                        post.setTags(getTagsByPost(post.getId()));
                        return post;
                    }
                }
            }
            return null;
        });
    }

    public List<BlogPost> getPublishedPosts(int limit, int offset) throws SQLException {
        return execute(conn -> {
            String sql = "SELECT * FROM blog_posts WHERE status = 'published' " +
                        "AND published_at <= CURRENT_TIMESTAMP " +
                        "ORDER BY published_at DESC LIMIT ? OFFSET ?";
            try (PreparedStatement stmt = conn.prepareStatement(sql)) {
                stmt.setInt(1, limit);
                stmt.setInt(2, offset);
                try (ResultSet rs = stmt.executeQuery()) {
                    List<BlogPost> posts = new ArrayList<>();
                    while (rs.next()) {
                        BlogPost post = mapPost(rs);
                        post.setAuthor(getAuthorById(post.getAuthorId()));
                        if (post.getCategoryId() != null) {
                            post.setCategory(getCategoryById(post.getCategoryId()));
                        }
                        post.setTags(getTagsByPost(post.getId()));
                        posts.add(post);
                    }
                    return posts;
                }
            }
        });
    }

    public List<BlogPost> getPostsByCategory(int categoryId, int limit, int offset) throws SQLException {
        return execute(conn -> {
            String sql = "SELECT * FROM blog_posts WHERE category_id = ? AND status = 'published' " +
                        "AND published_at <= CURRENT_TIMESTAMP " +
                        "ORDER BY published_at DESC LIMIT ? OFFSET ?";
            try (PreparedStatement stmt = conn.prepareStatement(sql)) {
                stmt.setInt(1, categoryId);
                stmt.setInt(2, limit);
                stmt.setInt(3, offset);
                try (ResultSet rs = stmt.executeQuery()) {
                    List<BlogPost> posts = new ArrayList<>();
                    while (rs.next()) {
                        BlogPost post = mapPost(rs);
                        post.setAuthor(getAuthorById(post.getAuthorId()));
                        post.setTags(getTagsByPost(post.getId()));
                        posts.add(post);
                    }
                    return posts;
                }
            }
        });
    }

    public List<BlogPost> getPostsByTag(int tagId, int limit, int offset) throws SQLException {
        return execute(conn -> {
            String sql = "SELECT bp.* FROM blog_posts bp " +
                        "JOIN post_tags pt ON bp.id = pt.post_id " +
                        "WHERE pt.tag_id = ? AND bp.status = 'published' " +
                        "AND bp.published_at <= CURRENT_TIMESTAMP " +
                        "ORDER BY bp.published_at DESC LIMIT ? OFFSET ?";
            try (PreparedStatement stmt = conn.prepareStatement(sql)) {
                stmt.setInt(1, tagId);
                stmt.setInt(2, limit);
                stmt.setInt(3, offset);
                try (ResultSet rs = stmt.executeQuery()) {
                    List<BlogPost> posts = new ArrayList<>();
                    while (rs.next()) {
                        BlogPost post = mapPost(rs);
                        post.setAuthor(getAuthorById(post.getAuthorId()));
                        post.setTags(getTagsByPost(post.getId()));
                        posts.add(post);
                    }
                    return posts;
                }
            }
        });
    }

    public List<BlogPost> getPostsByAuthor(int authorId, int limit, int offset) throws SQLException {
        return execute(conn -> {
            String sql = "SELECT * FROM blog_posts WHERE author_id = ? AND status = 'published' " +
                        "AND published_at <= CURRENT_TIMESTAMP " +
                        "ORDER BY published_at DESC LIMIT ? OFFSET ?";
            try (PreparedStatement stmt = conn.prepareStatement(sql)) {
                stmt.setInt(1, authorId);
                stmt.setInt(2, limit);
                stmt.setInt(3, offset);
                try (ResultSet rs = stmt.executeQuery()) {
                    List<BlogPost> posts = new ArrayList<>();
                    while (rs.next()) {
                        BlogPost post = mapPost(rs);
                        post.setAuthor(getAuthorById(post.getAuthorId()));
                        if (post.getCategoryId() != null) {
                            post.setCategory(getCategoryById(post.getCategoryId()));
                        }
                        post.setTags(getTagsByPost(post.getId()));
                        posts.add(post);
                    }
                    return posts;
                }
            }
        });
    }

    public List<BlogPost> searchPosts(String query, int limit, int offset) throws SQLException {
        return execute(conn -> {
            String sql = "SELECT * FROM blog_posts WHERE status = 'published' " +
                        "AND published_at <= CURRENT_TIMESTAMP " +
                        "AND (title LIKE ? OR content LIKE ? OR excerpt LIKE ?) " +
                        "ORDER BY published_at DESC LIMIT ? OFFSET ?";
            try (PreparedStatement stmt = conn.prepareStatement(sql)) {
                String searchTerm = "%" + query + "%";
                stmt.setString(1, searchTerm);
                stmt.setString(2, searchTerm);
                stmt.setString(3, searchTerm);
                stmt.setInt(4, limit);
                stmt.setInt(5, offset);
                try (ResultSet rs = stmt.executeQuery()) {
                    List<BlogPost> posts = new ArrayList<>();
                    while (rs.next()) {
                        BlogPost post = mapPost(rs);
                        post.setAuthor(getAuthorById(post.getAuthorId()));
                        if (post.getCategoryId() != null) {
                            post.setCategory(getCategoryById(post.getCategoryId()));
                        }
                        post.setTags(getTagsByPost(post.getId()));
                        posts.add(post);
                    }
                    return posts;
                }
            }
        });
    }

    public int countPublishedPosts() throws SQLException {
        return execute(conn -> {
            String sql = "SELECT COUNT(*) FROM blog_posts WHERE status = 'published' AND published_at <= CURRENT_TIMESTAMP";
            try (PreparedStatement stmt = conn.prepareStatement(sql)) {
                try (ResultSet rs = stmt.executeQuery()) {
                    return rs.next() ? rs.getInt(1) : 0;
                }
            }
        });
    }

    public void updatePost(BlogPost post) throws SQLException {
        executeInTransaction(conn -> {
            String sql = "UPDATE blog_posts SET title = ?, slug = ?, excerpt = ?, content = ?, " +
                        "author_id = ?, category_id = ?, featured_image_url = ?, meta_title = ?, " +
                        "meta_description = ?, meta_keywords = ?, canonical_url = ?, status = ?, " +
                        "published_at = ?, reading_time_minutes = ? WHERE id = ?";
            try (PreparedStatement stmt = conn.prepareStatement(sql)) {
                stmt.setString(1, post.getTitle());
                stmt.setString(2, post.getSlug());
                stmt.setString(3, post.getExcerpt());
                stmt.setString(4, post.getContent());
                stmt.setInt(5, post.getAuthorId());
                stmt.setInt(6, post.getCategoryId());
                stmt.setString(7, post.getFeaturedImageUrl());
                stmt.setString(8, post.getMetaTitle());
                stmt.setString(9, post.getMetaDescription());
                stmt.setString(10, post.getMetaKeywords());
                stmt.setString(11, post.getCanonicalUrl());
                stmt.setString(12, post.getStatus());
                if (post.getPublishedAt() != null) {
                    stmt.setTimestamp(13, Timestamp.valueOf(post.getPublishedAt()));
                } else {
                    stmt.setNull(13, Types.TIMESTAMP);
                }
                stmt.setInt(14, post.getReadingTimeMinutes());
                stmt.setInt(15, post.getId());
                stmt.executeUpdate();
            }
            return null;
        });
    }

    public void deletePost(int id) throws SQLException {
        executeInTransaction(conn -> {
            // Delete post tags first
            String deleteTagsSql = "DELETE FROM post_tags WHERE post_id = ?";
            try (PreparedStatement stmt = conn.prepareStatement(deleteTagsSql)) {
                stmt.setInt(1, id);
                stmt.executeUpdate();
            }
            // Delete analytics
            String deleteAnalyticsSql = "DELETE FROM post_analytics WHERE post_id = ?";
            try (PreparedStatement stmt = conn.prepareStatement(deleteAnalyticsSql)) {
                stmt.setInt(1, id);
                stmt.executeUpdate();
            }
            // Delete post
            String deletePostSql = "DELETE FROM blog_posts WHERE id = ?";
            try (PreparedStatement stmt = conn.prepareStatement(deletePostSql)) {
                stmt.setInt(1, id);
                stmt.executeUpdate();
            }
            return null;
        });
    }

    // ==================== POST TAGS OPERATIONS ====================

    public void addTagToPost(int postId, int tagId) throws SQLException {
        execute(conn -> {
            String sql = "INSERT OR IGNORE INTO post_tags (post_id, tag_id) VALUES (?, ?)";
            try (PreparedStatement stmt = conn.prepareStatement(sql)) {
                stmt.setInt(1, postId);
                stmt.setInt(2, tagId);
                stmt.executeUpdate();
            }
            return null;
        });
    }

    public void removeTagFromPost(int postId, int tagId) throws SQLException {
        execute(conn -> {
            String sql = "DELETE FROM post_tags WHERE post_id = ? AND tag_id = ?";
            try (PreparedStatement stmt = conn.prepareStatement(sql)) {
                stmt.setInt(1, postId);
                stmt.setInt(2, tagId);
                stmt.executeUpdate();
            }
            return null;
        });
    }

    public void setPostTags(int postId, List<Integer> tagIds) throws SQLException {
        executeInTransaction(conn -> {
            // Delete existing tags
            String deleteSql = "DELETE FROM post_tags WHERE post_id = ?";
            try (PreparedStatement stmt = conn.prepareStatement(deleteSql)) {
                stmt.setInt(1, postId);
                stmt.executeUpdate();
            }
            // Add new tags
            String insertSql = "INSERT INTO post_tags (post_id, tag_id) VALUES (?, ?)";
            try (PreparedStatement stmt = conn.prepareStatement(insertSql)) {
                for (Integer tagId : tagIds) {
                    stmt.setInt(1, postId);
                    stmt.setInt(2, tagId);
                    stmt.addBatch();
                }
                stmt.executeBatch();
            }
            return null;
        });
    }

    // ==================== ANALYTICS OPERATIONS ====================

    public void incrementView(int postId, String visitorIp) throws SQLException {
        executeInTransaction(conn -> {
            String today = java.time.LocalDate.now().toString();
            
            // Increment total views on post
            String updateViewsSql = "UPDATE blog_posts SET views = views + 1 WHERE id = ?";
            try (PreparedStatement stmt = conn.prepareStatement(updateViewsSql)) {
                stmt.setInt(1, postId);
                stmt.executeUpdate();
            }
            
            // Update daily analytics
            String upsertAnalyticsSql = "INSERT INTO post_analytics (post_id, date, views, unique_visitors) " +
                                        "VALUES (?, ?, 1, 1) " +
                                        "ON CONFLICT(post_id, date) DO UPDATE SET views = views + 1";
            try (PreparedStatement stmt = conn.prepareStatement(upsertAnalyticsSql)) {
                stmt.setInt(1, postId);
                stmt.setString(2, today);
                stmt.executeUpdate();
            }
            return null;
        });
    }

    public int getPostViews(int postId) throws SQLException {
        return execute(conn -> {
            String sql = "SELECT views FROM blog_posts WHERE id = ?";
            try (PreparedStatement stmt = conn.prepareStatement(sql)) {
                stmt.setInt(1, postId);
                try (ResultSet rs = stmt.executeQuery()) {
                    if (rs.next()) {
                        return rs.getInt("views");
                    }
                }
            }
            return 0;
        });
    }

    public List<BlogPost> getPopularPosts(int limit) throws SQLException {
        return execute(conn -> {
            String sql = "SELECT * FROM blog_posts WHERE status = 'published' " +
                        "AND published_at <= CURRENT_TIMESTAMP " +
                        "ORDER BY views DESC LIMIT ?";
            try (PreparedStatement stmt = conn.prepareStatement(sql)) {
                stmt.setInt(1, limit);
                try (ResultSet rs = stmt.executeQuery()) {
                    List<BlogPost> posts = new ArrayList<>();
                    while (rs.next()) {
                        BlogPost post = mapPost(rs);
                        post.setAuthor(getAuthorById(post.getAuthorId()));
                        if (post.getCategoryId() != null) {
                            post.setCategory(getCategoryById(post.getCategoryId()));
                        }
                        posts.add(post);
                    }
                    return posts;
                }
            }
        });
    }

    public List<int[]> getDailyStats(int postId, int days) throws SQLException {
        return execute(conn -> {
            String sql = "SELECT date, views, unique_visitors FROM post_analytics " +
                        "WHERE post_id = ? ORDER BY date DESC LIMIT ?";
            try (PreparedStatement stmt = conn.prepareStatement(sql)) {
                stmt.setInt(1, postId);
                stmt.setInt(2, days);
                try (ResultSet rs = stmt.executeQuery()) {
                    List<int[]> stats = new ArrayList<>();
                    while (rs.next()) {
                        // Store date as epoch and views count
                        java.time.LocalDate date = java.time.LocalDate.parse(rs.getString("date"));
                        long epochDay = date.toEpochDay();
                        stats.add(new int[]{(int) epochDay, rs.getInt("views"), rs.getInt("unique_visitors")});
                    }
                    return stats;
                }
            }
        });
    }

    // ==================== HELPER METHODS ====================

    private BlogAuthor mapAuthor(ResultSet rs) throws SQLException {
        BlogAuthor author = new BlogAuthor();
        author.setId(rs.getInt("id"));
        author.setName(rs.getString("name"));
        author.setEmail(rs.getString("email"));
        author.setBio(rs.getString("bio"));
        author.setAvatarUrl(rs.getString("avatar_url"));
        author.setCreatedAt(rs.getTimestamp("created_at") != null ? 
            rs.getTimestamp("created_at").toLocalDateTime() : null);
        author.setUpdatedAt(rs.getTimestamp("updated_at") != null ? 
            rs.getTimestamp("updated_at").toLocalDateTime() : null);
        return author;
    }

    private BlogCategory mapCategory(ResultSet rs) throws SQLException {
        BlogCategory category = new BlogCategory();
        category.setId(rs.getInt("id"));
        category.setName(rs.getString("name"));
        category.setSlug(rs.getString("slug"));
        category.setDescription(rs.getString("description"));
        category.setCreatedAt(rs.getTimestamp("created_at") != null ? 
            rs.getTimestamp("created_at").toLocalDateTime() : null);
        return category;
    }

    private BlogTag mapTag(ResultSet rs) throws SQLException {
        BlogTag tag = new BlogTag();
        tag.setId(rs.getInt("id"));
        tag.setName(rs.getString("name"));
        tag.setSlug(rs.getString("slug"));
        tag.setCreatedAt(rs.getTimestamp("created_at") != null ? 
            rs.getTimestamp("created_at").toLocalDateTime() : null);
        return tag;
    }

    private BlogPost mapPost(ResultSet rs) throws SQLException {
        BlogPost post = new BlogPost();
        post.setId(rs.getInt("id"));
        post.setTitle(rs.getString("title"));
        post.setSlug(rs.getString("slug"));
        post.setExcerpt(rs.getString("excerpt"));
        post.setContent(rs.getString("content"));
        post.setAuthorId(rs.getInt("author_id"));
        post.setCategoryId(rs.getInt("category_id"));
        post.setFeaturedImageUrl(rs.getString("featured_image_url"));
        post.setMetaTitle(rs.getString("meta_title"));
        post.setMetaDescription(rs.getString("meta_description"));
        post.setMetaKeywords(rs.getString("meta_keywords"));
        post.setCanonicalUrl(rs.getString("canonical_url"));
        post.setStatus(rs.getString("status"));
        post.setPublishedAt(rs.getTimestamp("published_at") != null ? 
            rs.getTimestamp("published_at").toLocalDateTime() : null);
        post.setCreatedAt(rs.getTimestamp("created_at") != null ? 
            rs.getTimestamp("created_at").toLocalDateTime() : null);
        post.setUpdatedAt(rs.getTimestamp("updated_at") != null ? 
            rs.getTimestamp("updated_at").toLocalDateTime() : null);
        post.setViews(rs.getInt("views"));
        post.setReadingTimeMinutes(rs.getInt("reading_time_minutes"));
        return post;
    }

    @FunctionalInterface
    public interface ConnectionConsumer<T> {
        T accept(Connection conn) throws SQLException;
    }

    @FunctionalInterface
    public interface ConnectionOperation {
        void accept(Connection conn) throws SQLException;
    }
}
