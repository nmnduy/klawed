package com.filesurf;

import jakarta.ws.rs.GET;
import jakarta.ws.rs.Path;
import jakarta.ws.rs.Produces;
import jakarta.ws.rs.core.MediaType;

@Path("/markdown-debug")
public class MarkdownDebugResource {

    @GET
    @Produces(MediaType.TEXT_HTML)
    public String getMarkdownDebugPage() {
        // Return the HTML directly since we have a static HTML file
        // The template will be used if we need dynamic content
        return "<html><body>Redirecting to static debug page...</body></html>";
    }
    
    @GET
    @Path("/test")
    @Produces(MediaType.TEXT_HTML)
    public String testMarkdownParsing() {
        // Simple endpoint to test markdown parsing
        return """
            <!DOCTYPE html>
            <html>
            <head>
                <title>Markdown Test</title>
                <script type="module">
                    import { parseMarkdown } from '/js/markdownUtils.js';
                    
                    const testCases = [
                        {
                            name: "Simple line breaks",
                            input: "Line 1\\nLine 2\\nLine 3",
                            expected: "Line 1<br>Line 2<br>Line 3"
                        },
                        {
                            name: "Paragraphs",
                            input: "Paragraph 1.\\n\\nParagraph 2.\\n\\nParagraph 3.",
                            expected: "<p>Paragraph 1.</p><p>Paragraph 2.</p><p>Paragraph 3.</p>"
                        }
                    ];
                    
                    function runTests() {
                        const results = testCases.map(test => {
                            const output = parseMarkdown(test.input);
                            return {
                                name: test.name,
                                input: test.input,
                                output: output,
                                matches: output.replace(/\\s+/g, ' ') === test.expected.replace(/\\s+/g, ' ')
                            };
                        });
                        
                        document.getElementById('results').innerHTML = results.map(r => `
                            <div style="border: 1px solid #ccc; padding: 10px; margin: 10px;">
                                <h3>${r.name}</h3>
                                <p><strong>Input:</strong> <pre>${r.input.replace(/\\n/g, '\\\\n')}</pre></p>
                                <p><strong>Output:</strong> <pre>${r.output}</pre></p>
                                <p><strong>Matches expected:</strong> ${r.matches ? '✅' : '❌'}</p>
                            </div>
                        `).join('');
                    }
                    
                    document.addEventListener('DOMContentLoaded', runTests);
                </script>
            </head>
            <body>
                <h1>Markdown Parsing Test</h1>
                <div id="results"></div>
            </body>
            </html>
            """;
    }
}