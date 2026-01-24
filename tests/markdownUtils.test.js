/**
 * Unit tests for markdownUtils.js markdown rendering
 * 
 * Tests the markdown parsing and rendering logic, including file:// protocol support.
 * These tests use the actual marked library to verify rendering behavior.
 * 
 * Run with: bun test tests/markdownUtils.test.js
 */

import { describe, it, expect, beforeAll } from 'bun:test';
import { JSDOM } from 'jsdom';

// We'll import marked directly and recreate the renderer logic for testing
// since the actual module uses browser-specific paths
import { marked } from 'marked';

// Set up DOM environment
let dom;
let document;
let window;

beforeAll(() => {
    dom = new JSDOM('<!DOCTYPE html><html><body></body></html>', {
        url: 'http://localhost/',
    });
    window = dom.window;
    document = dom.window.document;
    
    // Make globals available
    global.window = window;
    global.document = document;
});

/**
 * Escape HTML special characters
 */
function escapeHtml(text) {
    const div = document.createElement('div');
    div.textContent = text;
    return div.innerHTML;
}

/**
 * Sanitize HTML to prevent XSS attacks (simplified version)
 */
function sanitizeHtml(html) {
    const tempDiv = document.createElement('div');
    tempDiv.innerHTML = html;
    
    // Remove script tags
    const scripts = tempDiv.querySelectorAll('script');
    scripts.forEach(script => script.remove());
    
    // Remove event handlers
    const allElements = tempDiv.querySelectorAll('*');
    allElements.forEach(el => {
        const eventAttributes = Array.from(el.attributes).filter(attr => 
            attr.name.startsWith('on') || 
            attr.name === 'javascript:'
        );
        eventAttributes.forEach(attr => el.removeAttribute(attr.name));
    });
    
    return tempDiv.innerHTML;
}

/**
 * Parse markdown to HTML - mirrors the logic in markdownUtils.js
 * Updated for marked v17+ API
 */
function parseMarkdown(markdown) {
    if (!markdown || typeof markdown !== 'string') {
        return '';
    }
    
    try {
        const renderer = new marked.Renderer();
        
        // Override code block rendering
        // marked v17+ passes a token object: {text, lang, escaped}
        renderer.code = function({text, lang, escaped}) {
            const sanitizedCode = escapeHtml(text || '');
            const langClass = lang ? `language-${escapeHtml(lang)}` : 'language-text';
            return `<pre class="${langClass}"><code>${sanitizedCode}</code></pre>`;
        };
        
        // Override link rendering for file:// protocol support
        // marked v17+ passes a token object: {href, title, tokens}
        renderer.link = function({href, title, tokens}) {
            const text = this.parser.parseInline(tokens);
            
            if (href && typeof href === 'string' && href.startsWith('file://')) {
                const filePath = href.replace(/^file:\/\/\/?/, '/');
                const escapedPath = escapeHtml(filePath);
                const titleAttr = title ? ` title="${escapeHtml(title)}"` : ` title="Click to view in file explorer"`;
                return `<a href="#" data-file-path="${escapedPath}"${titleAttr} class="markdown-link file-link">📄 ${text}</a>`;
            }
            
            const safeProtocols = ['http:', 'https:', 'mailto:'];
            let safeHref = href || '';
            
            try {
                const url = new URL(safeHref, 'http://localhost/');
                if (!safeProtocols.includes(url.protocol)) {
                    safeHref = '#';
                }
            } catch (e) {
                safeHref = '#';
            }
            
            const titleAttr = title ? ` title="${escapeHtml(title)}"` : '';
            return `<a href="${safeHref}"${titleAttr} target="_blank" rel="noopener noreferrer" class="markdown-link">${text}</a>`;
        };
        
        // Override image rendering
        // marked v17+ passes a token object: {href, title, text}
        renderer.image = function({href, title, text}) {
            const safeProtocols = ['http:', 'https:'];
            let safeHref = href || '';
            
            try {
                const url = new URL(safeHref, 'http://localhost/');
                if (!safeProtocols.includes(url.protocol)) {
                    safeHref = '';
                }
            } catch (e) {
                safeHref = '';
            }
            
            const titleAttr = title ? ` title="${escapeHtml(title)}"` : '';
            return safeHref ? `<img src="${safeHref}" alt="${escapeHtml(text || '')}"${titleAttr} class="markdown-image">` : '';
        };
        
        // Override table rendering
        // marked v17+ passes a token object: {header, rows}
        renderer.table = function({header, rows}) {
            let headerHtml = '<tr>';
            for (const cell of header) {
                const align = cell.align ? ` style="text-align:${cell.align}"` : '';
                headerHtml += `<th${align}>${this.parser.parseInline(cell.tokens)}</th>`;
            }
            headerHtml += '</tr>';
            
            let bodyHtml = '';
            for (const row of rows) {
                bodyHtml += '<tr>';
                for (const cell of row) {
                    const align = cell.align ? ` style="text-align:${cell.align}"` : '';
                    bodyHtml += `<td${align}>${this.parser.parseInline(cell.tokens)}</td>`;
                }
                bodyHtml += '</tr>';
            }
            
            return `<div class="markdown-table-wrapper"><table class="markdown-table"><thead>${headerHtml}</thead><tbody>${bodyHtml}</tbody></table></div>`;
        };
        
        marked.use({ renderer });
        
        let html = marked.parse(markdown);
        html = sanitizeHtml(html);
        
        if (html && !html.includes('class="markdown-content"')) {
            html = `<div class="markdown-content">${html}</div>`;
        }
        
        return html;
    } catch (error) {
        console.error('Markdown parsing error:', error);
        return escapeHtml(markdown);
    }
}

/**
 * Check if text contains markdown formatting
 */
function containsMarkdown(text) {
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


// ============================================================================
// Test: parseMarkdown with file:// links
// ============================================================================

describe('parseMarkdown - file:// protocol links', () => {
    it('should render file:// links with proper attributes', () => {
        const markdown = 'Check out [my document](file:///path/to/document.pdf)';
        const html = parseMarkdown(markdown);
        
        expect(html).toContain('data-file-path="/path/to/document.pdf"');
        expect(html).toContain('class="markdown-link file-link"');
        expect(html).toContain('📄');
        expect(html).toContain('my document');
    });

    it('should handle file:/// with three slashes', () => {
        const markdown = '[report.pdf](file:///reports/quarterly_report.pdf)';
        const html = parseMarkdown(markdown);
        
        expect(html).toContain('data-file-path="/reports/quarterly_report.pdf"');
        expect(html).toContain('href="#"');
    });

    it('should handle file:// with two slashes', () => {
        const markdown = '[analysis.csv](file://analysis.csv)';
        const html = parseMarkdown(markdown);
        
        expect(html).toContain('data-file-path="/analysis.csv"');
    });

    it('should handle file paths with spaces (URL encoded)', () => {
        const markdown = '[my file](file:///path/to/my%20file.txt)';
        const html = parseMarkdown(markdown);
        
        expect(html).toContain('data-file-path');
        expect(html).toContain('my file');
    });

    it('should handle multiple file:// links in same message', () => {
        const markdown = `Here are the files:
- [report.pdf](file:///reports/report.pdf)
- [data.csv](file:///data/data.csv)
- [notes.md](file:///notes/notes.md)`;
        
        const html = parseMarkdown(markdown);
        
        expect(html).toContain('data-file-path="/reports/report.pdf"');
        expect(html).toContain('data-file-path="/data/data.csv"');
        expect(html).toContain('data-file-path="/notes/notes.md"');
    });

    it('should add default title for file links', () => {
        const markdown = '[document.txt](file:///document.txt)';
        const html = parseMarkdown(markdown);
        
        expect(html).toContain('title="Click to view in file explorer"');
    });

    it('should preserve custom title for file links', () => {
        const markdown = '[document.txt](file:///document.txt "View the main document")';
        const html = parseMarkdown(markdown);
        
        expect(html).toContain('title="View the main document"');
    });

    it('should render typical AI response with file link', () => {
        // This is the typical format klawed uses
        const markdown = `I've created the report for you: [quarterly_report.pdf](file:///reports/quarterly_report.pdf)

You can click the link above to view it in the file explorer.`;
        
        const html = parseMarkdown(markdown);
        
        expect(html).toContain('data-file-path="/reports/quarterly_report.pdf"');
        expect(html).toContain('quarterly_report.pdf');
        expect(html).toContain('click the link');
    });

    it('should handle nested paths correctly', () => {
        const markdown = '[deep file](file:///a/b/c/d/e/file.txt)';
        const html = parseMarkdown(markdown);
        
        expect(html).toContain('data-file-path="/a/b/c/d/e/file.txt"');
    });

    it('should handle various file extensions', () => {
        const extensions = ['pdf', 'csv', 'txt', 'md', 'json', 'xml', 'html', 'py', 'js'];
        
        for (const ext of extensions) {
            const markdown = `[file.${ext}](file:///path/file.${ext})`;
            const html = parseMarkdown(markdown);
            
            expect(html).toContain(`data-file-path="/path/file.${ext}"`);
        }
    });
});

// ============================================================================
// Test: parseMarkdown with regular links
// ============================================================================

describe('parseMarkdown - regular links', () => {
    it('should render https:// links normally', () => {
        const markdown = 'Visit [Google](https://google.com)';
        const html = parseMarkdown(markdown);
        
        expect(html).toContain('href="https://google.com"');
        expect(html).toContain('target="_blank"');
        expect(html).toContain('rel="noopener noreferrer"');
        expect(html).not.toContain('data-file-path');
    });

    it('should render http:// links normally', () => {
        const markdown = 'Visit [Example](http://example.com)';
        const html = parseMarkdown(markdown);
        
        expect(html).toContain('href="http://example.com"');
    });

    it('should render mailto: links normally', () => {
        const markdown = 'Email [support](mailto:support@example.com)';
        const html = parseMarkdown(markdown);
        
        expect(html).toContain('href="mailto:support@example.com"');
    });

    it('should sanitize javascript: links', () => {
        const markdown = 'Click [here](javascript:alert("xss"))';
        const html = parseMarkdown(markdown);
        
        expect(html).not.toContain('javascript:');
        expect(html).toContain('href="#"');
    });

    it('should handle mixed file:// and http:// links', () => {
        const markdown = `Check [local file](file:///path/to/file.txt) or visit [website](https://example.com)`;
        const html = parseMarkdown(markdown);
        
        expect(html).toContain('data-file-path="/path/to/file.txt"');
        expect(html).toContain('href="https://example.com"');
    });
});

// ============================================================================
// Test: parseMarkdown with code blocks
// ============================================================================

describe('parseMarkdown - code blocks', () => {
    it('should render fenced code blocks', () => {
        const markdown = '```javascript\nconst x = 1;\n```';
        const html = parseMarkdown(markdown);
        
        expect(html).toContain('<pre');
        expect(html).toContain('<code>');
        expect(html).toContain('language-javascript');
    });

    it('should escape HTML in code blocks', () => {
        const markdown = '```html\n<script>alert("xss")</script>\n```';
        const html = parseMarkdown(markdown);
        
        expect(html).toContain('&lt;script&gt;');
        expect(html).not.toContain('<script>alert');
    });

    it('should handle code blocks without language', () => {
        const markdown = '```\nplain code\n```';
        const html = parseMarkdown(markdown);
        
        expect(html).toContain('language-text');
    });
});

// ============================================================================
// Test: parseMarkdown with images
// ============================================================================

describe('parseMarkdown - images', () => {
    it('should render https images', () => {
        const markdown = '![alt text](https://example.com/image.png)';
        const html = parseMarkdown(markdown);
        
        expect(html).toContain('<img');
        expect(html).toContain('src="https://example.com/image.png"');
        expect(html).toContain('alt="alt text"');
    });

    it('should block file:// images for security', () => {
        const markdown = '![image](file:///path/to/image.png)';
        const html = parseMarkdown(markdown);
        
        // Should not render file:// images
        expect(html).not.toContain('src="file://');
    });
});

// ============================================================================
// Test: parseMarkdown with tables
// ============================================================================

describe('parseMarkdown - tables', () => {
    it('should render tables with wrapper', () => {
        const markdown = `| Name | Age |
| --- | --- |
| Alice | 30 |
| Bob | 25 |`;
        const html = parseMarkdown(markdown);
        
        expect(html).toContain('markdown-table-wrapper');
        expect(html).toContain('markdown-table');
        expect(html).toContain('<thead>');
        expect(html).toContain('<tbody>');
        expect(html).toContain('Alice');
        expect(html).toContain('Bob');
    });

    it('should handle table alignment', () => {
        const markdown = `| Left | Center | Right |
| :--- | :---: | ---: |
| L | C | R |`;
        const html = parseMarkdown(markdown);
        
        expect(html).toContain('text-align:left');
        expect(html).toContain('text-align:center');
        expect(html).toContain('text-align:right');
    });
});

// ============================================================================
// Test: parseMarkdown edge cases
// ============================================================================

describe('parseMarkdown - edge cases', () => {
    it('should handle empty input', () => {
        expect(parseMarkdown('')).toBe('');
        expect(parseMarkdown(null)).toBe('');
        expect(parseMarkdown(undefined)).toBe('');
    });

    it('should handle non-string input', () => {
        expect(parseMarkdown(123)).toBe('');
        expect(parseMarkdown({})).toBe('');
    });

    it('should wrap content in markdown-content div', () => {
        const markdown = 'Hello world';
        const html = parseMarkdown(markdown);
        
        expect(html).toContain('class="markdown-content"');
    });

    it('should handle complex mixed content', () => {
        const markdown = `# Report

Here is the [report file](file:///reports/q1.pdf) with the following summary:

| Metric | Value |
| --- | --- |
| Revenue | $1M |

\`\`\`python
print("Analysis complete")
\`\`\`

Visit [our site](https://example.com) for more info.`;

        const html = parseMarkdown(markdown);
        
        // Should have all components
        expect(html).toContain('<h1');
        expect(html).toContain('data-file-path="/reports/q1.pdf"');
        expect(html).toContain('markdown-table');
        expect(html).toContain('language-python');
        expect(html).toContain('href="https://example.com"');
    });
});

// ============================================================================
// Test: containsMarkdown detection
// ============================================================================

describe('containsMarkdown', () => {
    it('should detect file:// links as markdown', () => {
        expect(containsMarkdown('[file](file:///path/to/file.txt)')).toBe(true);
    });

    it('should detect regular links as markdown', () => {
        expect(containsMarkdown('[link](https://example.com)')).toBe(true);
    });

    it('should detect headers', () => {
        expect(containsMarkdown('# Header')).toBe(true);
        expect(containsMarkdown('## Sub Header')).toBe(true);
    });

    it('should detect code blocks', () => {
        expect(containsMarkdown('```code```')).toBe(true);
        expect(containsMarkdown('`inline`')).toBe(true);
    });

    it('should detect bold and italic', () => {
        expect(containsMarkdown('**bold**')).toBe(true);
        expect(containsMarkdown('*italic*')).toBe(true);
    });

    it('should return false for plain text', () => {
        expect(containsMarkdown('Hello world')).toBe(false);
        expect(containsMarkdown('Just some text')).toBe(false);
    });
});

// ============================================================================
// Test: XSS prevention
// ============================================================================

describe('parseMarkdown - XSS prevention', () => {
    it('should remove script tags', () => {
        const markdown = 'Hello <script>alert("xss")</script> world';
        const html = parseMarkdown(markdown);
        
        expect(html).not.toContain('<script>');
    });

    it('should handle markdown with embedded HTML safely', () => {
        const markdown = `# Header

<div onmouseover="alert('xss')">Content</div>

Some **bold** text.`;
        
        const html = parseMarkdown(markdown);
        
        expect(html).not.toContain('onmouseover');
        expect(html).toContain('<strong>');
    });
});

// ============================================================================
// Test: Real-world klawed AI responses
// ============================================================================

describe('parseMarkdown - Real klawed AI responses', () => {
    it('should handle typical file creation response', () => {
        const markdown = `I've created the report: [quarterly_report.pdf](file:///reports/quarterly_report.pdf)

Your analysis is ready: [analysis.csv](file:///analysis.csv)

The code has been saved to [main.py](file:///code/main.py)`;

        const html = parseMarkdown(markdown);
        
        expect(html).toContain('data-file-path="/reports/quarterly_report.pdf"');
        expect(html).toContain('data-file-path="/analysis.csv"');
        expect(html).toContain('data-file-path="/code/main.py"');
        
        // All should have file-link class
        const fileLinks = html.match(/class="markdown-link file-link"/g);
        expect(fileLinks).not.toBeNull();
        expect(fileLinks.length).toBe(3);
    });

    it('should handle response with code and file link', () => {
        const markdown = `I've written the Python script:

\`\`\`python
def hello():
    print("Hello, world!")
\`\`\`

The file has been saved: [script.py](file:///script.py)`;

        const html = parseMarkdown(markdown);
        
        expect(html).toContain('language-python');
        expect(html).toContain('data-file-path="/script.py"');
        expect(html).toContain('def hello()');
    });

    it('should handle response with markdown formatting and file links', () => {
        const markdown = `## Summary

I've analyzed your data and created the following files:

1. **Report**: [summary.pdf](file:///summary.pdf) - Contains the executive summary
2. **Data**: [processed_data.csv](file:///processed_data.csv) - Cleaned dataset
3. **Charts**: [charts.html](file:///charts.html) - Interactive visualizations

> All files are ready for review.`;

        const html = parseMarkdown(markdown);
        
        expect(html).toContain('<h2');
        expect(html).toContain('<strong>');
        expect(html).toContain('data-file-path="/summary.pdf"');
        expect(html).toContain('data-file-path="/processed_data.csv"');
        expect(html).toContain('data-file-path="/charts.html"');
        expect(html).toContain('<blockquote>');
    });
});
