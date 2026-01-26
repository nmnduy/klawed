/**
 * Blog Post Page - Client-side Logic
 * 
 * This module handles:
 * - JSON-LD structured data generation (prevents Qute templating issues)
 * - Dynamic meta tag updates
 * - Client-side enhancements
 */

/**
 * Escape special characters for JSON
 * @param {string} str - String to escape
 * @returns {string} - Escaped string
 */
function escapeJson(str) {
    if (!str) return '';
    return str
        .replace(/\\/g, '\\\\')
        .replace(/"/g, '\\"')
        .replace(/\n/g, '\\n')
        .replace(/\r/g, '\\r')
        .replace(/\t/g, '\\t');
}

/**
 * Generate JSON-LD structured data for a blog post
 * @param {Object} data - Blog post data
 * @returns {string} - JSON-LD string
 */
function generateStructuredData(data) {
    const schema = {
        "@context": "https://schema.org",
        "@type": "BlogPosting",
        "headline": data.title
    };

    // Add author if present
    if (data.author) {
        schema.author = {
            "@type": "Person",
            "name": data.author.name
        };
        if (data.author.avatarUrl) {
            schema.author.url = data.author.avatarUrl;
        }
    }

    // Add featured image
    if (data.featuredImageUrl) {
        schema.image = data.featuredImageUrl;
    }

    // Add dates
    if (data.publishedAt) {
        schema.datePublished = data.publishedAt;
    }
    if (data.updatedAt) {
        schema.dateModified = data.updatedAt;
    }

    // Add description
    if (data.description) {
        schema.description = data.description;
    }

    // Add main entity (canonical URL)
    if (data.canonicalUrl) {
        schema.mainEntityOfPage = {
            "@type": "WebPage",
            "@id": data.canonicalUrl
        };
    }

    // Add publisher
    if (data.siteName) {
        schema.publisher = {
            "@type": "Organization",
            "name": data.siteName
        };

        // Add logo if featured image exists
        if (data.featuredImageUrl) {
            schema.publisher.logo = {
                "@type": "ImageObject",
                "url": data.featuredImageUrl
            };
        }
    }

    return JSON.stringify(schema);
}

/**
 * Inject JSON-LD structured data into the page
 * @param {Object} data - Blog post data
 */
function injectStructuredData(data) {
    try {
        const jsonLd = generateStructuredData(data);
        
        // Create script element
        const script = document.createElement('script');
        script.type = 'application/ld+json';
        script.textContent = jsonLd;
        
        // Inject into head
        document.head.appendChild(script);
        
        console.log('✅ JSON-LD structured data injected successfully');
    } catch (error) {
        console.error('❌ Failed to inject structured data:', error);
    }
}

/**
 * Initialize blog post page
 */
function initBlogPost() {
    // Get blog post data from window object (set by template)
    const blogPostData = window.__BLOG_POST_DATA__;
    
    if (!blogPostData) {
        console.warn('⚠️ Blog post data not found');
        return;
    }

    // Inject structured data
    injectStructuredData(blogPostData);

    console.log('✅ Blog post page initialized');
}

// Initialize when DOM is ready
if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', initBlogPost);
} else {
    initBlogPost();
}
