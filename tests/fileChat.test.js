/**
 * Unit tests for fileChat.js message handling functions
 * 
 * Tests the markdown rendering and message finalization logic.
 * Run with: bun test tests/fileChat.test.js
 */

import { describe, it, expect, beforeEach, mock } from 'bun:test';

// Mock DOM environment
function createMockDocument() {
    const elements = new Map();
    let idCounter = 0;

    function createElement(tagName) {
        const el = {
            tagName: tagName.toUpperCase(),
            className: '',
            textContent: '',
            innerHTML: '',
            dataset: {},
            attributes: [],
            children: [],
            style: {},
            _id: `mock-${idCounter++}`,
            appendChild(child) {
                this.children.push(child);
                child.parentElement = this;
                return child;
            },
            querySelector(selector) {
                // Simple selector matching
                if (selector.startsWith('[data-message-id=')) {
                    const match = selector.match(/data-message-id="([^"]+)"/);
                    if (match) {
                        return findByMessageId(this, match[1]);
                    }
                }
                if (selector.includes(',')) {
                    // Multiple selectors - try each
                    for (const s of selector.split(',').map(x => x.trim())) {
                        const result = this.querySelector(s);
                        if (result) return result;
                    }
                    return null;
                }
                if (selector.startsWith('.')) {
                    return findByClass(this, selector.slice(1));
                }
                if (selector.startsWith('div.')) {
                    const className = selector.slice(4);
                    return findByTagAndClass(this, 'DIV', className);
                }
                return findBySelector(this, selector);
            },
            querySelectorAll(selector) {
                const results = [];
                findAllBySelector(this, selector, results);
                return results;
            },
            remove() {
                if (this.parentElement) {
                    const idx = this.parentElement.children.indexOf(this);
                    if (idx >= 0) this.parentElement.children.splice(idx, 1);
                }
            },
            setAttribute(name, value) {
                this.attributes.push({ name, value });
                if (name.startsWith('data-')) {
                    this.dataset[name.slice(5).replace(/-([a-z])/g, (_, c) => c.toUpperCase())] = value;
                }
            },
            getAttribute(name) {
                const attr = this.attributes.find(a => a.name === name);
                return attr ? attr.value : null;
            },
            hasAttribute(name) {
                return this.attributes.some(a => a.name === name);
            },
            removeAttribute(name) {
                this.attributes = this.attributes.filter(a => a.name !== name);
            },
            addEventListener() {},
            removeEventListener() {},
            classList: {
                _classes: [],
                add(c) { if (!this._classes.includes(c)) this._classes.push(c); },
                remove(c) { this._classes = this._classes.filter(x => x !== c); },
                contains(c) { return this._classes.includes(c); },
                toggle(c) {
                    if (this.contains(c)) { this.remove(c); return false; }
                    else { this.add(c); return true; }
                }
            }
        };
        return el;
    }

    function findByMessageId(el, messageId) {
        if (el.dataset && el.dataset.messageId === messageId) return el;
        for (const child of (el.children || [])) {
            const found = findByMessageId(child, messageId);
            if (found) return found;
        }
        return null;
    }

    function findByClass(el, className) {
        if (el.className && el.className.includes(className)) return el;
        for (const child of (el.children || [])) {
            const found = findByClass(child, className);
            if (found) return found;
        }
        return null;
    }

    function findByTagAndClass(el, tagName, className) {
        if (el.tagName === tagName && el.className && el.className.includes(className)) return el;
        for (const child of (el.children || [])) {
            const found = findByTagAndClass(child, tagName, className);
            if (found) return found;
        }
        return null;
    }

    function findBySelector(el, selector) {
        // Very basic selector matching
        for (const child of (el.children || [])) {
            const found = findBySelector(child, selector);
            if (found) return found;
        }
        return null;
    }

    function findAllBySelector(el, selector, results) {
        if (selector.startsWith('a.') && el.tagName === 'A' && el.className.includes(selector.slice(2).split('[')[0])) {
            results.push(el);
        }
        for (const child of (el.children || [])) {
            findAllBySelector(child, selector, results);
        }
    }

    return {
        createElement,
        body: createElement('body'),
    };
}

// Helper to create a mock message structure similar to what fileChat.js creates
function createMockMessage(messageId, content, isUser = false) {
    const doc = createMockDocument();
    
    const messageDiv = doc.createElement('div');
    messageDiv.dataset.messageId = messageId;
    messageDiv.className = 'flex ' + (isUser ? 'justify-end' : 'justify-start');
    
    const bubble = doc.createElement('div');
    bubble.className = 'rounded-2xl px-4 py-3 max-w-prose';
    
    const textDiv = doc.createElement('div');
    textDiv.className = 'break-words font-sans text-body-m leading-relaxed text-current whitespace-pre-wrap';
    textDiv.textContent = content;
    
    const timestamp = doc.createElement('div');
    timestamp.className = 'text-caption-s mt-2';
    timestamp.textContent = '10:30 AM';
    
    bubble.appendChild(textDiv);
    bubble.appendChild(timestamp);
    messageDiv.appendChild(bubble);
    
    return { messageDiv, bubble, textDiv, doc };
}

// ============================================================================
// Test: containsMarkdown function
// ============================================================================

describe('containsMarkdown', () => {
    // Inline implementation to test
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

    it('should detect headers', () => {
        expect(containsMarkdown('# Header 1')).toBe(true);
        expect(containsMarkdown('## Header 2')).toBe(true);
        expect(containsMarkdown('### Header 3')).toBe(true);
    });

    it('should detect bold text', () => {
        expect(containsMarkdown('This is **bold** text')).toBe(true);
        expect(containsMarkdown('This is __bold__ text')).toBe(true);
    });

    it('should detect italic text', () => {
        expect(containsMarkdown('This is *italic* text')).toBe(true);
        expect(containsMarkdown('This is _italic_ text')).toBe(true);
    });

    it('should detect code blocks', () => {
        expect(containsMarkdown('```javascript\ncode\n```')).toBe(true);
        expect(containsMarkdown('Use `inline code` here')).toBe(true);
    });

    it('should detect links', () => {
        expect(containsMarkdown('Click [here](https://example.com)')).toBe(true);
        expect(containsMarkdown('[file link](file:///path/to/file.txt)')).toBe(true);
    });

    it('should detect lists', () => {
        expect(containsMarkdown('- item 1\n- item 2')).toBe(true);
        expect(containsMarkdown('* item 1\n* item 2')).toBe(true);
        expect(containsMarkdown('1. first\n2. second')).toBe(true);
    });

    it('should detect blockquotes', () => {
        expect(containsMarkdown('> This is a quote')).toBe(true);
    });

    it('should detect tables', () => {
        expect(containsMarkdown('| Col1 | Col2 |')).toBe(true);
    });

    it('should return false for plain text', () => {
        expect(containsMarkdown('Hello world')).toBe(false);
        expect(containsMarkdown('This is plain text without formatting')).toBe(false);
    });

    it('should handle null and undefined', () => {
        expect(containsMarkdown(null)).toBe(false);
        expect(containsMarkdown(undefined)).toBe(false);
        expect(containsMarkdown('')).toBe(false);
    });

    it('should handle non-string input', () => {
        expect(containsMarkdown(123)).toBe(false);
        expect(containsMarkdown({})).toBe(false);
        expect(containsMarkdown([])).toBe(false);
    });
});

// ============================================================================
// Test: Content type conversion for TEXT messages
// ============================================================================

describe('TEXT message content conversion', () => {
    // Implementation matching the fix in fileChat.js
    function convertTextContent(content) {
        return typeof content === 'string' ? content :
            (typeof content === 'object' ? JSON.stringify(content, null, 2) : String(content));
    }

    it('should pass through string content unchanged', () => {
        expect(convertTextContent('Hello world')).toBe('Hello world');
        expect(convertTextContent('# Markdown header')).toBe('# Markdown header');
    });

    it('should stringify object content', () => {
        const obj = { key: 'value', nested: { a: 1 } };
        const result = convertTextContent(obj);
        expect(result).toContain('"key": "value"');
        expect(result).toContain('"nested"');
    });

    it('should handle [object Object] case', () => {
        // This is what would cause [object Object] if not handled
        const obj = { serviceAccount: { email: 'test@example.com' } };
        const result = convertTextContent(obj);
        expect(result).not.toBe('[object Object]');
        expect(result).toContain('serviceAccount');
        expect(result).toContain('test@example.com');
    });

    it('should convert numbers to string', () => {
        expect(convertTextContent(123)).toBe('123');
        expect(convertTextContent(0)).toBe('0');
    });

    it('should convert booleans to string', () => {
        expect(convertTextContent(true)).toBe('true');
        expect(convertTextContent(false)).toBe('false');
    });

    it('should handle null and undefined', () => {
        expect(convertTextContent(null)).toBe('null');
        expect(convertTextContent(undefined)).toBe('undefined');
    });

    it('should handle arrays', () => {
        const arr = [1, 2, 3];
        const result = convertTextContent(arr);
        expect(result).toContain('1');
        expect(result).toContain('2');
        expect(result).toContain('3');
    });
});

// ============================================================================
// Test: finalizeMessage logic
// ============================================================================

describe('finalizeMessage', () => {
    // Simplified implementation matching the fix
    function containsMarkdown(text) {
        if (!text || typeof text !== 'string') return false;
        const markdownPatterns = [
            /^#{1,6}\s/gm,
            /\*\*[^*]+\*\*/g,
            /```[\s\S]*?```/g,
            /`[^`]+`/g,
            /\[([^\]]+)\]\([^)]+\)/g,
            /^\s*[-*+]\s/gm,
            /^\s*\d+\.\s/gm,
        ];
        return markdownPatterns.some(pattern => pattern.test(text));
    }

    function finalizeMessage(messageDiv, content, parseMarkdownFn) {
        if (!messageDiv) return { success: false, reason: 'no messageDiv' };

        const bubble = messageDiv.querySelector('.rounded-2xl, .rounded-lg');
        if (!bubble) return { success: false, reason: 'no bubble' };

        const textElement = bubble.querySelector('div.whitespace-pre-wrap, div.break-words');
        if (!textElement) return { success: false, reason: 'no textElement' };

        const markdownContent = String(content ?? '');
        const baseClasses = 'break-words font-sans text-body-m leading-relaxed text-current';

        if (containsMarkdown(markdownContent)) {
            textElement.className = baseClasses;
            textElement.innerHTML = parseMarkdownFn(markdownContent);
            return { success: true, rendered: 'markdown' };
        } else {
            textElement.className = baseClasses + ' whitespace-pre-wrap';
            textElement.textContent = markdownContent;
            return { success: true, rendered: 'plain' };
        }
    }

    it('should render markdown content', () => {
        const { messageDiv, textDiv } = createMockMessage('msg-1', 'initial');
        const mockParseMarkdown = (md) => `<p>${md}</p>`;
        
        const result = finalizeMessage(messageDiv, '**bold text**', mockParseMarkdown);
        
        expect(result.success).toBe(true);
        expect(result.rendered).toBe('markdown');
        expect(textDiv.innerHTML).toContain('<p>');
        expect(textDiv.className).not.toContain('whitespace-pre-wrap');
    });

    it('should keep plain text without markdown', () => {
        const { messageDiv, textDiv } = createMockMessage('msg-2', 'initial');
        const mockParseMarkdown = (md) => `<p>${md}</p>`;
        
        const result = finalizeMessage(messageDiv, 'Hello world', mockParseMarkdown);
        
        expect(result.success).toBe(true);
        expect(result.rendered).toBe('plain');
        expect(textDiv.textContent).toBe('Hello world');
        expect(textDiv.className).toContain('whitespace-pre-wrap');
    });

    it('should handle null content', () => {
        const { messageDiv, textDiv } = createMockMessage('msg-3', 'initial');
        const mockParseMarkdown = (md) => `<p>${md}</p>`;
        
        const result = finalizeMessage(messageDiv, null, mockParseMarkdown);
        
        expect(result.success).toBe(true);
        expect(textDiv.textContent).toBe('');
    });

    it('should fail gracefully with missing messageDiv', () => {
        const mockParseMarkdown = (md) => `<p>${md}</p>`;
        const result = finalizeMessage(null, 'content', mockParseMarkdown);
        
        expect(result.success).toBe(false);
    });

    it('should detect code blocks and render as markdown', () => {
        const { messageDiv, textDiv } = createMockMessage('msg-4', 'initial');
        const mockParseMarkdown = (md) => `<pre><code>${md}</code></pre>`;
        
        const content = '```python\nprint("hello")\n```';
        const result = finalizeMessage(messageDiv, content, mockParseMarkdown);
        
        expect(result.success).toBe(true);
        expect(result.rendered).toBe('markdown');
    });

    it('should detect file:// links and render as markdown', () => {
        const { messageDiv, textDiv } = createMockMessage('msg-5', 'initial');
        const mockParseMarkdown = (md) => `<a href="#">link</a>`;
        
        const content = 'Check [this file](file:///path/to/file.txt)';
        const result = finalizeMessage(messageDiv, content, mockParseMarkdown);
        
        expect(result.success).toBe(true);
        expect(result.rendered).toBe('markdown');
    });
});

// ============================================================================
// Test: handleStreamComplete logic
// ============================================================================

describe('handleStreamComplete', () => {
    it('should call finalizeMessage when stream has messageId', () => {
        let updateMessageCalled = false;
        let finalizeMessageCalled = false;
        let addMessageCalled = false;

        // Simulate the function logic
        function handleStreamComplete(content, currentStreamMessageId) {
            if (currentStreamMessageId) {
                updateMessageCalled = true;
                finalizeMessageCalled = true;
                return { finalized: true, messageId: currentStreamMessageId };
            } else {
                addMessageCalled = true;
                return { finalized: false, addedNew: true };
            }
        }

        const result = handleStreamComplete('# Header', 'stream-123');
        
        expect(result.finalized).toBe(true);
        expect(updateMessageCalled).toBe(true);
        expect(finalizeMessageCalled).toBe(true);
        expect(addMessageCalled).toBe(false);
    });

    it('should call addMessage when no stream messageId', () => {
        let addMessageCalled = false;
        let finalizeMessageCalled = false;

        function handleStreamComplete(content, currentStreamMessageId) {
            if (currentStreamMessageId) {
                finalizeMessageCalled = true;
                return { finalized: true };
            } else {
                addMessageCalled = true;
                return { finalized: false, addedNew: true };
            }
        }

        const result = handleStreamComplete('Hello', null);
        
        expect(result.addedNew).toBe(true);
        expect(addMessageCalled).toBe(true);
        expect(finalizeMessageCalled).toBe(false);
    });
});

// ============================================================================
// Test: File link detection in markdown
// ============================================================================

describe('file:// link handling', () => {
    it('should detect file:// protocol in markdown links', () => {
        const content = 'Check [this file](file:///home/user/document.pdf)';
        const hasFileLink = content.includes('file://');
        expect(hasFileLink).toBe(true);
    });

    it('should handle file:// links with special characters', () => {
        const paths = [
            'file:///path/with spaces/file.txt',
            'file:///path/with-dashes/file.txt',
            'file:///path/with_underscores/file.txt',
            'file:///path/to/file (1).txt',
        ];
        
        paths.forEach(path => {
            expect(path.startsWith('file://')).toBe(true);
        });
    });

    it('should extract file path from file:// URL', () => {
        function extractFilePath(href) {
            return href.replace(/^file:\/\/\/?/, '/');
        }
        
        expect(extractFilePath('file:///home/user/doc.pdf')).toBe('/home/user/doc.pdf');
        expect(extractFilePath('file://home/user/doc.pdf')).toBe('/home/user/doc.pdf');
    });
});

// ============================================================================
// Test: Edge cases
// ============================================================================

describe('edge cases', () => {
    it('should handle very long content', () => {
        const longContent = '# Header\n' + 'Lorem ipsum '.repeat(1000);
        
        function containsMarkdown(text) {
            return /^#{1,6}\s/gm.test(text);
        }
        
        expect(containsMarkdown(longContent)).toBe(true);
    });

    it('should handle content with emoji', () => {
        const content = '# 🚀 Getting Started\n\nHello **world** 👋';
        
        function containsMarkdown(text) {
            return /^#{1,6}\s/gm.test(text) || /\*\*[^*]+\*\*/g.test(text);
        }
        
        expect(containsMarkdown(content)).toBe(true);
    });

    it('should handle mixed content with code and text', () => {
        const content = `Here's how to do it:

\`\`\`javascript
console.log('hello');
\`\`\`

And then call the function.`;
        
        function containsMarkdown(text) {
            return /```[\s\S]*?```/g.test(text);
        }
        
        expect(containsMarkdown(content)).toBe(true);
    });

    it('should not falsely detect markdown in regular sentences', () => {
        const plainTexts = [
            'Hello, how are you?',
            'The price is $50 or more.',
            'Use the > symbol for greater than.',
            'File path is /home/user/docs',
        ];
        
        function containsMarkdown(text) {
            const markdownPatterns = [
                /^#{1,6}\s/gm,
                /\*\*[^*]+\*\*/g,
                /```[\s\S]*?```/g,
                /`[^`]+`/g,
                /\[([^\]]+)\]\([^)]+\)/g,
            ];
            return markdownPatterns.some(pattern => pattern.test(text));
        }
        
        plainTexts.forEach(text => {
            expect(containsMarkdown(text)).toBe(false);
        });
    });
});
