package com.filesurf;

import com.filesurf.model.*;
import com.filesurf.service.BlogService;
import com.filesurf.service.RssFeedService;
import com.filesurf.service.SitemapService;
import com.filesurf.util.CssVersionProvider;
import io.quarkus.qute.Location;
import io.quarkus.qute.Template;
import io.quarkus.qute.TemplateInstance;
import jakarta.inject.Inject;
import jakarta.ws.rs.*;
import jakarta.ws.rs.core.Context;
import jakarta.ws.rs.core.HttpHeaders;
import jakarta.ws.rs.core.MediaType;
import jakarta.ws.rs.core.Response;
import org.eclipse.microprofile.config.inject.ConfigProperty;

import java.net.URI;
import java.util.List;
import java.util.logging.Logger;

/**
 * Public REST endpoints for the blog.
 * These endpoints do not require authentication.
 */
@Path("/blog")
public class BlogResource {

    private static final Logger LOGGER = Logger.getLogger(BlogResource.class.getName());

    @Inject
    BlogService blogService;

    @Inject
    CssVersionProvider cssVersionProvider;

    @Inject
    @Location("blog")
    Template blog;

    @Inject
    @Location("blog-post")
    Template blogPost;

    @Inject
    @Location("blog-category")
    Template blogCategory;

    @Inject
    @Location("blog-tag")
    Template blogTag;

    @Inject
    @Location("blog-author")
    Template blogAuthor;

    @Inject
    @Location("blog-search")
    Template blogSearch;

    @Inject
    SitemapService sitemapService;

    @Inject
    RssFeedService rssFeedService;

    @ConfigProperty(name = "blog.site.url", defaultValue = "https://filesurf.example.com")
    String siteUrl;

    @ConfigProperty(name = "blog.site.name", defaultValue = "FileSurf Blog")
    String siteName;

    /**
     * Blog homepage with paginated posts
     */
    @GET
    @Produces(MediaType.TEXT_HTML)
    public TemplateInstance getBlogHome(
            @QueryParam("page") @DefaultValue("0") int page,
            @Context HttpHeaders headers) {
        LOGGER.info("Blog home page requested, page: " + page);
        
        return blog.data("siteName", siteName)
                .data("pageTitle", siteName)
                .data("cssPath", cssVersionProvider.getCssPath());
    }

    /**
     * JSON API: Get blog data for homepage
     */
    @GET
    @Path("/api/home")
    @Produces(MediaType.APPLICATION_JSON)
    public Response getBlogHomeData(
            @QueryParam("page") @DefaultValue("0") int page) {
        LOGGER.info("Blog home data API requested, page: " + page);
        
        try {
            BlogService.PaginatedResult<BlogPost> result = blogService.getPublishedPosts(page);
            List<BlogCategory> categories = blogService.listCategories();
            List<BlogTag> tags = blogService.listTags();
            List<BlogPost> popularPosts = blogService.getPopularPosts(5);
            
            var data = new java.util.HashMap<String, Object>();
            data.put("posts", result.getItems());
            data.put("page", page);
            data.put("perPage", result.getPerPage());
            data.put("total", result.getTotal());
            data.put("hasMore", result.hasMore());
            data.put("hasPrevious", result.hasPrevious());
            data.put("previousPage", result.getPreviousPage());
            data.put("nextPage", result.getNextPage());
            data.put("categories", categories);
            data.put("tags", tags);
            data.put("popularPosts", popularPosts);
            
            return Response.ok(data).build();
        } catch (Exception e) {
            LOGGER.severe("Error loading blog data: " + e.getMessage());
            return Response.status(Response.Status.INTERNAL_SERVER_ERROR)
                    .entity(java.util.Map.of("error", e.getMessage()))
                    .build();
        }
    }

    /**
     * Individual blog post view
     */
    @GET
    @Path("/{slug}")
    @Produces(MediaType.TEXT_HTML)
    public Response getPost(
            @PathParam("slug") String slug,
            @Context HttpHeaders headers) {
        LOGGER.info("Blog post requested: " + slug);
        
        try {
            BlogPost post = blogService.getPost(slug);
            
            if (post == null || !post.isPublished()) {
                return Response.status(Response.Status.NOT_FOUND)
                        .entity("<html><body><h1>404 - Post Not Found</h1><p>The requested post does not exist.</p></body></html>")
                        .build();
            }
            
            // Track view
            String clientIp = getClientIp(headers);
            blogService.trackView(post.getId(), clientIp);
            
            // Get related posts
            List<BlogPost> relatedPosts = List.of();
            if (post.getCategory() != null) {
                relatedPosts = blogService.getPostsByCategory(post.getCategory().getSlug(), 0).getItems()
                        .stream()
                        .filter(p -> !p.getId().equals(post.getId()))
                        .limit(3)
                        .toList();
            }
            
            // Generate SEO data
            String metaTitle = blogService.getMetaTitle(post);
            String metaDescription = blogService.getMetaDescription(post);
            String keywords = blogService.getKeywords(post);
            String canonicalUrl = blogService.getCanonicalUrl(post);
            String structuredData = blogService.generateStructuredData(post);
            
            return Response.ok(blogPost.data("post", post)
                    .data("metaTitle", metaTitle)
                    .data("metaDescription", metaDescription)
                    .data("keywords", keywords)
                    .data("canonicalUrl", canonicalUrl)
                    .data("structuredData", structuredData)
                    .data("relatedPosts", relatedPosts)
                    .data("siteName", siteName)
                    .data("siteUrl", siteUrl)
                    .data("pageTitle", post.getTitle())
                    .data("cssPath", cssVersionProvider.getCssPath())).build();
                    
        } catch (Exception e) {
            LOGGER.severe("Error loading post: " + e.getMessage());
            return Response.status(Response.Status.INTERNAL_SERVER_ERROR)
                    .entity("<html><body><h1>500 - Internal Error</h1><p>" + e.getMessage() + "</p></body></html>")
                    .build();
        }
    }

    /**
     * Posts by category
     */
    @GET
    @Path("/category/{slug}")
    @Produces(MediaType.TEXT_HTML)
    public TemplateInstance getPostsByCategory(
            @PathParam("slug") String slug,
            @QueryParam("page") @DefaultValue("0") int page) {
        LOGGER.info("Category page requested: " + slug + ", page: " + page);
        
        try {
            BlogCategory category = blogService.getCategoryBySlug(slug);
            
            if (category == null) {
                return blog.data("error", "Category not found: " + slug)
                        .data("siteName", siteName)
                        .data("pageTitle", "Category Not Found")
                        .data("cssPath", cssVersionProvider.getCssPath());
            }
            
            BlogService.PaginatedResult<BlogPost> result = blogService.getPostsByCategory(slug, page);
            List<BlogCategory> categories = blogService.listCategories();
            List<BlogTag> tags = blogService.listTags();
            
            return blogCategory.data("category", category)
                    .data("posts", result.getItems())
                    .data("page", page)
                    .data("perPage", result.getPerPage())
                    .data("total", result.getTotal())
                    .data("hasMore", result.hasMore())
                    .data("categories", categories)
                    .data("tags", tags)
                    .data("siteName", siteName)
                    .data("pageTitle", category.getName() + " | " + siteName)
                    .data("cssPath", cssVersionProvider.getCssPath());
                    
        } catch (Exception e) {
            LOGGER.severe("Error loading category: " + e.getMessage());
            return blog.data("error", e.getMessage())
                    .data("siteName", siteName)
                    .data("pageTitle", "Error")
                    .data("cssPath", cssVersionProvider.getCssPath());
        }
    }

    /**
     * Posts by tag
     */
    @GET
    @Path("/tag/{slug}")
    @Produces(MediaType.TEXT_HTML)
    public TemplateInstance getPostsByTag(
            @PathParam("slug") String slug,
            @QueryParam("page") @DefaultValue("0") int page) {
        LOGGER.info("Tag page requested: " + slug + ", page: " + page);
        
        try {
            BlogTag tag = blogService.getTagBySlug(slug);
            
            if (tag == null) {
                return blog.data("error", "Tag not found: " + slug)
                        .data("siteName", siteName)
                        .data("pageTitle", "Tag Not Found")
                        .data("cssPath", cssVersionProvider.getCssPath());
            }
            
            BlogService.PaginatedResult<BlogPost> result = blogService.getPostsByTag(slug, page);
            List<BlogCategory> categories = blogService.listCategories();
            List<BlogTag> tags = blogService.listTags();
            
            return blogTag.data("tag", tag)
                    .data("posts", result.getItems())
                    .data("page", page)
                    .data("perPage", result.getPerPage())
                    .data("hasMore", result.hasMore())
                    .data("categories", categories)
                    .data("tags", tags)
                    .data("siteName", siteName)
                    .data("pageTitle", "#" + tag.getName() + " | " + siteName)
                    .data("cssPath", cssVersionProvider.getCssPath());
                    
        } catch (Exception e) {
            LOGGER.severe("Error loading tag: " + e.getMessage());
            return blog.data("error", e.getMessage())
                    .data("siteName", siteName)
                    .data("pageTitle", "Error")
                    .data("cssPath", cssVersionProvider.getCssPath());
        }
    }

    /**
     * Posts by author
     */
    @GET
    @Path("/author/{id}")
    @Produces(MediaType.TEXT_HTML)
    public TemplateInstance getPostsByAuthor(
            @PathParam("id") int authorId,
            @QueryParam("page") @DefaultValue("0") int page) {
        LOGGER.info("Author page requested: " + authorId + ", page: " + page);
        
        try {
            BlogAuthor author = blogService.getAuthor(authorId);
            
            if (author == null) {
                return blog.data("error", "Author not found: " + authorId)
                        .data("siteName", siteName)
                        .data("pageTitle", "Author Not Found")
                        .data("cssPath", cssVersionProvider.getCssPath());
            }
            
            BlogService.PaginatedResult<BlogPost> result = blogService.getPostsByAuthor(authorId, page);
            List<BlogCategory> categories = blogService.listCategories();
            List<BlogTag> tags = blogService.listTags();
            
            return blogAuthor.data("author", author)
                    .data("posts", result.getItems())
                    .data("page", page)
                    .data("perPage", result.getPerPage())
                    .data("hasMore", result.hasMore())
                    .data("categories", categories)
                    .data("tags", tags)
                    .data("siteName", siteName)
                    .data("pageTitle", author.getName() + " | " + siteName)
                    .data("cssPath", cssVersionProvider.getCssPath());
                    
        } catch (Exception e) {
            LOGGER.severe("Error loading author: " + e.getMessage());
            return blog.data("error", e.getMessage())
                    .data("siteName", siteName)
                    .data("pageTitle", "Error")
                    .data("cssPath", cssVersionProvider.getCssPath());
        }
    }

    /**
     * Search posts
     */
    @GET
    @Path("/search")
    @Produces(MediaType.TEXT_HTML)
    public TemplateInstance searchPosts(
            @QueryParam("q") String query,
            @QueryParam("page") @DefaultValue("0") int page) {
        LOGGER.info("Search requested: '" + query + "', page: " + page);
        
        if (query == null || query.trim().isEmpty()) {
            return blogSearch.data("query", "")
                    .data("posts", List.of())
                    .data("categories", List.of())
                    .data("tags", List.of())
                    .data("siteName", siteName)
                    .data("pageTitle", "Search | " + siteName)
                    .data("cssPath", cssVersionProvider.getCssPath());
        }
        
        try {
            BlogService.PaginatedResult<BlogPost> result = blogService.searchPosts(query.trim(), page);
            List<BlogCategory> categories = blogService.listCategories();
            List<BlogTag> tags = blogService.listTags();
            
            return blogSearch.data("query", query)
                    .data("posts", result.getItems())
                    .data("page", page)
                    .data("perPage", result.getPerPage())
                    .data("total", result.getTotal())
                    .data("hasMore", result.hasMore())
                    .data("categories", categories)
                    .data("tags", tags)
                    .data("siteName", siteName)
                    .data("pageTitle", "Search: " + query + " | " + siteName)
                    .data("cssPath", cssVersionProvider.getCssPath());
                    
        } catch (Exception e) {
            LOGGER.severe("Error searching: " + e.getMessage());
            return blogSearch.data("query", query)
                    .data("error", e.getMessage())
                    .data("siteName", siteName)
                    .data("pageTitle", "Search Error | " + siteName)
                    .data("cssPath", cssVersionProvider.getCssPath());
        }
    }

    /**
     * XML sitemap for SEO
     */
    @GET
    @Path("/sitemap.xml")
    @Produces(MediaType.APPLICATION_XML)
    public String getSitemap() {
        LOGGER.info("Sitemap requested");
        return sitemapService.generateSitemap();
    }

    /**
     * RSS feed
     */
    @GET
    @Path("/rss.xml")
    @Produces(MediaType.APPLICATION_XML)
    public String getRssFeed() {
        LOGGER.info("RSS feed requested");
        return rssFeedService.generateRssFeed();
    }

    /**
     * Atom feed
     */
    @GET
    @Path("/atom.xml")
    @Produces(MediaType.APPLICATION_XML)
    public String getAtomFeed() {
        LOGGER.info("Atom feed requested");
        return rssFeedService.generateAtomFeed();
    }

    /**
     * Robots.txt
     */
    @GET
    @Path("/robots.txt")
    @Produces(MediaType.TEXT_PLAIN)
    public String getRobotsTxt() {
        LOGGER.info("Robots.txt requested");
        return sitemapService.generateRobotsTxt();
    }

    /**
     * Get client IP address from request headers
     */
    private String getClientIp(HttpHeaders headers) {
        // Check X-Forwarded-For header first (for proxied requests)
        List<String> forwardedFor = headers.getRequestHeader("X-Forwarded-For");
        if (forwardedFor != null && !forwardedFor.isEmpty()) {
            // X-Forwarded-For can contain multiple IPs, first one is the original client
            String[] ips = forwardedFor.get(0).split(",");
            return ips[0].trim();
        }
        
        // Check X-Real-IP header
        List<String> realIp = headers.getRequestHeader("X-Real-IP");
        if (realIp != null && !realIp.isEmpty()) {
            return realIp.get(0);
        }
        
        // Fall back to unknown
        return "unknown";
    }
}
