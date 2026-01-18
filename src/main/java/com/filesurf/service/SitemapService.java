package com.filesurf.service;

import com.filesurf.db.BlogDatabaseManager;
import com.filesurf.model.BlogCategory;
import com.filesurf.model.BlogPost;
import com.filesurf.model.BlogTag;
import jakarta.enterprise.context.ApplicationScoped;
import jakarta.inject.Inject;
import org.eclipse.microprofile.config.inject.ConfigProperty;

import java.time.format.DateTimeFormatter;
import java.util.List;
import java.util.logging.Logger;

/**
 * Service for generating XML sitemaps for SEO.
 * Generates sitemaps conforming to sitemaps.org protocol.
 */
@ApplicationScoped
public class SitemapService {

    private static final Logger LOGGER = Logger.getLogger(SitemapService.class.getName());

    @Inject
    BlogDatabaseManager db;

    @ConfigProperty(name = "blog.site.url", defaultValue = "https://filesurf.example.com")
    String siteUrl;

    @ConfigProperty(name = "blog.sitemap.max-urls", defaultValue = "1000")
    int maxUrls;

    private static final DateTimeFormatter ISO_FORMATTER = DateTimeFormatter.ISO_INSTANT;
    private static final DateTimeFormatter SITEMAP_DATE_FORMAT = DateTimeFormatter.ofPattern("yyyy-MM-dd");

    /**
     * Generate sitemap index XML (for multiple sitemaps)
     */
    public String generateSitemapIndex(List<String> sitemapUrls) {
        StringBuilder sb = new StringBuilder();
        sb.append("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
        sb.append("<sitemapindex xmlns=\"http://www.sitemaps.org/schemas/sitemap/0.9\">\n");
        
        for (String url : sitemapUrls) {
            sb.append("  <sitemap>\n");
            sb.append("    <loc>").append(escapeXml(url)).append("</loc>\n");
            sb.append("    <lastmod>").append(SITEMAP_DATE_FORMAT.format(java.time.LocalDate.now())).append("</lastmod>\n");
            sb.append("  </sitemap>\n");
        }
        
        sb.append("</sitemapindex>");
        return sb.toString();
    }

    /**
     * Generate main sitemap with all content URLs
     */
    public String generateSitemap() {
        StringBuilder sb = new StringBuilder();
        sb.append("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
        sb.append("<urlset xmlns=\"http://www.sitemaps.org/schemas/sitemap/0.9\">\n");
        
        int urlCount = 0;
        
        // Add blog home page
        urlCount += addUrl(sb, siteUrl + "/blog", "1.0", "daily", null);
        
        // Add RSS and Atom feed URLs
        urlCount += addUrl(sb, siteUrl + "/rss.xml", "0.9", "weekly", null);
        urlCount += addUrl(sb, siteUrl + "/atom.xml", "0.9", "weekly", null);
        
        try {
            // Add published posts
            List<BlogPost> posts = db.getPublishedPosts(maxUrls, 0);
            for (BlogPost post : posts) {
                if (urlCount >= maxUrls) break;
                String loc = siteUrl + "/blog/" + post.getSlug();
                String lastmod = post.getUpdatedAt() != null ? 
                    post.getUpdatedAt().format(SITEMAP_DATE_FORMAT) : null;
                String changefreq = "weekly";
                String priority = "0.8";
                
                urlCount += addUrl(sb, loc, priority, changefreq, lastmod);
            }
            
            // Add categories
            List<BlogCategory> categories = db.listCategories();
            for (BlogCategory category : categories) {
                if (urlCount >= maxUrls) break;
                String loc = siteUrl + "/blog/category/" + category.getSlug();
                urlCount += addUrl(sb, loc, "0.6", "daily", null);
            }
            
            // Add tags
            List<BlogTag> tags = db.listTags();
            for (BlogTag tag : tags) {
                if (urlCount >= maxUrls) break;
                String loc = siteUrl + "/blog/tag/" + tag.getSlug();
                urlCount += addUrl(sb, loc, "0.5", "weekly", null);
            }
            
        } catch (Exception e) {
            LOGGER.severe("Error generating sitemap: " + e.getMessage());
        }
        
        sb.append("</urlset>");
        
        LOGGER.info("Generated sitemap with " + urlCount + " URLs");
        return sb.toString();
    }

    /**
     * Generate sitemap for a specific category
     */
    public String generateCategorySitemap(BlogCategory category) {
        StringBuilder sb = new StringBuilder();
        sb.append("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
        sb.append("<urlset xmlns=\"http://www.sitemaps.org/schemas/sitemap/0.9\">\n");
        
        // Category page
        addUrl(sb, siteUrl + "/blog/category/" + category.getSlug(), "0.7", "daily", null);
        
        try {
            List<BlogPost> posts = db.getPostsByCategory(category.getId(), maxUrls, 0);
            for (BlogPost post : posts) {
                String loc = siteUrl + "/blog/" + post.getSlug();
                String lastmod = post.getUpdatedAt() != null ? 
                    post.getUpdatedAt().format(SITEMAP_DATE_FORMAT) : null;
                addUrl(sb, loc, "0.6", "weekly", lastmod);
            }
        } catch (Exception e) {
            LOGGER.severe("Error generating category sitemap: " + e.getMessage());
        }
        
        sb.append("</urlset>");
        return sb.toString();
    }

    /**
     * Generate sitemap for a specific tag
     */
    public String generateTagSitemap(BlogTag tag) {
        StringBuilder sb = new StringBuilder();
        sb.append("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
        sb.append("<urlset xmlns=\"http://www.sitemaps.org/schemas/sitemap/0.9\">\n");
        
        // Tag page
        addUrl(sb, siteUrl + "/blog/tag/" + tag.getSlug(), "0.5", "weekly", null);
        
        try {
            List<BlogPost> posts = db.getPostsByTag(tag.getId(), maxUrls, 0);
            for (BlogPost post : posts) {
                String loc = siteUrl + "/blog/" + post.getSlug();
                String lastmod = post.getUpdatedAt() != null ? 
                    post.getUpdatedAt().format(SITEMAP_DATE_FORMAT) : null;
                addUrl(sb, loc, "0.5", "weekly", lastmod);
            }
        } catch (Exception e) {
            LOGGER.severe("Error generating tag sitemap: " + e.getMessage());
        }
        
        sb.append("</urlset>");
        return sb.toString();
    }

    /**
     * Generate robots.txt content
     */
    public String generateRobotsTxt() {
        StringBuilder sb = new StringBuilder();
        sb.append("User-agent: *\n");
        sb.append("Allow: /blog\n");
        sb.append("Allow: /rss.xml\n");
        sb.append("Allow: /atom.xml\n");
        sb.append("Allow: /sitemap.xml\n");
        sb.append("\n");
        sb.append("Sitemap: ").append(siteUrl).append("/sitemap.xml\n");
        return sb.toString();
    }

    /**
     * Add a URL entry to the sitemap
     * @return 1 if URL was added, 0 otherwise
     */
    private int addUrl(StringBuilder sb, String loc, String priority, String changefreq, String lastmod) {
        sb.append("  <url>\n");
        sb.append("    <loc>").append(escapeXml(loc)).append("</loc>\n");
        
        if (lastmod != null) {
            sb.append("    <lastmod>").append(escapeXml(lastmod)).append("</lastmod>\n");
        }
        
        sb.append("    <changefreq>").append(escapeXml(changefreq)).append("</changefreq>\n");
        sb.append("    <priority>").append(escapeXml(priority)).append("</priority>\n");
        
        sb.append("  </url>\n");
        return 1;
    }

    /**
     * Escape special XML characters
     */
    private String escapeXml(String str) {
        if (str == null) return "";
        return str.replace("&", "&amp;")
                  .replace("<", "&lt;")
                  .replace(">", "&gt;")
                  .replace("\"", "&quot;")
                  .replace("'", "&apos;");
    }
}
