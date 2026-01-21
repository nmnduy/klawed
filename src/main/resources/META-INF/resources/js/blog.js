/**
 * Blog page functionality
 * Fetches blog data via API and renders the page dynamically
 */

// Get page parameter from URL
function getPageFromUrl() {
    const params = new URLSearchParams(window.location.search);
    return parseInt(params.get('page') || '0', 10);
}

// Fetch blog data from API
async function fetchBlogData(page = 0) {
    try {
        const response = await fetch(`/blog/api/home?page=${page}`);
        if (!response.ok) {
            throw new Error(`HTTP error! status: ${response.status}`);
        }
        return await response.json();
    } catch (error) {
        console.error('Error fetching blog data:', error);
        throw error;
    }
}

// Render error message
function renderError(message) {
    const mainContent = document.getElementById('main-content');
    mainContent.innerHTML = `
        <div class="bg-destructive/10 border border-destructive/30 rounded-lg p-6 text-center">
            <h2 class="text-h4-headline-s text-destructive mb-2">Error Loading Blog</h2>
            <p class="text-body-m text-muted-foreground">${escapeHtml(message)}</p>
        </div>
    `;
}

// Render empty state
function renderEmpty() {
    const mainContent = document.getElementById('main-content');
    mainContent.innerHTML = `
        <div class="text-center py-12">
            <svg class="w-16 h-16 mx-auto text-muted-foreground/50 mb-4" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M19 20H5a2 2 0 01-2-2V6a2 2 0 012-2h10a2 2 0 012 2v1m2 13a2 2 0 01-2-2V7m2 13a2 2 0 002-2V9a2 2 0 00-2-2h-2m-4-3H9M7 16h6M7 8h6v4H7V8z" />
            </svg>
            <h2 class="text-h3-headline-m text-foreground mb-2">No posts yet</h2>
            <p class="text-body-m text-muted-foreground">Check back soon for new content!</p>
        </div>
    `;
}

// Render a single blog post card
function renderPostCard(post) {
    const imageHtml = post.featuredImageUrl 
        ? `<a href="/blog/${post.slug}" class="block">
            <img src="${escapeHtml(post.featuredImageUrl)}" alt="${escapeHtml(post.title)}" class="w-full h-48 object-cover" />
           </a>`
        : '';
    
    const categoryHtml = post.category 
        ? `<a href="/blog/category/${post.category.slug}" class="inline-flex items-center text-caption-s font-medium text-primary hover:underline">${escapeHtml(post.category.name)}</a>
           <span class="text-muted-foreground">•</span>`
        : '';
    
    const avatarHtml = post.author?.avatarUrl 
        ? `<img src="${escapeHtml(post.author.avatarUrl)}" alt="${escapeHtml(post.author.name)}" class="w-8 h-8 rounded-full" />`
        : '';
    
    const excerptHtml = post.excerpt 
        ? `<p class="text-muted-foreground line-clamp-3 mb-4">${escapeHtml(post.excerpt)}</p>`
        : '';
    
    return `
        <article class="bg-card/80 backdrop-blur rounded-xl border border-border/60 overflow-hidden transition-all duration-300 hover:shadow-lg hover:border-primary/30 hover:-translate-y-0.5">
            ${imageHtml}
            <div class="p-6">
                <div class="flex items-center gap-2 mb-3">
                    ${categoryHtml}
                    <span class="text-caption-s text-muted-foreground flex items-center gap-4">${escapeHtml(post.readingTimeText || '')}</span>
                    <span class="text-muted-foreground">•</span>
                    <span class="text-caption-s text-muted-foreground flex items-center gap-4">${post.views || 0} views</span>
                </div>
                <h2 class="text-h3-headline-m font-bold text-foreground mb-2">
                    <a href="/blog/${post.slug}" class="hover:text-semantic-link-hover transition-colors">${escapeHtml(post.title)}</a>
                </h2>
                ${excerptHtml}
                <div class="flex items-center justify-between">
                    <div class="flex items-center gap-2">
                        ${avatarHtml}
                        <span class="text-caption-m text-muted-foreground">${escapeHtml(post.author?.name || 'Unknown')}</span>
                    </div>
                    <a href="/blog/${post.slug}" class="text-semantic-link-unvisited hover:text-semantic-link-hover font-medium text-body-s">Read more →</a>
                </div>
            </div>
        </article>
    `;
}

// Render pagination
function renderPagination(data) {
    const { page, perPage, total, hasPrevious, hasMore, previousPage, nextPage } = data;
    
    if (!hasPrevious && !hasMore) {
        return '';
    }
    
    const totalPages = Math.ceil(total / perPage);
    
    const prevButton = hasPrevious 
        ? `<a href="/blog?page=${previousPage}" class="inline-flex items-center px-4 py-2 rounded-lg border border-border bg-card hover:bg-muted transition-colors">
            ← Previous
           </a>`
        : '';
    
    const nextButton = hasMore 
        ? `<a href="/blog?page=${nextPage}" class="inline-flex items-center px-4 py-2 rounded-lg border border-border bg-card hover:bg-muted transition-colors">
            Next →
           </a>`
        : '';
    
    return `
        <div class="flex items-center justify-center gap-2 pt-8">
            ${prevButton}
            <span class="px-4 py-2 text-body-s text-muted-foreground">
                Page ${page + 1} of ${totalPages}
            </span>
            ${nextButton}
        </div>
    `;
}

// Render blog posts
function renderPosts(data) {
    const mainContent = document.getElementById('main-content');
    
    if (data.error) {
        renderError(data.error);
        return;
    }
    
    if (!data.posts || data.posts.length === 0) {
        renderEmpty();
        return;
    }
    
    const postsHtml = data.posts.map(post => renderPostCard(post)).join('');
    const paginationHtml = renderPagination(data);
    
    mainContent.innerHTML = `
        <div class="grid gap-6">
            ${postsHtml}
        </div>
        ${paginationHtml}
    `;
}

// Render categories in sidebar
function renderCategories(categories) {
    const container = document.getElementById('categories-list');
    if (!container || !categories || categories.length === 0) {
        const widget = document.getElementById('categories-widget');
        if (widget) widget.style.display = 'none';
        return;
    }
    
    const html = categories.map(cat => `
        <li>
            <a href="/blog/category/${cat.slug}" class="flex items-center justify-between py-1.5 text-body-s text-muted-foreground hover:text-semantic-link-hover transition-colors">
                <span>${escapeHtml(cat.name)}</span>
            </a>
        </li>
    `).join('');
    
    container.innerHTML = html;
}

// Render popular posts in sidebar
function renderPopularPosts(posts) {
    const container = document.getElementById('popular-posts-list');
    if (!container || !posts || posts.length === 0) {
        const widget = document.getElementById('popular-posts-widget');
        if (widget) widget.style.display = 'none';
        return;
    }
    
    const html = posts.map(post => `
        <li>
            <a href="/blog/${post.slug}" class="block py-1.5">
                <h4 class="text-body-s font-medium text-foreground hover:text-semantic-link-hover transition-colors line-clamp-2">${escapeHtml(post.title)}</h4>
                <span class="text-caption-s text-muted-foreground">${post.views || 0} views</span>
            </a>
        </li>
    `).join('');
    
    container.innerHTML = html;
}

// Render tags in sidebar
function renderTags(tags) {
    const container = document.getElementById('tags-list');
    if (!container || !tags || tags.length === 0) {
        const widget = document.getElementById('tags-widget');
        if (widget) widget.style.display = 'none';
        return;
    }
    
    const html = tags.map(tag => `
        <a href="/blog/tag/${tag.slug}" class="inline-flex items-center px-2.5 py-0.5 rounded-full text-caption-s font-medium bg-primary/10 text-primary dark:bg-primary/20 dark:text-primary">${escapeHtml(tag.name)}</a>
    `).join('');
    
    container.innerHTML = html;
}

// Escape HTML to prevent XSS
function escapeHtml(text) {
    const div = document.createElement('div');
    div.textContent = text;
    return div.innerHTML;
}

// Initialize the page
async function init() {
    const page = getPageFromUrl();
    const loadingIndicator = document.getElementById('loading-indicator');
    const mainContent = document.getElementById('main-content');
    
    try {
        // Show loading state
        if (loadingIndicator) loadingIndicator.style.display = 'block';
        if (mainContent) mainContent.style.display = 'none';
        
        // Fetch data
        const data = await fetchBlogData(page);
        
        // Hide loading, show content
        if (loadingIndicator) loadingIndicator.style.display = 'none';
        if (mainContent) mainContent.style.display = 'block';
        
        // Render everything
        renderPosts(data);
        renderCategories(data.categories);
        renderPopularPosts(data.popularPosts);
        renderTags(data.tags);
        
    } catch (error) {
        console.error('Failed to initialize blog:', error);
        if (loadingIndicator) loadingIndicator.style.display = 'none';
        if (mainContent) mainContent.style.display = 'block';
        renderError(error.message || 'Failed to load blog data');
    }
}

// Start when DOM is ready
if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', init);
} else {
    init();
}
