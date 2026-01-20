/**
 * Markdown Debug Utility
 * Test markdown parsing to identify spacing issues
 */

import { parseMarkdown, containsMarkdown } from './markdownUtils.js';

/**
 * Create a debug interface for testing markdown parsing
 */
export function createMarkdownDebugger() {
    const container = document.createElement('div');
    container.className = 'markdown-debug-container p-6 bg-slate-50 dark:bg-slate-900 rounded-lg border border-slate-200 dark:border-slate-700';
    
    container.innerHTML = `
        <h2 class="text-xl font-bold mb-4">Markdown Parsing Debugger</h2>
        
        <div class="grid grid-cols-1 md:grid-cols-2 gap-6">
            <div>
                <h3 class="font-semibold mb-2">Input Markdown</h3>
                <textarea id="markdown-input" class="w-full h-64 p-3 border rounded font-mono text-sm" placeholder="Enter markdown to test..."></textarea>
                <div class="mt-2 flex gap-2">
                    <button id="test-parsing" class="px-4 py-2 bg-blue-500 text-white rounded hover:bg-blue-600">Test Parsing</button>
                    <button id="load-examples" class="px-4 py-2 bg-slate-200 dark:bg-slate-700 rounded hover:bg-slate-300 dark:hover:bg-slate-600">Load Examples</button>
                </div>
            </div>
            
            <div>
                <h3 class="font-semibold mb-2">Parsed Output</h3>
                <div id="parsed-output" class="h-64 p-3 border rounded bg-white dark:bg-slate-800 overflow-auto markdown-content"></div>
                <div class="mt-2 text-sm text-slate-500">
                    <div>Contains markdown: <span id="contains-markdown">false</span></div>
                    <div>Raw HTML: <button id="show-html" class="text-blue-500 hover:underline">Show</button></div>
                </div>
            </div>
        </div>
        
        <div class="mt-6">
            <h3 class="font-semibold mb-2">Debug Information</h3>
            <div id="debug-info" class="p-3 bg-slate-100 dark:bg-slate-800 rounded text-sm font-mono overflow-auto max-h-64"></div>
        </div>
    `;
    
    // Set up event listeners
    const inputTextarea = container.querySelector('#markdown-input');
    const testButton = container.querySelector('#test-parsing');
    const loadExamplesButton = container.querySelector('#load-examples');
    const parsedOutput = container.querySelector('#parsed-output');
    const containsMarkdownSpan = container.querySelector('#contains-markdown');
    const showHtmlButton = container.querySelector('#show-html');
    const debugInfo = container.querySelector('#debug-info');
    
    // Example markdown texts
    const examples = [
        {
            name: "Simple text with line breaks",
            content: "This is line 1.\nThis is line 2.\nThis is line 3."
        },
        {
            name: "Text with double line breaks",
            content: "Paragraph 1.\n\nParagraph 2.\n\nParagraph 3."
        },
        {
            name: "Markdown with headers",
            content: "# Header 1\n## Header 2\n### Header 3\n\nSome text under headers."
        },
        {
            name: "Markdown with lists",
            content: "- Item 1\n- Item 2\n- Item 3\n\n1. First\n2. Second\n3. Third"
        },
        {
            name: "Markdown with code blocks",
            content: "```javascript\nfunction test() {\n    console.log('Hello');\n}\n```\n\nInline `code` example."
        },
        {
            name: "Mixed markdown",
            content: "# Title\n\nThis is a **bold** text and *italic* text.\n\n- List item 1\n- List item 2\n\n> Blockquote example\n\n`code snippet`"
        }
    ];
    
    function updateDebugInfo(markdown, html) {
        const lines = markdown.split('\n');
        const lineBreaks = markdown.match(/\n/g)?.length || 0;
        const doubleLineBreaks = markdown.match(/\n\n/g)?.length || 0;
        
        debugInfo.innerHTML = `
            Input length: ${markdown.length} characters<br>
            Lines: ${lines.length}<br>
            Line breaks: ${lineBreaks}<br>
            Double line breaks: ${doubleLineBreaks}<br>
            HTML length: ${html.length} characters<br>
            <br>
            Line analysis:<br>
            ${lines.map((line, i) => `Line ${i+1}: "${line.replace(/\n/g, '\\n')}" (${line.length} chars)`).join('<br>')}
        `;
    }
    
    function testParsing() {
        const markdown = inputTextarea.value;
        const containsMd = containsMarkdown(markdown);
        const html = parseMarkdown(markdown);
        
        containsMarkdownSpan.textContent = containsMd;
        parsedOutput.innerHTML = html;
        
        updateDebugInfo(markdown, html);
    }
    
    function loadExamples() {
        const exampleList = examples.map((ex, i) => 
            `<option value="${i}">${ex.name}</option>`
        ).join('');
        
        const selectHtml = `
            <div class="mb-2">
                <label class="block text-sm mb-1">Select example:</label>
                <select id="example-select" class="w-full p-2 border rounded">
                    <option value="">Choose an example...</option>
                    ${exampleList}
                </select>
            </div>
        `;
        
        const modal = document.createElement('div');
        modal.className = 'fixed inset-0 bg-black/50 flex items-center justify-center z-50';
        modal.innerHTML = `
            <div class="bg-white dark:bg-slate-800 rounded-lg p-6 max-w-md w-full">
                <h3 class="text-lg font-bold mb-4">Load Example</h3>
                ${selectHtml}
                <div class="flex justify-end gap-2 mt-4">
                    <button id="cancel-example" class="px-4 py-2 bg-slate-200 dark:bg-slate-700 rounded hover:bg-slate-300 dark:hover:bg-slate-600">Cancel</button>
                    <button id="load-example" class="px-4 py-2 bg-blue-500 text-white rounded hover:bg-blue-600">Load</button>
                </div>
            </div>
        `;
        
        document.body.appendChild(modal);
        
        const select = modal.querySelector('#example-select');
        const cancelBtn = modal.querySelector('#cancel-example');
        const loadBtn = modal.querySelector('#load-example');
        
        cancelBtn.addEventListener('click', () => modal.remove());
        loadBtn.addEventListener('click', () => {
            const selectedIndex = parseInt(select.value);
            if (!isNaN(selectedIndex) && examples[selectedIndex]) {
                inputTextarea.value = examples[selectedIndex].content;
                modal.remove();
                testParsing();
            }
        });
    }
    
    function showHtml() {
        const html = parsedOutput.innerHTML;
        const modal = document.createElement('div');
        modal.className = 'fixed inset-0 bg-black/50 flex items-center justify-center z-50';
        modal.innerHTML = `
            <div class="bg-white dark:bg-slate-800 rounded-lg p-6 max-w-2xl w-full max-h-[80vh] flex flex-col">
                <h3 class="text-lg font-bold mb-4">Raw HTML Output</h3>
                <pre class="flex-1 overflow-auto bg-slate-100 dark:bg-slate-900 p-4 rounded text-sm font-mono whitespace-pre-wrap">${html.replace(/</g, '&lt;').replace(/>/g, '&gt;')}</pre>
                <div class="flex justify-end mt-4">
                    <button id="close-html" class="px-4 py-2 bg-slate-200 dark:bg-slate-700 rounded hover:bg-slate-300 dark:hover:bg-slate-600">Close</button>
                </div>
            </div>
        `;
        
        document.body.appendChild(modal);
        modal.querySelector('#close-html').addEventListener('click', () => modal.remove());
    }
    
    // Set up event listeners
    testButton.addEventListener('click', testParsing);
    loadExamplesButton.addEventListener('click', loadExamples);
    showHtmlButton.addEventListener('click', showHtml);
    
    // Auto-test when input changes (with debounce)
    let debounceTimer;
    inputTextarea.addEventListener('input', () => {
        clearTimeout(debounceTimer);
        debounceTimer = setTimeout(testParsing, 500);
    });
    
    // Initial test with default content
    inputTextarea.value = examples[0].content;
    testParsing();
    
    return container;
}

/**
 * Initialize markdown debugger
 */
export function initMarkdownDebugger() {
    // Check if we're in debug mode
    const urlParams = new URLSearchParams(window.location.search);
    if (!urlParams.has('markdown-debug')) {
        return;
    }
    
    // Create and append debugger
    const debuggerEl = createMarkdownDebugger();
    document.body.appendChild(debuggerEl);
    
    // Add some styles
    const style = document.createElement('style');
    style.textContent = `
        .markdown-debug-container {
            margin: 20px;
            max-width: 1200px;
            margin-left: auto;
            margin-right: auto;
        }
        
        .markdown-content h1, .markdown-content h2, .markdown-content h3 {
            margin-top: 0.5em;
            margin-bottom: 0.5em;
        }
        
        .markdown-content p {
            margin-bottom: 1em;
        }
        
        .markdown-content pre {
            margin: 1em 0;
            padding: 1em;
            background: #f5f5f5;
            border-radius: 4px;
            overflow: auto;
        }
        
        .markdown-content ul, .markdown-content ol {
            margin: 1em 0;
            padding-left: 2em;
        }
        
        .markdown-content li {
            margin: 0.25em 0;
        }
    `;
    document.head.appendChild(style);
}

// Auto-initialize if this script is loaded
if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', initMarkdownDebugger);
} else {
    initMarkdownDebugger();
}