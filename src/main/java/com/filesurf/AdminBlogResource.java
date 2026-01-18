package com.filesurf;

import com.filesurf.model.*;
import com.filesurf.service.BlogService;
import com.filesurf.service.UserService;
import io.quarkus.qute.Template;
import io.quarkus.qute.TemplateInstance;
import jakarta.annotation.security.RolesAllowed;
import jakarta.inject.Inject;
import jakarta.ws.rs.*;
import jakarta.ws.rs.core.Context;
import jakarta.ws.rs.core.Cookie;
import jakarta.ws.rs.core.HttpHeaders;
import jakarta.ws.rs.core.MediaType;
import jakarta.ws.rs.core.Response;
import org.eclipse.microprofile.config.inject.ConfigProperty;

import java.net.URI;
import java.util.Arrays;
import java.util.List;
import java.util.logging.Logger;
import java.util.stream.Collectors;

/**
 * Admin REST endpoints for the blog.
 * These endpoints require authentication.
 */
@Path("/admin/blog")
@RolesAllowed({"admin", "user"})
public class AdminBlogResource {

    private static final Logger LOGGER = Logger.getLogger(AdminBlogResource.class.getName());
    private static final String USER_COOKIE_NAME = "filesurf_userId";

    @Inject
    BlogService blogService;

    @Inject
    UserService userService;

    @Inject
    Template adminBlogPosts;

    @Inject
    Template adminBlogPostEdit;

    @Inject
    Template adminBlogCategories;

    @Inject
    Template adminBlogTags;

    @Inject
    Template adminBlogAnalytics;

    @ConfigProperty(name = "blog.site.url", defaultValue = "https://filesurf.example.com")
    String siteUrl;

    @ConfigProperty(name = "blog.posts.per-page", defaultValue = "10")
    int postsPerPage;

    // ==================== POSTS ====================

    /**
     * List all posts (admin view)
     */
    @GET
    @Path("/posts")
    @Produces(MediaType.TEXT_HTML)
    public TemplateInstance listPosts(
            @QueryParam("status") String status,
            @QueryParam("page") @DefaultValue("0") int page,
            @Context HttpHeaders headers) {
        LOGGER.info("Admin: List posts, status: " + status + ", page: " + page);
        
        try {
            List<BlogPost> allPosts = blogService.getPublishedPosts(postsPerPage * 2, page * postsPerPage);
            int total = blogService.countPublishedPosts();
            List<BlogAuthor> authors = blogService.listAuthors();
            List<BlogCategory> categories = blogService.listCategories();
            
            return adminBlogPosts.data("posts", allPosts)
                    .data("status", status)
                    .data("page", page)
                    .data("perPage", postsPerPage)
                    .data("total", total)
                    .data("authors", authors)
                    .data("categories", categories)
                    .data("siteUrl", siteUrl);
                    
        } catch (Exception e) {
            LOGGER.severe("Error listing posts: " + e.getMessage());
            return adminBlogPosts.data("error", e.getMessage());
        }
    }

    /**
     * Create new post form
     */
    @GET
    @Path("/posts/new")
    @Produces(MediaType.TEXT_HTML)
    public TemplateInstance newPostForm() {
        LOGGER.info("Admin: New post form requested");
        
        try {
            List<BlogAuthor> authors = blogService.listAuthors();
            List<BlogCategory> categories = blogService.listCategories();
            List<BlogTag> tags = blogService.listTags();
            
            return adminBlogPostEdit.data("post", null)
                    .data("isNew", true)
                    .data("authors", authors)
                    .data("categories", categories)
                    .data("allTags", tags)
                    .data("selectedTagIds", List.of())
                    .data("siteUrl", siteUrl);
                    
        } catch (Exception e) {
            LOGGER.severe("Error loading new post form: " + e.getMessage());
            return adminBlogPostEdit.data("error", e.getMessage());
        }
    }

    /**
     * Edit post form
     */
    @GET
    @Path("/posts/{id}")
    @Produces(MediaType.TEXT_HTML)
    public TemplateInstance editPostForm(@PathParam("id") int id) {
        LOGGER.info("Admin: Edit post form requested, id: " + id);
        
        try {
            BlogPost post = blogService.getPost(id);
            
            if (post == null) {
                return adminBlogPostEdit.data("error", "Post not found: " + id);
            }
            
            List<BlogAuthor> authors = blogService.listAuthors();
            List<BlogCategory> categories = blogService.listCategories();
            List<BlogTag> allTags = blogService.listTags();
            List<BlogTag> postTags = blogService.getTagsForPost(id);
            List<Integer> selectedTagIds = postTags.stream()
                    .map(BlogTag::getId)
                    .collect(Collectors.toList());
            
            return adminBlogPostEdit.data("post", post)
                    .data("isNew", false)
                    .data("authors", authors)
                    .data("categories", categories)
                    .data("allTags", allTags)
                    .data("selectedTagIds", selectedTagIds)
                    .data("siteUrl", siteUrl);
                    
        } catch (Exception e) {
            LOGGER.severe("Error loading edit form: " + e.getMessage());
            return adminBlogPostEdit.data("error", e.getMessage());
        }
    }

    /**
     * Create new post
     */
    @POST
    @Path("/posts")
    @Produces(MediaType.APPLICATION_JSON)
    @Consumes(MediaType.APPLICATION_FORM_URLENCODED)
    public Response createPost(
            @FormParam("title") String title,
            @FormParam("content") String content,
            @FormParam("authorId") int authorId,
            @FormParam("categoryId") Integer categoryId,
            @FormParam("excerpt") String excerpt,
            @FormParam("tagIds") String tagIdsStr,
            @FormParam("featuredImageUrl") String featuredImageUrl,
            @FormParam("metaTitle") String metaTitle,
            @FormParam("metaDescription") String metaDescription,
            @FormParam("metaKeywords") String metaKeywords,
            @FormParam("status") @DefaultValue("draft") String status,
            @FormParam("action") String action) {
        
        LOGGER.info("Admin: Create post: " + title);
        
        // Validate required fields
        if (title == null || title.trim().isEmpty()) {
            return Response.status(Response.Status.BAD_REQUEST)
                    .entity("{\"error\": \"Title is required\"}")
                    .build();
        }
        if (content == null || content.trim().isEmpty()) {
            return Response.status(Response.Status.BAD_REQUEST)
                    .entity("{\"error\": \"Content is required\"}")
                    .build();
        }
        
        try {
            // Parse tag IDs
            List<Integer> tagIds = parseTagIds(tagIdsStr);
            
            // Create the post
            BlogPost post = blogService.createPost(
                    title, content, authorId, categoryId, excerpt, tagIds, featuredImageUrl);
            
            // Set meta fields
            if (metaTitle != null && !metaTitle.isBlank()) {
                post.setMetaTitle(metaTitle);
            }
            if (metaDescription != null && !metaDescription.isBlank()) {
                post.setMetaDescription(metaDescription);
            }
            if (metaKeywords != null && !metaKeywords.isBlank()) {
                post.setMetaKeywords(metaKeywords);
            }
            
            // Handle action (publish or save)
            if ("publish".equals(action)) {
                post = blogService.publishPost(post.getId());
            }
            
            blogService.updatePost(post);
            
            LOGGER.info("Admin: Post created successfully, id: " + post.getId());
            
            return Response.seeOther(URI.create("/admin/blog/posts/" + post.getId()))
                    .entity("{\"success\": true, \"postId\": " + post.getId() + "}")
                    .build();
                    
        } catch (Exception e) {
            LOGGER.severe("Error creating post: " + e.getMessage());
            return Response.status(Response.Status.INTERNAL_SERVER_ERROR)
                    .entity("{\"error\": \"" + escapeJson(e.getMessage()) + "\"}")
                    .build();
        }
    }

    /**
     * Update existing post
     */
    @PUT
    @Path("/posts/{id}")
    @Produces(MediaType.APPLICATION_JSON)
    @Consumes(MediaType.APPLICATION_FORM_URLENCODED)
    public Response updatePost(
            @PathParam("id") int id,
            @FormParam("title") String title,
            @FormParam("content") String content,
            @FormParam("authorId") int authorId,
            @FormParam("categoryId") Integer categoryId,
            @FormParam("excerpt") String excerpt,
            @FormParam("tagIds") String tagIdsStr,
            @FormParam("featuredImageUrl") String featuredImageUrl,
            @FormParam("metaTitle") String metaTitle,
            @FormParam("metaDescription") String metaDescription,
            @FormParam("metaKeywords") String metaKeywords,
            @FormParam("status") String status,
            @FormParam("action") String action) {
        
        LOGGER.info("Admin: Update post: " + id);
        
        try {
            BlogPost post = blogService.getPost(id);
            
            if (post == null) {
                return Response.status(Response.Status.NOT_FOUND)
                        .entity("{\"error\": \"Post not found\"}")
                        .build();
            }
            
            // Update fields
            post.setTitle(title);
            post.setSlug(blogService.generateSlug(title));
            post.setContent(content);
            post.setAuthorId(authorId);
            post.setCategoryId(categoryId);
            post.setExcerpt(excerpt);
            post.setFeaturedImageUrl(featuredImageUrl);
            post.setMetaTitle(metaTitle);
            post.setMetaDescription(metaDescription);
            post.setMetaKeywords(metaKeywords);
            post.setStatus(status);
            
            // Handle tags
            List<Integer> tagIds = parseTagIds(tagIdsStr);
            blogService.setPostTags(id, tagIds);
            
            // Handle action
            if ("publish".equals(action) && !"published".equals(post.getStatus())) {
                post = blogService.publishPost(id);
            } else if ("unpublish".equals(action) && !"draft".equals(post.getStatus())) {
                post = blogService.unpublishPost(id);
            }
            
            blogService.updatePost(post);
            
            LOGGER.info("Admin: Post updated successfully: " + id);
            
            return Response.seeOther(URI.create("/admin/blog/posts/" + id))
                    .entity("{\"success\": true}")
                    .build();
                    
        } catch (Exception e) {
            LOGGER.severe("Error updating post: " + e.getMessage());
            return Response.status(Response.Status.INTERNAL_SERVER_ERROR)
                    .entity("{\"error\": \"" + escapeJson(e.getMessage()) + "\"}")
                    .build();
        }
    }

    /**
     * Delete post
     */
    @DELETE
    @Path("/posts/{id}")
    @Produces(MediaType.APPLICATION_JSON)
    public Response deletePost(@PathParam("id") int id) {
        LOGGER.info("Admin: Delete post: " + id);
        
        try {
            blogService.deletePost(id);
            
            LOGGER.info("Admin: Post deleted successfully: " + id);
            
            return Response.ok()
                    .entity("{\"success\": true, \"message\": \"Post deleted\"}")
                    .build();
                    
        } catch (Exception e) {
            LOGGER.severe("Error deleting post: " + e.getMessage());
            return Response.status(Response.Status.INTERNAL_SERVER_ERROR)
                    .entity("{\"error\": \"" + escapeJson(e.getMessage()) + "\"}")
                    .build();
        }
    }

    /**
     * Publish post
     */
    @POST
    @Path("/posts/{id}/publish")
    @Produces(MediaType.APPLICATION_JSON)
    public Response publishPost(@PathParam("id") int id) {
        LOGGER.info("Admin: Publish post: " + id);
        
        try {
            blogService.publishPost(id);
            
            return Response.ok()
                    .entity("{\"success\": true, \"message\": \"Post published\"}")
                    .build();
                    
        } catch (Exception e) {
            LOGGER.severe("Error publishing post: " + e.getMessage());
            return Response.status(Response.Status.INTERNAL_SERVER_ERROR)
                    .entity("{\"error\": \"" + escapeJson(e.getMessage()) + "\"}")
                    .build();
        }
    }

    /**
     * Unpublish post
     */
    @POST
    @Path("/posts/{id}/unpublish")
    @Produces(MediaType.APPLICATION_JSON)
    public Response unpublishPost(@PathParam("id") int id) {
        LOGGER.info("Admin: Unpublish post: " + id);
        
        try {
            blogService.unpublishPost(id);
            
            return Response.ok()
                    .entity("{\"success\": true, \"message\": \"Post unpublished\"}")
                    .build();
                    
        } catch (Exception e) {
            LOGGER.severe("Error unpublishing post: " + e.getMessage());
            return Response.status(Response.Status.INTERNAL_SERVER_ERROR)
                    .entity("{\"error\": \"" + escapeJson(e.getMessage()) + "\"}")
                    .build();
        }
    }

    // ==================== CATEGORIES ====================

    /**
     * List categories
     */
    @GET
    @Path("/categories")
    @Produces(MediaType.TEXT_HTML)
    public TemplateInstance listCategories() {
        LOGGER.info("Admin: List categories");
        
        try {
            List<BlogCategory> categories = blogService.listCategories();
            int postCount = blogService.countPublishedPosts();
            
            return adminBlogCategories.data("categories", categories)
                    .data("totalPosts", postCount)
                    .data("siteUrl", siteUrl);
                    
        } catch (Exception e) {
            LOGGER.severe("Error listing categories: " + e.getMessage());
            return adminBlogCategories.data("error", e.getMessage());
        }
    }

    /**
     * Create category
     */
    @POST
    @Path("/categories")
    @Produces(MediaType.APPLICATION_JSON)
    @Consumes(MediaType.APPLICATION_FORM_URLENCODED)
    public Response createCategory(
            @FormParam("name") String name,
            @FormParam("description") String description) {
        
        LOGGER.info("Admin: Create category: " + name);
        
        if (name == null || name.trim().isEmpty()) {
            return Response.status(Response.Status.BAD_REQUEST)
                    .entity("{\"error\": \"Category name is required\"}")
                    .build();
        }
        
        try {
            blogService.createCategory(name, description);
            
            return Response.seeOther(URI.create("/admin/blog/categories"))
                    .entity("{\"success\": true}")
                    .build();
                    
        } catch (Exception e) {
            LOGGER.severe("Error creating category: " + e.getMessage());
            return Response.status(Response.Status.INTERNAL_SERVER_ERROR)
                    .entity("{\"error\": \"" + escapeJson(e.getMessage()) + "\"}")
                    .build();
        }
    }

    /**
     * Delete category
     */
    @DELETE
    @Path("/categories/{id}")
    @Produces(MediaType.APPLICATION_JSON)
    public Response deleteCategory(@PathParam("id") int id) {
        LOGGER.info("Admin: Delete category: " + id);
        
        try {
            blogService.deleteCategory(id);
            
            return Response.ok()
                    .entity("{\"success\": true}")
                    .build();
                    
        } catch (Exception e) {
            LOGGER.severe("Error deleting category: " + e.getMessage());
            return Response.status(Response.Status.INTERNAL_SERVER_ERROR)
                    .entity("{\"error\": \"" + escapeJson(e.getMessage()) + "\"}")
                    .build();
        }
    }

    // ==================== TAGS ====================

    /**
     * List tags
     */
    @GET
    @Path("/tags")
    @Produces(MediaType.TEXT_HTML)
    public TemplateInstance listTags() {
        LOGGER.info("Admin: List tags");
        
        try {
            List<BlogTag> tags = blogService.listTags();
            int postCount = blogService.countPublishedPosts();
            
            return adminBlogTags.data("tags", tags)
                    .data("totalPosts", postCount)
                    .data("siteUrl", siteUrl);
                    
        } catch (Exception e) {
            LOGGER.severe("Error listing tags: " + e.getMessage());
            return adminBlogTags.data("error", e.getMessage());
        }
    }

    /**
     * Create tag
     */
    @POST
    @Path("/tags")
    @Produces(MediaType.APPLICATION_JSON)
    @Consumes(MediaType.APPLICATION_FORM_URLENCODED)
    public Response createTag(@FormParam("name") String name) {
        LOGGER.info("Admin: Create tag: " + name);
        
        if (name == null || name.trim().isEmpty()) {
            return Response.status(Response.Status.BAD_REQUEST)
                    .entity("{\"error\": \"Tag name is required\"}")
                    .build();
        }
        
        try {
            blogService.createTag(name);
            
            return Response.seeOther(URI.create("/admin/blog/tags"))
                    .entity("{\"success\": true}")
                    .build();
                    
        } catch (Exception e) {
            LOGGER.severe("Error creating tag: " + e.getMessage());
            return Response.status(Response.Status.INTERNAL_SERVER_ERROR)
                    .entity("{\"error\": \"" + escapeJson(e.getMessage()) + "\"}")
                    .build();
        }
    }

    /**
     * Delete tag
     */
    @DELETE
    @Path("/tags/{id}")
    @Produces(MediaType.APPLICATION_JSON)
    public Response deleteTag(@PathParam("id") int id) {
        LOGGER.info("Admin: Delete tag: " + id);
        
        try {
            blogService.deleteTag(id);
            
            return Response.ok()
                    .entity("{\"success\": true}")
                    .build();
                    
        } catch (Exception e) {
            LOGGER.severe("Error deleting tag: " + e.getMessage());
            return Response.status(Response.Status.INTERNAL_SERVER_ERROR)
                    .entity("{\"error\": \"" + escapeJson(e.getMessage()) + "\"}")
                    .build();
        }
    }

    // ==================== AUTHORS ====================

    /**
     * List authors
     */
    @GET
    @Path("/authors")
    @Produces(MediaType.APPLICATION_JSON)
    public Response listAuthors() {
        try {
            List<BlogAuthor> authors = blogService.listAuthors();
            return Response.ok(authors).build();
        } catch (Exception e) {
            return Response.status(Response.Status.INTERNAL_SERVER_ERROR)
                    .entity("{\"error\": \"" + escapeJson(e.getMessage()) + "\"}")
                    .build();
        }
    }

    /**
     * Create author
     */
    @POST
    @Path("/authors")
    @Produces(MediaType.APPLICATION_JSON)
    @Consumes(MediaType.APPLICATION_FORM_URLENCODED)
    public Response createAuthor(
            @FormParam("name") String name,
            @FormParam("email") String email,
            @FormParam("bio") String bio,
            @FormParam("avatarUrl") String avatarUrl) {
        
        LOGGER.info("Admin: Create author: " + name);
        
        if (name == null || name.trim().isEmpty()) {
            return Response.status(Response.Status.BAD_REQUEST)
                    .entity("{\"error\": \"Author name is required\"}")
                    .build();
        }
        
        try {
            blogService.createAuthor(name, email, bio, avatarUrl);
            
            return Response.ok()
                    .entity("{\"success\": true}")
                    .build();
                    
        } catch (Exception e) {
            LOGGER.severe("Error creating author: " + e.getMessage());
            return Response.status(Response.Status.INTERNAL_SERVER_ERROR)
                    .entity("{\"error\": \"" + escapeJson(e.getMessage()) + "\"}")
                    .build();
        }
    }

    // ==================== ANALYTICS ====================

    /**
     * View analytics
     */
    @GET
    @Path("/analytics")
    @Produces(MediaType.TEXT_HTML)
    public TemplateInstance viewAnalytics() {
        LOGGER.info("Admin: View analytics");
        
        try {
            List<BlogPost> popularPosts = blogService.getPopularPosts(10);
            int totalPosts = blogService.countPublishedPosts();
            List<BlogAuthor> authors = blogService.listAuthors();
            List<BlogCategory> categories = blogService.listCategories();
            
            return adminBlogAnalytics.data("popularPosts", popularPosts)
                    .data("totalPosts", totalPosts)
                    .data("authors", authors)
                    .data("categories", categories)
                    .data("siteUrl", siteUrl);
                    
        } catch (Exception e) {
            LOGGER.severe("Error loading analytics: " + e.getMessage());
            return adminBlogAnalytics.data("error", e.getMessage());
        }
    }

    // ==================== HELPERS ====================

    private List<Integer> parseTagIds(String tagIdsStr) {
        if (tagIdsStr == null || tagIdsStr.trim().isEmpty()) {
            return List.of();
        }
        return Arrays.stream(tagIdsStr.split(","))
                .map(String::trim)
                .filter(s -> !s.isEmpty())
                .map(Integer::parseInt)
                .collect(Collectors.toList());
    }

    private String escapeJson(String str) {
        if (str == null) return "";
        return str.replace("\\", "\\\\")
                  .replace("\"", "\\\"")
                  .replace("\n", "\\n")
                  .replace("\r", "\\r")
                  .replace("\t", "\\t");
    }
}
