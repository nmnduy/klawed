package com.filesurf.service;

import jakarta.enterprise.context.ApplicationScoped;
import org.commonmark.Extension;
import org.commonmark.ext.autolink.AutolinkExtension;
import org.commonmark.ext.gfm.tables.TablesExtension;
import org.commonmark.node.Node;
import org.commonmark.parser.Parser;
import org.commonmark.renderer.html.HtmlRenderer;

import java.util.List;

/**
 * Service for converting Markdown to HTML.
 * Uses CommonMark library with GitHub Flavored Markdown extensions.
 */
@ApplicationScoped
public class MarkdownService {

    private final Parser parser;
    private final HtmlRenderer renderer;

    public MarkdownService() {
        // Enable GitHub Flavored Markdown extensions
        List<Extension> extensions = List.of(
                TablesExtension.create(),      // Support for tables
                AutolinkExtension.create()     // Auto-link URLs
        );

        this.parser = Parser.builder()
                .extensions(extensions)
                .build();

        this.renderer = HtmlRenderer.builder()
                .extensions(extensions)
                .build();
    }

    /**
     * Convert markdown text to HTML.
     *
     * @param markdown The markdown content
     * @return HTML rendered content
     */
    public String toHtml(String markdown) {
        if (markdown == null || markdown.isEmpty()) {
            return "";
        }

        Node document = parser.parse(markdown);
        return renderer.render(document);
    }

    /**
     * Convert markdown text to plain text (strips all formatting).
     *
     * @param markdown The markdown content
     * @return Plain text content
     */
    public String toPlainText(String markdown) {
        if (markdown == null || markdown.isEmpty()) {
            return "";
        }

        // Simple approach: convert to HTML first, then strip tags
        String html = toHtml(markdown);
        return html.replaceAll("<[^>]*>", "")
                .replaceAll("\\s+", " ")
                .trim();
    }
}
