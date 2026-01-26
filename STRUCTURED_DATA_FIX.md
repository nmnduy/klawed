# Structured Data JSON-LD Parsing Error - Fixed

## Problem

Google Search Console reported: **"Parsing error: Missing '}' or object member name"** for structured data on filesurf.io blog posts.

## Root Cause

The issue was **NOT** in the Java code that generates the JSON-LD structured data. The `BlogService.generateStructuredData()` method was generating perfectly valid JSON.

The real issue was in the **Qute template rendering**:

```html
<!-- BEFORE (Broken) -->
<script type="application/ld+json">{structuredData}</script>
```

By default, Qute **HTML-escapes** all template variables to prevent XSS attacks. This meant:
- `"` became `&quot;`
- `{` became `&#123;`
- `}` became `&#125;`

This turned valid JSON into invalid HTML-escaped text that couldn't be parsed as JSON-LD.

## Solution

Use Qute's `.raw` accessor to prevent HTML escaping:

```html
<!-- AFTER (Fixed) -->
<script type="application/ld+json">{structuredData.raw}</script>
```

## Verification

The generated JSON-LD structure is:

```json
{
  "@context": "https://schema.org",
  "@type": "BlogPosting",
  "headline": "Post Title",
  "author": {
    "@type": "Person",
    "name": "Author Name",
    "url": "Author Avatar URL"
  },
  "image": "Featured Image URL",
  "datePublished": "2026-01-26T12:00:00",
  "dateModified": "2026-01-26T13:00:00",
  "description": "Post description or excerpt",
  "mainEntityOfPage": {
    "@type": "WebPage",
    "@id": "https://filesurf.io/blog/post-slug"
  },
  "publisher": {
    "@type": "Organization",
    "name": "FileSurf",
    "logo": {
      "@type": "ImageObject",
      "url": "Featured Image URL"
    }
  }
}
```

This is valid JSON-LD and conforms to Schema.org's BlogPosting specification.

## Files Changed

- `src/main/resources/templates/blog-post.html` - Added `.raw` to prevent HTML escaping

## Testing

After deployment, verify the fix by:

1. **View Page Source**: Visit any blog post and check the `<script type="application/ld+json">` tag
2. **Copy JSON**: Extract the JSON-LD content
3. **Validate**: Use [Google's Rich Results Test](https://search.google.com/test/rich-results) or [Schema.org Validator](https://validator.schema.org/)
4. **Google Search Console**: Wait for Google to re-crawl the pages and verify the error is resolved

## Related Context

- This is similar to how we handle HTML content in blog posts: `{post.htmlContent.raw}`
- Qute documentation on raw output: https://quarkus.io/guides/qute-reference#raw_string
- The Java code in `BlogService.generateStructuredData()` was already correct and didn't need changes
