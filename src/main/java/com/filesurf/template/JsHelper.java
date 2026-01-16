package com.filesurf.template;

import com.filesurf.util.JsVersionProvider;
import io.quarkus.qute.TemplateData;
import jakarta.enterprise.context.ApplicationScoped;
import jakarta.inject.Inject;
import jakarta.inject.Named;

/**
 * Helper bean for JS file paths with cache busting.
 * Made available to templates via @Named("jsHelper").
 *
 * Usage in templates:
 *   <script src="{inject:jsHelper.path('fileChat')}"></script>
 */
@Named("jsHelper")
@ApplicationScoped
@TemplateData
public class JsHelper {

    @Inject
    JsVersionProvider jsVersionProvider;

    /**
     * Get the hashed JS path for a given base name.
     * @param baseName The base name of the JS file (e.g., "fileChat")
     * @return Full path with hash (e.g., "/dist/fileChat.abc123.js")
     */
    public String path(String baseName) {
        return jsVersionProvider != null ? jsVersionProvider.getJsPath(baseName) : "/dist/" + baseName + ".js";
    }

    /**
     * Get the JS filename only (without /js/ prefix).
     * @param baseName The base name of the JS file (e.g., "fileChat")
     * @return Filename with hash (e.g., "fileChat.abc123.js")
     */
    public String filename(String baseName) {
        return jsVersionProvider != null ? jsVersionProvider.getJsFilename(baseName) : baseName + ".js";
    }
}
