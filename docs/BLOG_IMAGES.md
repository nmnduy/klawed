# Blog Image Management Guide

This guide explains how to work with images in FileSurf blog posts.

## Table of Contents
- [Overview](#overview)
- [Image Support](#image-support)
- [Publishing Blog Posts with Images](#publishing-blog-posts-with-images)
- [Directory Structure](#directory-structure)
- [Best Practices](#best-practices)
- [Troubleshooting](#troubleshooting)

---

## Overview

FileSurf's blog infrastructure supports images in two ways:
1. **Featured Images** - A single hero image displayed at the top of each post
2. **Inline Images** - Images embedded within blog post content via Markdown

Both are fully supported and rendered with proper responsive styling.

---

## Image Support

### Featured Images
- Set via `featured_image_url` field in database
- Displayed at top of blog post
- Used in social media previews (Open Graph, Twitter Cards)
- Recommended size: 1200x630px (2:1 ratio)

### Inline Images
- Embedded using standard Markdown syntax: `![Alt text](/path/to/image.png)`
- Automatically rendered to HTML via CommonMark parser
- Supports PNG, JPG, GIF, WebP formats
- Responsive by default via Tailwind CSS prose classes

---

## Publishing Blog Posts with Images

### Quick Start

```bash
# 1. Prepare your blog post with images
./scripts/prepare-blog-images.sh

# 2. Publish to database
./scripts/publish-blog-post.sh <post-slug>

# 3. Test locally
mvn quarkus:dev
# Visit: http://localhost:9090/blog/<post-slug>

# 4. Deploy to production
./scripts/deploy-blog.sh
```

### Step-by-Step Process

#### 1. Prepare Your Content

Place your blog post and images in a directory:
```
filesurf-demo/
├── BLOG_POST.md
├── 01-screenshot.png
├── 02-screenshot.png
└── ...
```

Your markdown should reference images with simple filenames:
```markdown
![Description](01-screenshot.png)
```

#### 2. Run Image Preparation Script

```bash
./scripts/prepare-blog-images.sh
```

This script:
- ✅ Copies all PNG images to `src/main/resources/META-INF/resources/assets/blog/<post-slug>/`
- ✅ Updates image paths in markdown to use `/assets/blog/<post-slug>/` URLs
- ✅ Creates a web-ready version: `BLOG_POST_WEB.md`
- ✅ Verifies all images exist

**Output:**
```
✓ Created web-ready blog post: filesurf-demo/BLOG_POST_WEB.md
✓ Updated 16 image paths
✓ All images verified ✓
```

#### 3. Publish to Database

```bash
# Publish as "published" (live immediately)
./scripts/publish-blog-post.sh filesurf-demo

# Or publish as "draft" (hidden from public)
./scripts/publish-blog-post.sh filesurf-demo --draft
```

This script:
- ✅ Extracts title, excerpt, and metadata from markdown
- ✅ Calculates reading time based on word count
- ✅ Sets featured image (first image in post)
- ✅ Inserts/updates blog post in SQLite database
- ✅ Adds relevant tags automatically

**Output:**
```
✓ Blog post published successfully!

Post Details:
  ID: 2
  Title: From Laptop Chaos to FileSurf: Automating a Residential...
  Slug: filesurf-demo
  Status: published
  Reading time: 18 minutes
  Word count: 3541

View at:
  Local:  http://localhost:9090/blog/filesurf-demo
  Production: https://filesurf.io/blog/filesurf-demo
```

#### 4. Test Locally

Start Quarkus in development mode:
```bash
mvn quarkus:dev
```

Visit your blog post:
```
http://localhost:9090/blog/filesurf-demo
```

**What to verify:**
- ✅ Featured image displays at top
- ✅ All inline images render correctly
- ✅ Images are responsive (resize browser window)
- ✅ Images load quickly (check browser network tab)
- ✅ Alt text is present (hover over images)

#### 5. Deploy to Production

```bash
./scripts/deploy-blog.sh
```

This will:
1. Export blog database from local development
2. Create backup of production database
3. Import new blog post
4. Verify blog is accessible
5. Test specific blog post URL

**Note:** Image files are already deployed since they're in `src/main/resources/META-INF/resources/assets/`. The regular app deployment (`./deployment/deploy-rsync.sh`) will sync these files.

---

## Directory Structure

### Local Development
```
filesurf_v2/
├── filesurf-demo/                          # Source blog post directory
│   ├── BLOG_POST.md                        # Original markdown
│   ├── BLOG_POST_WEB.md                    # Web-ready (updated paths)
│   └── *.png                               # Original images
│
├── src/main/resources/META-INF/resources/
│   └── assets/
│       └── blog/
│           └── filesurf-demo/              # Published images
│               ├── 01-screenshot.png
│               ├── 02-screenshot.png
│               └── ...
│
├── data/
│   └── blog.db                             # SQLite blog database
│
└── scripts/
    ├── prepare-blog-images.sh              # Prepare images for web
    ├── publish-blog-post.sh                # Publish to database
    └── deploy-blog.sh                      # Deploy to production
```

### Production
```
/var/lib/filesurf/
├── data/
│   └── blog.db                             # Production blog database
│
└── (app deployment includes assets/)
```

Images are served at:
```
https://filesurf.io/assets/blog/filesurf-demo/01-screenshot.png
```

---

## Best Practices

### Image Naming
- ✅ Use descriptive prefixes: `01-feature-name.png`, `02-setup-screen.png`
- ✅ Use lowercase and hyphens: `google-sheets-integration.png`
- ❌ Avoid spaces: `Google Sheets Integration.png`
- ❌ Avoid special characters: `screenshot@2x.png`

### Image Optimization
- **Before uploading**, optimize images:
  ```bash
  # Install imagemagick
  sudo apt install imagemagick
  
  # Resize large screenshots (max width 1200px)
  mogrify -resize 1200x\> *.png
  
  # Or use pngquant for compression
  pngquant --quality=65-80 *.png
  ```

- **Recommended sizes:**
  - Screenshots: 1200px wide max
  - Featured images: 1200x630px (2:1 ratio)
  - Diagrams: 800px wide max

### Accessibility
- ✅ Always include descriptive alt text
  ```markdown
  ![User uploading a file to FileSurf interface](upload-screen.png)
  ```
- ❌ Don't use generic alt text
  ```markdown
  ![Image](screenshot.png)  # Bad
  ```

### Performance
- Keep individual images under 500KB
- Total post images under 5MB
- Use WebP for smaller file sizes (requires additional setup)

---

## Troubleshooting

### Images Not Showing

**Symptom:** Broken image icon in browser

**Check:**
1. **Path is correct:**
   ```bash
   # Should be:
   /assets/blog/filesurf-demo/01-image.png
   
   # Not:
   01-image.png
   assets/blog/filesurf-demo/01-image.png (missing leading /)
   ```

2. **File exists:**
   ```bash
   ls src/main/resources/META-INF/resources/assets/blog/filesurf-demo/
   ```

3. **Quarkus reloaded:**
   - Wait 5 seconds after saving files
   - Check Quarkus console for "Reloading"
   - Refresh browser (Cmd/Ctrl + Shift + R)

### Script Errors

**Error:** `Could not extract title from blog post`

**Fix:** Ensure your markdown has a proper H1 heading:
```markdown
# Your Title Here

Your content...
```

**Error:** `Database not found: data/blog.db`

**Fix:** Initialize the blog database first (it should exist if you've run the app once):
```bash
mvn quarkus:dev
# Let it start, then Ctrl+C
```

### Image Path Mistakes

If you published without running `prepare-blog-images.sh`:

```bash
# 1. Fix the paths
./scripts/prepare-blog-images.sh

# 2. Re-publish (will update existing post)
./scripts/publish-blog-post.sh filesurf-demo
```

### Deploy Issues

**Error:** Blog post shows on local but not production

**Cause:** You published to local database but haven't deployed

**Fix:**
```bash
./scripts/deploy-blog.sh
```

**Error:** Images work locally but 404 in production

**Cause:** Image files weren't deployed with the app

**Fix:**
```bash
# Full app deployment (includes assets)
./deployment/deploy-rsync.sh
```

---

## Advanced: Custom Image Handling

### Multiple Post Directories

If you have multiple blog posts to publish:

```bash
# Structure:
blog-posts/
├── post-1/
│   ├── BLOG_POST.md
│   └── *.png
├── post-2/
│   ├── BLOG_POST.md
│   └── *.png
```

Modify scripts to accept directory parameter:
```bash
./scripts/prepare-blog-images.sh blog-posts/post-1
./scripts/publish-blog-post.sh post-1
```

### External Images

To use externally hosted images (CDN, etc.):

```markdown
![Alt text](https://cdn.example.com/images/screenshot.png)
```

No script changes needed - Markdown will render external URLs as-is.

### Featured Image Override

To use a different featured image (not the first image in content):

```bash
# After publishing, manually update:
sqlite3 data/blog.db "UPDATE blog_posts SET featured_image_url = '/assets/blog/filesurf-demo/hero-image.png' WHERE slug = 'filesurf-demo';"
```

---

## Summary

| Action | Command | Purpose |
|--------|---------|---------|
| **Prepare images** | `./scripts/prepare-blog-images.sh` | Copy images & update paths |
| **Publish post** | `./scripts/publish-blog-post.sh <slug>` | Add to database |
| **Test locally** | `mvn quarkus:dev` | Preview before deploy |
| **Deploy** | `./scripts/deploy-blog.sh` | Push to production |

**Workflow:**
```
Write BLOG_POST.md → prepare-blog-images.sh → publish-blog-post.sh → Test → deploy-blog.sh
```

---

## Related Documentation
- `scripts/deploy-blog.sh` - Production deployment script
- `src/main/java/com/filesurf/service/MarkdownService.java` - Markdown rendering
- `src/main/resources/templates/blog-post.html` - Blog post template
- `KLAWED.md` - General project documentation

---

**Questions or issues?** Check the troubleshooting section above or review the script output for specific error messages.
