package com.filesurf.service;

import com.filesurf.db.BlogDatabaseManager;
import com.filesurf.model.BlogPost;
import jakarta.enterprise.context.ApplicationScoped;
import jakarta.inject.Inject;
import org.eclipse.microprofile.config.inject.ConfigProperty;

import java.time.format.DateTimeFormatter;
import java.util.List;
import java.util.logging.Logger;

/**
 * Service for generating RSS and Atom feeds for content syndication.
 */
@ApplicationScoped
public class RssFeedService {

    private static final Logger LOGGER = Logger.getLogger(RssFeedService.class.getName());

    @Inject
    BlogDatabaseManager db;

    @ConfigProperty(name = "blog.site.url", defaultValue = "https://filesurf.example.com")
    String siteUrl;

    @ConfigProperty(name = "blog.site.name", defaultValue = "FileSurf Blog")
    String siteName;

    @ConfigProperty(name = "blog.site.description", defaultValue = "AI-powered file management and automation insights")
    String siteDescription;

    @ConfigProperty(name = "blog.rss.items", defaultValue = "20")
    int maxItems;

    private static final DateTimeFormatter RFC822_DATE_FORMAT = 
        DateTimeFormatter.ofPattern("EEE, dd MMM yyyy HH:mm:ss Z");

    /**
     * Generate RSS 2.0 feed
     */
    public String generateRssFeed() {
        StringBuilder sb = new StringBuilder();
        sb.append("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
        sb.append("<rss version=\"2.0\" xmlns:atom=\"http://www.w3.org/2005/Atom\">\n");
        sb.append("  <channel>\n");
        sb.append("    <title>").append(escapeXml(siteName)).append("</title>\n");
        sb.append("    <link>").append(siteUrl).append("/blog</link>\n");
        sb.append("    <description>").append(escapeXml(siteDescription)).append("</description>\n");
        sb.append("    <language>en-us</language>\n");
        sb.append("    <lastBuildDate>").append(RFC822_DATE_FORMAT.format(java.time.ZonedDateTime.now())).append("</lastBuildDate>\n");
        sb.append("    <atom:link href=\"").append(siteUrl).append("/rss.xml\" rel=\"self\" type=\"application/rss+xml\"/>\n");
        
        try {
            List<BlogPost> posts = db.getPublishedPosts(maxItems, 0);
            for (BlogPost post : posts) {
                sb.append("    <item>\n");
                sb.append("      <title>").append(escapeXml(post.getTitle())).append("</title>\n");
                sb.append("      <link>").append(siteUrl).append("/blog/").append(post.getSlug()).append("</link>\n");
                sb.append("      <guid isPermaLink=\"true\">").append(siteUrl).append("/blog/").append(post.getSlug()).append("</guid>\n");
                
                if (post.getExcerpt() != null && !post.getExcerpt().isBlank()) {
                    sb.append("      <description><![CDATA[").append(post.getExcerpt()).append("]]></description>\n");
                }
                
                if (post.getAuthor() != null) {
                    sb.append("      <author>").append(escapeXml(post.getAuthor().getEmail() != null ? 
                        post.getAuthor().getEmail() : post.getAuthor().getName())).append("</author>\n");
                    sb.append("      <creator>").append(escapeXml(post.getAuthor().getName())).append("</creator>\n");
                }
                
                if (post.getCategory() != null) {
                    sb.append("      <category>").append(escapeXml(post.getCategory().getName())).append("</category>\n");
                }
                
                if (post.getPublishedAt() != null) {
                    sb.append("      <pubDate>").append(post.getPublishedAt().atZone(java.time.ZoneId.of("UTC")).format(RFC822_DATE_FORMAT)).append("</pubDate>\n");
                }
                
                sb.append("    </item>\n");
            }
        } catch (Exception e) {
            LOGGER.severe("Error generating RSS feed: " + e.getMessage());
        }
        
        sb.append("  </channel>\n");
        sb.append("</rss>");
        
        LOGGER.info("Generated RSS feed");
        return sb.toString();
    }

    /**
     * Generate Atom 1.0 feed
     */
    public String generateAtomFeed() {
        StringBuilder sb = new StringBuilder();
        sb.append("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
        sb.append("<feed xmlns=\"http://www.w3.org/2005/Atom\">\n");
        sb.append("  <title>").append(escapeXml(siteName)).append("</title>\n");
        sb.append("  <subtitle>").append(escapeXml(siteDescription)).append("</subtitle>\n");
        sb.append("  <link href=\"").append(siteUrl).append("/blog\" rel=\"alternate\"/>\n");
        sb.append("  <link href=\"").append(siteUrl).append("/atom.xml\" rel=\"self\"/>\n");
        sb.append("  <id>").append(siteUrl).append("/</id>\n");
        sb.append("  <updated>").append(java.time.ZonedDateTime.now().format(DateTimeFormatter.ISO_INSTANT)).append("</updated>\n");
        
        try {
            List<BlogPost> posts = db.getPublishedPosts(maxItems, 0);
            for (BlogPost post : posts) {
                String postUrl = siteUrl + "/blog/" + post.getSlug();
                String entryId = postUrl;
                
                sb.append("  <entry>\n");
                sb.append("    <title>").append(escapeXml(post.getTitle())).append("</title>\n");
                sb.append("    <link href=\"").append(postUrl).append("\" rel=\"alternate\"/>\n");
                sb.append("    <id>").append(entryId).append("</id>\n");
                
                if (post.getExcerpt() != null && !post.getExcerpt().isBlank()) {
                    sb.append("    <summary><![CDATA[").append(post.getExcerpt()).append("]]></summary>\n");
                }
                
                // Add content as well
                sb.append("    <content type=\"html\"><![CDATA[").append(post.getContent()).append("]]></content>\n");
                
                if (post.getAuthor() != null) {
                    sb.append("    <author>\n");
                    sb.append("      <name>").append(escapeXml(post.getAuthor().getName())).append("</name>\n");
                    if (post.getAuthor().getEmail() != null) {
                        sb.append("      <email>").append(escapeXml(post.getAuthor().getEmail())).append("</email>\n");
                    }
                    sb.append("    </author>\n");
                }
                
                if (post.getPublishedAt() != null) {
                    sb.append("    <published>").append(post.getPublishedAt().format(DateTimeFormatter.ISO_INSTANT)).append("</published>\n");
                    sb.append("    <updated>").append(
                        (post.getUpdatedAt() != null ? post.getUpdatedAt() : post.getPublishedAt())
                            .format(DateTimeFormatter.ISO_INSTANT)).append("</updated>\n");
                }
                
                if (post.getCategory() != null) {
                    sb.append("    <category term=\"").append(escapeXml(post.getCategory().getName())).append("\"/>\n");
                }
                
                sb.append("  </entry>\n");
            }
        } catch (Exception e) {
            LOGGER.severe("Error generating Atom feed: " + e.getMessage());
        }
        
        sb.append("</feed>");
        
        LOGGER.info("Generated Atom feed");
        return sb.toString();
    }

    /**
     * Generate RSS feed for a specific category
     */
    public String generateCategoryRssFeed(BlogPost post) {
        // Similar implementation for category feeds
        return generateRssFeed();
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
