package com.filesurf.service;

import com.filesurf.db.BlogDatabaseManager;
import com.filesurf.model.BlogAuthor;
import com.filesurf.model.BlogPost;
import jakarta.enterprise.context.ApplicationScoped;
import jakarta.inject.Inject;
import org.eclipse.microprofile.config.inject.ConfigProperty;

import java.time.LocalDateTime;
import java.time.ZoneOffset;
import java.time.format.DateTimeFormatter;
import java.util.List;
import java.util.logging.Logger;
import java.util.regex.Pattern;

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

    // RFC 822 date format for RSS 2.0
    private static final DateTimeFormatter RFC_822_FORMATTER = 
            DateTimeFormatter.ofPattern("EEE, dd MMM yyyy HH:mm:ss Z", java.util.Locale.ENGLISH);

    // W3C date format for Atom
    private static final DateTimeFormatter W3C_FORMATTER = 
            DateTimeFormatter.ofPattern("yyyy-MM-dd'T'HH:mm:ssXXX");

    // XML character escape pattern
    private static final Pattern AMP_PATTERN = Pattern.compile("&");
    private static final Pattern LT_PATTERN = Pattern.compile("<");
    private static final Pattern GT_PATTERN = Pattern.compile(">");
    private static final Pattern QUOTE_PATTERN = Pattern.compile("\"");
    private static final Pattern APOS_PATTERN = Pattern.compile("'");

    /**
     * Generate RSS 2.0 XML feed
     */
    public String generateRssFeed() {
        StringBuilder xml = new StringBuilder();
        List<BlogPost> posts = getPublishedPosts();

        xml.append("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
        xml.append("<rss version=\"2.0\" xmlns:atom=\"http://www.w3.org/2005/Atom\">\n");
        xml.append("  <channel>\n");
        xml.append("    <title>").append(escapeXml(siteName)).append("</title>\n");
        xml.append("    <description>").append(escapeXml(siteDescription)).append("</description>\n");
        xml.append("    <link>").append(escapeXml(siteUrl)).append("</link>\n");
        xml.append("    <language>en-us</language>\n");
        xml.append("    <atom:link href=\"").append(escapeXml(siteUrl)).append("/feed/rss\" rel=\"self\" type=\"application/rss+xml\"/>\n");

        for (BlogPost post : posts) {
            xml.append("    <item>\n");
            xml.append("      <title>").append(escapeXml(post.getTitle())).append("</title>\n");
            xml.append("      <description>").append(escapeXml(getPostDescription(post))).append("</description>\n");
            xml.append("      <link>").append(escapeXml(getPostUrl(post))).append("</link>\n");
            xml.append("      <guid isPermaLink=\"false\">").append(escapeXml(getPostGuid(post))).append("</guid>\n");
            
            if (post.getPublishedAt() != null) {
                xml.append("      <pubDate>").append(formatRfc822Date(post.getPublishedAt())).append("</pubDate>\n");
            }
            
            if (post.getAuthor() != null && post.getAuthor().getEmail() != null) {
                xml.append("      <author>").append(escapeXml(post.getAuthor().getEmail()));
                if (post.getAuthor().getName() != null) {
                    xml.append(" (").append(escapeXml(post.getAuthor().getName())).append(")");
                }
                xml.append("</author>\n");
            }
            
            xml.append("    </item>\n");
        }

        xml.append("  </channel>\n");
        xml.append("</rss>");

        return xml.toString();
    }

    /**
     * Generate Atom 1.0 XML feed
     */
    public String generateAtomFeed() {
        StringBuilder xml = new StringBuilder();
        List<BlogPost> posts = getPublishedPosts();
        LocalDateTime now = LocalDateTime.now();

        xml.append("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
        xml.append("<feed xmlns=\"http://www.w3.org/2005/Atom\">\n");
        xml.append("  <title>").append(escapeXml(siteName)).append("</title>\n");
        xml.append("  <subtitle>").append(escapeXml(siteDescription)).append("</subtitle>\n");
        xml.append("  <updated>").append(formatW3cDate(now)).append("</updated>\n");
        xml.append("  <id>").append(escapeXml(siteUrl)).append("/feed/atom</id>\n");
        xml.append("  <link href=\"").append(escapeXml(siteUrl)).append("\" rel=\"alternate\" type=\"text/html\"/>\n");
        xml.append("  <link href=\"").append(escapeXml(siteUrl)).append("/feed/atom\" rel=\"self\" type=\"application/atom+xml\"/>\n");

        for (BlogPost post : posts) {
            xml.append("  <entry>\n");
            xml.append("    <title>").append(escapeXml(post.getTitle())).append("</title>\n");
            xml.append("    <summary type=\"html\">").append(escapeXml(getPostDescription(post))).append("</summary>\n");
            xml.append("    <id>").append(escapeXml(getPostUrl(post))).append("</id>\n");
            
            if (post.getPublishedAt() != null) {
                xml.append("    <published>").append(formatW3cDate(post.getPublishedAt())).append("</published>\n");
                xml.append("    <updated>").append(formatW3cDate(
                        post.getUpdatedAt() != null ? post.getUpdatedAt() : post.getPublishedAt()
                )).append("</updated>\n");
            }
            
            if (post.getAuthor() != null) {
                xml.append("    <author>\n");
                xml.append("      <name>").append(escapeXml(post.getAuthor().getName() != null ? 
                        post.getAuthor().getName() : "Unknown")).append("</name>\n");
                if (post.getAuthor().getEmail() != null) {
                    xml.append("      <email>").append(escapeXml(post.getAuthor().getEmail())).append("</email>\n");
                }
                xml.append("    </author>\n");
            }
            
            xml.append("    <link href=\"").append(escapeXml(getPostUrl(post))).append("\" rel=\"alternate\" type=\"text/html\"/>\n");
            
            xml.append("  </entry>\n");
        }

        xml.append("</feed>");

        return xml.toString();
    }

    /**
     * Get published posts for the feed
     */
    private List<BlogPost> getPublishedPosts() {
        try {
            return db.getPublishedPosts(maxItems, 0);
        } catch (Exception e) {
            LOGGER.warning("Failed to fetch published posts for feed: " + e.getMessage());
            return List.of();
        }
    }

    /**
     * Get post description (excerpt or content snippet)
     */
    private String getPostDescription(BlogPost post) {
        if (post.getExcerpt() != null && !post.getExcerpt().isEmpty()) {
            return post.getExcerpt();
        }
        if (post.getContent() != null && !post.getContent().isEmpty()) {
            // Strip HTML tags and return plain text
            String content = post.getContent().replaceAll("<[^>]*>", "");
            return content.length() > 300 ? content.substring(0, 297) + "..." : content;
        }
        return "";
    }

    /**
     * Get full URL for a post
     */
    private String getPostUrl(BlogPost post) {
        return siteUrl + "/blog/" + post.getSlug();
    }

    /**
     * Get unique identifier for a post
     */
    private String getPostGuid(BlogPost post) {
        return siteUrl + "/blog/" + post.getSlug();
    }

    /**
     * Format LocalDateTime to RFC 822 format for RSS
     */
    private String formatRfc822Date(LocalDateTime dateTime) {
        return dateTime.atOffset(ZoneOffset.UTC).format(RFC_822_FORMATTER);
    }

    /**
     * Format LocalDateTime to W3C format for Atom
     */
    private String formatW3cDate(LocalDateTime dateTime) {
        return dateTime.atOffset(ZoneOffset.UTC).format(W3C_FORMATTER);
    }

    /**
     * Escape XML special characters
     */
    private String escapeXml(String text) {
        if (text == null) {
            return "";
        }
        return AMP_PATTERN.matcher(
                LT_PATTERN.matcher(
                        GT_PATTERN.matcher(
                                QUOTE_PATTERN.matcher(
                                        APOS_PATTERN.matcher(text).replaceAll("&apos;"))
                                .replaceAll("&quot;"))
                        .replaceAll("&gt;"))
                .replaceAll("&lt;"))
                .replaceAll("&amp;");
    }
}
