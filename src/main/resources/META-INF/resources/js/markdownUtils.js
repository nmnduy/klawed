/**
 * Markdown Rendering Utility
 * Parses and sanitizes markdown content for safe rendering in chat messages
 */

import { marked } from '../dist/marked.esm.js';

/**
 * Configure marked options for security and consistency
 */
marked.setOptions({
    gfm: true,           // Enable GitHub Flavored Markdown
    breaks: true,        // Convert \n to <br>
    headerIds: false,    // Don't add IDs to headers
    mangle: false,       // Don't mangle header IDs
});

/**
 * Sanitize HTML to prevent XSS attacks
 * @param {string} html - HTML string to sanitize
 * @returns {string} - Sanitized HTML string
 */
function sanitizeHtml(html) {
    // Create a temporary element to parse the HTML
    const tempDiv = document.createElement('div');
    tempDiv.innerHTML = html;
    
    // Remove any script tags
    const scripts = tempDiv.querySelectorAll('script');
    scripts.forEach(script => script.remove());
    
    // Remove any event handlers from elements
    const allElements = tempDiv.querySelectorAll('*');
    allElements.forEach(el => {
        // Remove all event handler attributes
        const eventAttributes = Array.from(el.attributes).filter(attr => 
            attr.name.startsWith('on') || 
            attr.name === 'javascript:'
        );
        eventAttributes.forEach(attr => el.removeAttribute(attr.name));
        
        // Remove data: URLs which could be used for XSS
        if (el.href && el.href.startsWith('data:')) {
            el.removeAttribute('href');
        }
        
        // Remove action attributes from forms
        if (el.hasAttribute('action')) {
            el.removeAttribute('action');
        }
    });
    
    // Remove dangerous attributes from specific elements
    const dangerousTags = ['iframe', 'object', 'embed', 'form'];
    dangerousTags.forEach(tagName => {
        const elements = tempDiv.querySelectorAll(tagName);
        elements.forEach(el => el.remove());
    });
    
    return tempDiv.innerHTML;
}

/**
 * Process code blocks to add syntax highlighting class and security
 * @param {string} code - Code content
 * @param {string} language - Programming language
 * @returns {string} - Processed HTML
 */
function processCodeBlocks(code, language) {
    const escapedCode = escapeHtml(code.trim());
    const langClass = language ? `language-${language}` : 'language-text';
    return `<pre class="${langClass}"><code>${escapedCode}</code></pre>`;
}

/**
 * Escape HTML special characters
 * @param {string} text - Text to escape
 * @returns {string} - Escaped text
 */
function escapeHtml(text) {
    const div = document.createElement('div');
    div.textContent = text;
    return div.innerHTML;
}

/**
 * Parse markdown content to HTML
 * @param {string} markdown - Markdown text to parse
 * @returns {string} - Parsed HTML
 */
export function parseMarkdown(markdown) {
    if (!markdown || typeof markdown !== 'string') {
        return '';
    }
    
    try {
        // Configure marked with custom renderer for additional security
        const renderer = new marked.Renderer();
        
        // Override code block rendering for security
        renderer.code = function(code, language) {
            // Basic sanitization of code content
            const sanitizedCode = escapeHtml(code);
            const langClass = language ? `language-${escapeHtml(language)}` : 'language-text';
            return `<pre class="${langClass}"><code>${sanitizedCode}</code></pre>`;
        };
        
        // Override link rendering for security
        renderer.link = function(href, title, text) {
            // Only allow safe protocols
            const safeProtocols = ['http:', 'https:', 'mailto:'];
            let safeHref = href;
            
            try {
                const url = new URL(href, window.location.href);
                if (!safeProtocols.includes(url.protocol)) {
                    safeHref = '#';
                }
            } catch (e) {
                // Invalid URL, use safe default
                safeHref = '#';
            }
            
            const titleAttr = title ? ` title="${escapeHtml(title)}"` : '';
            return `<a href="${safeHref}"${titleAttr} target="_blank" rel="noopener noreferrer" class="markdown-link">${text}</a>`;
        };
        
        // Override image rendering for security
        renderer.image = function(href, title, text) {
            // Only allow safe image protocols
            const safeProtocols = ['http:', 'https:'];
            let safeHref = href;
            
            try {
                const url = new URL(href, window.location.href);
                if (!safeProtocols.includes(url.protocol)) {
                    safeHref = '';
                }
            } catch (e) {
                safeHref = '';
            }
            
            const titleAttr = title ? ` title="${escapeHtml(title)}"` : '';
            return safeHref ? `<img src="${safeHref}" alt="${escapeHtml(text)}"${titleAttr} class="markdown-image">` : '';
        };
        
        // Override table rendering
        renderer.table = function(header, body) {
            return `<div class="markdown-table-wrapper"><table class="markdown-table"><thead>${header}</thead><tbody>${body}</tbody></table></div>`;
        };
        
        // Set custom renderer
        marked.use({ renderer });
        
        // Parse markdown to HTML
        let html = marked.parse(markdown);
        
        // Sanitize the resulting HTML
        html = sanitizeHtml(html);
        
        return html;
    } catch (error) {
        console.error('Markdown parsing error:', error);
        // Fallback to escaped plain text if parsing fails
        return escapeHtml(markdown);
    }
}

/**
 * Render markdown content to a DOM element
 * @param {string} markdown - Markdown text to render
 * @param {HTMLElement} container - Container element to render into
 * @param {boolean} isUserMessage - Whether this is a user message (user messages don't get markdown parsed)
 */
export function renderMarkdown(markdown, container, isUserMessage = false) {
    if (!container || !markdown) {
        return;
    }
    
    // User messages are displayed as plain text (no markdown parsing)
    if (isUserMessage) {
        container.textContent = markdown;
        return;
    }
    
    // AI messages get markdown parsing
    const html = parseMarkdown(markdown);
    container.innerHTML = html;
}

/**
 * Utility to check if text contains markdown formatting
 * @param {string} text - Text to check
 * @returns {boolean} - True if text likely contains markdown
 */
export function containsMarkdown(text) {
    if (!text || typeof text !== 'string') {
        return false;
    }
    
    const markdownPatterns = [
        /^#{1,6}\s/gm,           // Headers
        /\*\*[^*]+\*\*/g,         // Bold
        /\*[^*]+\*/g,            // Italic
        /__[^_]+__/g,            // Bold (underscore)
        /_[^_]+_/g,              // Italic (underscore)
        /```[\s\S]*?```/g,       // Code blocks
        /`[^`]+`/g,              // Inline code
        /\[([^\]]+)\]\([^)]+\)/g, // Links
        /^\s*[-*+]\s/gm,         // Unordered list
        /^\s*\d+\.\s/gm,         // Ordered list
        /^\s*>/gm,               // Blockquotes
        /\|[^|]+\|/g,            // Table cells
    ];
    
    return markdownPatterns.some(pattern => pattern.test(text));
}
