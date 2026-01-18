package com.filesurf.service;

import com.filesurf.db.BlogDatabaseManager;
import com.filesurf.model.*;
import jakarta.enterprise.context.ApplicationScoped;
import jakarta.inject.Inject;
import org.eclipse.microprofile.config.inject.ConfigProperty;

import java.time.LocalDateTime;
import java.util.List;
import java.util.Optional;
import java.util.regex.Pattern;
import java.util.logging.Logger;

@ApplicationScoped
public class BlogService {
    private static final Logger LOGGER = Logger.getLogger(BlogService.class.getName());

    @Inject
    BlogDatabaseManager db;

    @ConfigProperty(name = "blog.posts.per-page", defaultValue = "10")
    int postsPerPage;

    @ConfigProperty(name = "blog.site.url", defaultValue = "https://filesurf.example.com")
    String siteUrl;

    @ConfigProperty(name = "blog.site.name", defaultValue = "FileSurf Blog")
    String siteName;

    @ConfigProperty(name = "blog.site.description", defaultValue = "AI-powered file management and automation insights")
    String siteDescription;

    // ==================== SLUG GENERATION ====================

    /**
     * Generate a URL-friendly slug from a title
     */
    public String generateSlug(String title) {
        if (title == null || title.isBlank()) {
            return "post-" + System.currentTimeMillis();
        }
        
        String slug = title.toLowerCase()
                .replaceAll("[^a-z0-9\\s-]", "")
                .replaceAll("\\s+", "-")
                .replaceAll("-+", "-")
                .replaceAll("^-|-$", "");
        
        if (slug.isEmpty()) {
            slug = "post-" + System.currentTimeMillis();
        }
        
        // Ensure uniqueness
        try {
            if (db.getPostBySlug(slug) != null) {
                slug = slug + "-" + System.currentTimeMillis();
            }
        } catch (Exception e) {
            LOGGER.warning("Could not check slug uniqueness: " + e.getMessage());
        }
        
        return slug;
    }

    // ==================== AUTHOR OPERATIONS ====================

    public BlogAuthor createAuthor(String name, String email, String bio, String avatarUrl) throws Exception {
        BlogAuthor author = new BlogAuthor(name, email, bio, avatarUrl);
        return db.createAuthor(author);
    }

    public BlogAuthor getAuthor(int id) throws Exception {
        return db.getAuthorById(id);
    }

    public BlogAuthor getAuthorByEmail(String email) throws Exception {
        return db.getAuthorByEmail(email);
    }

    public List<BlogAuthor> listAuthors() throws Exception {
        return db.listAuthors();
    }

    public void updateAuthor(BlogAuthor author) throws Exception {
        db.updateAuthor(author);
    }

    public void deleteAuthor(int id) throws Exception {
        db.deleteAuthor(id);
    }

    // ==================== CATEGORY OPERATIONS ====================

    public BlogCategory createCategory(String name, String description) throws Exception {
        String slug = generateSlug(name);
        // Ensure unique slug
        if (db.getCategoryBySlug(slug) != null) {
            slug = slug + "-" + System.currentTimeMillis();
        }
        BlogCategory category = new BlogCategory(name, slug, description);
        return db.createCategory(category);
    }

    public BlogCategory getCategory(int id) throws Exception {
        return db.getCategoryById(id);
    }

    public BlogCategory getCategoryBySlug(String slug) throws Exception {
        return db.getCategoryBySlug(slug);
    }

    public List<BlogCategory> listCategories() throws Exception {
        return db.listCategories();
    }

    public void updateCategory(BlogCategory category) throws Exception {
        db.updateCategory(category);
    }

    public void deleteCategory(int id) throws Exception {
        db.deleteCategory(id);
    }

    // ==================== TAG OPERATIONS ====================

    public BlogTag createTag(String name) throws Exception {
        String slug = generateSlug(name);
        // Ensure unique slug
        if (db.getTagBySlug(slug) != null) {
            slug = slug + "-" + System.currentTimeMillis();
        }
        BlogTag tag = new BlogTag(name, slug);
        return db.createTag(tag);
    }

    public BlogTag getTag(int id) throws Exception {
        return db.getTagById(id);
    }

    public BlogTag getTagBySlug(String slug) throws Exception {
        return db.getTagBySlug(slug);
    }

    public List<BlogTag> listTags() throws Exception {
        return db.listTags();
    }

    public List<BlogTag> getTagsForPost(int postId) throws Exception {
        return db.getTagsByPost(postId);
    }

    public void updateTag(BlogTag tag) throws Exception {
        db.updateTag(tag);
    }

    public void deleteTag(int id) throws Exception {
        db.deleteTag(id);
    }

    // ==================== POST OPERATIONS ====================

    /**
     * Create a new blog post with automatic slug generation
     */
    public BlogPost createPost(String title, String content, Integer authorId, Integer categoryId,
                               String excerpt, List<Integer> tagIds, String featuredImageUrl) throws Exception {
        String slug = generateSlug(title);
        String metaTitle = title.length() > 60 ? title.substring(0, 57) + "..." : title;
        
        BlogPost post = new BlogPost();
        post.setTitle(title);
        post.setSlug(slug);
        post.setContent(content);
        post.setAuthorId(authorId);
        post.setCategoryId(categoryId);
        post.setExcerpt(excerpt);
        post.setFeaturedImageUrl(featuredImageUrl);
        post.setMetaTitle(metaTitle);
        post.setStatus("draft");
        post.setReadingTimeMinutes(calculateReadingTime(content));
        
        BlogPost created = db.createPost(post);
        
        // Add tags if provided
        if (tagIds != null && !tagIds.isEmpty()) {
            for (Integer tagId : tagIds) {
                db.addTagToPost(created.getId(), tagId);
            }
        }
        
        return getPost(created.getId());
    }

    /**
     * Publish a post
     */
    public BlogPost publishPost(int postId) throws Exception {
        BlogPost post = db.getPostById(postId);
        if (post == null) {
            throw new IllegalArgumentException("Post not found: " + postId);
        }
        post.setStatus("published");
        post.setPublishedAt(LocalDateTime.now());
        db.updatePost(post);
        return getPost(postId);
    }

    /**
     * Unpublish a post (back to draft)
     */
    public BlogPost unpublishPost(int postId) throws Exception {
        BlogPost post = db.getPostById(postId);
        if (post == null) {
            throw new IllegalArgumentException("Post not found: " + postId);
        }
        post.setStatus("draft");
        db.updatePost(post);
        return getPost(postId);
    }

    public BlogPost getPost(int id) throws Exception {
        return db.getPostById(id);
    }

    public BlogPost getPost(String slug) throws Exception {
        return db.getPostBySlug(slug);
    }

    /**
     * Get paginated published posts
     */
    public PaginatedResult<BlogPost> getPublishedPosts(int page) throws Exception {
        int offset = page * postsPerPage;
        List<BlogPost> posts = db.getPublishedPosts(postsPerPage + 1, offset);
        boolean hasMore = posts.size() > postsPerPage;
        if (hasMore) {
            posts = posts.subList(0, postsPerPage);
        }
        int total = db.countPublishedPosts();
        return new PaginatedResult<>(posts, page, postsPerPage, total, hasMore);
    }

    /**
     * Get posts by category
     */
    public PaginatedResult<BlogPost> getPostsByCategory(String categorySlug, int page) throws Exception {
        BlogCategory category = db.getCategoryBySlug(categorySlug);
        if (category == null) {
            return new PaginatedResult<>(List.of(), page, postsPerPage, 0, false);
        }
        int offset = page * postsPerPage;
        List<BlogPost> posts = db.getPostsByCategory(category.getId(), postsPerPage + 1, offset);
        boolean hasMore = posts.size() > postsPerPage;
        if (hasMore) {
            posts = posts.subList(0, postsPerPage);
        }
        return new PaginatedResult<>(posts, page, postsPerPage, posts.size(), hasMore);
    }

    /**
     * Get posts by tag
     */
    public PaginatedResult<BlogPost> getPostsByTag(String tagSlug, int page) throws Exception {
        BlogTag tag = db.getTagBySlug(tagSlug);
        if (tag == null) {
            return new PaginatedResult<>(List.of(), page, postsPerPage, 0, false);
        }
        int offset = page * postsPerPage;
        List<BlogPost> posts = db.getPostsByTag(tag.getId(), postsPerPage + 1, offset);
        boolean hasMore = posts.size() > postsPerPage;
        if (hasMore) {
            posts = posts.subList(0, postsPerPage);
        }
        return new PaginatedResult<>(posts, page, postsPerPage, posts.size(), hasMore);
    }

    /**
     * Get posts by author
     */
    public PaginatedResult<BlogPost> getPostsByAuthor(int authorId, int page) throws Exception {
        int offset = page * postsPerPage;
        List<BlogPost> posts = db.getPostsByAuthor(authorId, postsPerPage + 1, offset);
        boolean hasMore = posts.size() > postsPerPage;
        if (hasMore) {
            posts = posts.subList(0, postsPerPage);
        }
        return new PaginatedResult<>(posts, page, postsPerPage, posts.size(), hasMore);
    }

    /**
     * Search posts
     */
    public PaginatedResult<BlogPost> searchPosts(String query, int page) throws Exception {
        int offset = page * postsPerPage;
        List<BlogPost> posts = db.searchPosts(query, postsPerPage + 1, offset);
        boolean hasMore = posts.size() > postsPerPage;
        if (hasMore) {
            posts = posts.subList(0, postsPerPage);
        }
        return new PaginatedResult<>(posts, page, postsPerPage, posts.size(), hasMore);
    }

    public void updatePost(BlogPost post) throws Exception {
        post.setUpdatedAt(LocalDateTime.now());
        post.setReadingTimeMinutes(calculateReadingTime(post.getContent()));
        db.updatePost(post);
    }

    public void deletePost(int id) throws Exception {
        db.deletePost(id);
    }

    /**
     * Set tags for a post (replaces existing tags)
     */
    public void setPostTags(int postId, List<Integer> tagIds) throws Exception {
        db.setPostTags(postId, tagIds);
    }

    /**
     * Add a tag to a post
     */
    public void addTagToPost(int postId, int tagId) throws Exception {
        db.addTagToPost(postId, tagId);
    }

    /**
     * Remove a tag from a post
     */
    public void removeTagFromPost(int postId, int tagId) throws Exception {
        db.removeTagFromPost(postId, tagId);
    }

    // ==================== ANALYTICS OPERATIONS ====================

    /**
     * Track a view for a post
     */
    public void trackView(int postId, String visitorIp) throws Exception {
        db.incrementView(postId, visitorIp);
    }

    /**
     * Get view count for a post
     */
    public int getViewCount(int postId) throws Exception {
        return db.getPostViews(postId);
    }

    /**
     * Get popular posts
     */
    public List<BlogPost> getPopularPosts(int limit) throws Exception {
        return db.getPopularPosts(limit);
    }

    /**
     * Get daily stats for a post
     */
    public List<int[]> getDailyStats(int postId, int days) throws Exception {
        return db.getDailyStats(postId, days);
    }

    // ==================== SEO HELPERS ====================

    /**
     * Generate canonical URL for a post
     */
    public String getCanonicalUrl(BlogPost post) {
        if (post.getCanonicalUrl() != null && !post.getCanonicalUrl().isBlank()) {
            return post.getCanonicalUrl();
        }
        return siteUrl + "/blog/" + post.getSlug();
    }

    /**
     * Generate meta description from content
     */
    public String generateMetaDescription(String content, int maxLength) {
        if (content == null) return "";
        String plainText = content.replaceAll("<[^>]*>", "").replaceAll("\\s+", " ").trim();
        if (plainText.length() <= maxLength) {
            return plainText;
        }
        return plainText.substring(0, maxLength - 3).trim() + "...";
    }

    /**
     * Generate structured data (JSON-LD) for a blog post
     */
    public String generateStructuredData(BlogPost post) {
        StringBuilder sb = new StringBuilder();
        sb.append("{");
        sb.append("\"@context\": \"https://schema.org\",");
        sb.append("\"@type\": \"BlogPosting\",");
        sb.append("\"headline\": \"").append(escapeJson(post.getTitle())).append("\",");
        
        if (post.getAuthor() != null) {
            sb.append("\"author\": {");
            sb.append("\"@type\": \"Person\",");
            sb.append("\"name\": \"").append(escapeJson(post.getAuthor().getName())).append("\"");
            if (post.getAuthor().getUrl() != null) {
                sb.append(",\"url\": \"").append(escapeJson(post.getAuthor().getUrl())).append("\"");
            }
            sb.append("},");
        }
        
        if (post.getFeaturedImageUrl() != null) {
            sb.append("\"image\": \"").append(escapeJson(post.getFeaturedImageUrl())).append("\",");
        }
        
        if (post.getPublishedAt() != null) {
            sb.append("\"datePublished\": \"").append(post.getPublishedAt()).append("\",");
        }
        if (post.getUpdatedAt() != null) {
            sb.append("\"dateModified\": \"").append(post.getUpdatedAt()).append("\",");
        }
        
        sb.append("\"description\": \"").append(escapeJson(getMetaDescription(post))).append("\",");
        sb.append("\"mainEntityOfPage\": {");
        sb.append("\"@type\": \"WebPage\",");
        sb.append("\"@id\": \"").append(escapeJson(getCanonicalUrl(post))).append("\"");
        sb.append("},");
        sb.append("\"publisher\": {");
        sb.append("\"@type\": \"Organization\",");
        sb.append("\"name\": \"").append(escapeJson(siteName)).append("\"");
        if (post.getFeaturedImageUrl() != null) {
            sb.append(",\"logo\": {\"@type\": \"ImageObject\",\"url\": \"").append(escapeJson(post.getFeaturedImageUrl())).append("\"}");
        }
        sb.append("}");
        sb.append("}");
        
        return sb.toString();
    }

    /**
     * Generate meta description for a post
     */
    public String getMetaDescription(BlogPost post) {
        if (post.getMetaDescription() != null && !post.getMetaDescription().isBlank()) {
            return post.getMetaDescription();
        }
        return generateMetaDescription(post.getExcerpt() != null ? post.getExcerpt() : post.getContent(), 160);
    }

    /**
     * Generate meta title for a post
     */
    public String getMetaTitle(BlogPost post) {
        if (post.getMetaTitle() != null && !post.getMetaTitle().isBlank()) {
            return post.getMetaTitle();
        }
        String title = post.getTitle();
        if (title.length() > 60) {
            title = title.substring(0, 57) + "...";
        }
        return title + " | " + siteName;
    }

    /**
     * Get keywords for a post
     */
    public String getKeywords(BlogPost post) {
        if (post.getMetaKeywords() != null && !post.getMetaKeywords().isBlank()) {
            return post.getMetaKeywords();
        }
        // Generate from tags
        if (post.getTags() != null && !post.getTags().isEmpty()) {
            return post.getTags().stream()
                    .map(BlogTag::getName)
                    .limit(5)
                    .collect(java.util.stream.Collectors.joining(", "));
        }
        return "";
    }

    // ==================== UTILITY METHODS ====================

    /**
     * Calculate reading time in minutes
     */
    private int calculateReadingTime(String content) {
        if (content == null || content.isBlank()) {
            return 1;
        }
        // Strip HTML tags
        String plainText = content.replaceAll("<[^>]*>", "").replaceAll("\\s+", " ");
        int wordCount = plainText.split("\\s+").length;
        // Average reading speed: 200 words per minute
        return Math.max(1, (int) Math.ceil(wordCount / 200.0));
    }

    /**
     * Escape special characters for JSON
     */
    private String escapeJson(String str) {
        if (str == null) return "";
        return str.replace("\\", "\\\\")
                  .replace("\"", "\\\"")
                  .replace("\n", "\\n")
                  .replace("\r", "\\r")
                  .replace("\t", "\\t");
    }

    // ==================== HELPER CLASSES ====================

    public static class PaginatedResult<T> {
        private final List<T> items;
        private final int page;
        private final int perPage;
        private final int total;
        private final boolean hasMore;
        private final int totalPages;

        public PaginatedResult(List<T> items, int page, int perPage, int total, boolean hasMore) {
            this.items = items;
            this.page = page;
            this.perPage = perPage;
            this.total = total;
            this.hasMore = hasMore;
            this.totalPages = (int) Math.ceil((double) total / perPage);
        }

        public List<T> getItems() { return items; }
        public int getPage() { return page; }
        public int getPerPage() { return perPage; }
        public int getTotal() { return total; }
        public boolean hasMore() { return hasMore; }
        public int getTotalPages() { return totalPages; }
        
        public boolean hasPrevious() { return page > 0; }
        public int getPreviousPage() { return Math.max(0, page - 1); }
        public int getNextPage() { return hasMore ? page + 1 : -1; }
    }
}
