package com.filesurf.template;

import com.filesurf.util.CssVersionProvider;
import io.quarkus.qute.TemplateGlobal;
import jakarta.enterprise.context.ApplicationScoped;
import jakarta.inject.Inject;

/**
 * Provides global data to all Qute templates.
 * Makes CSS version information available for cache busting.
 */
@TemplateGlobal
@ApplicationScoped
public class GlobalTemplateData {
    
    @Inject
    CssVersionProvider cssVersionProvider;
    
    /**
     * Get the hashed CSS path for use in template link tags.
     * Usage in templates: <link rel="stylesheet" href="{cssPath}">
     */
    public String cssPath() {
        return cssVersionProvider.getCssPath();
    }
    
    /**
     * Get the CSS filename only (without /assets/ prefix).
     * Usage in templates: {cssFilename}
     */
    public String cssFilename() {
        return cssVersionProvider.getCssFilename();
    }
    
    /**
     * Get the CSS content hash.
     * Usage in templates: {cssHash}
     */
    public String cssHash() {
        return cssVersionProvider.getCssHash();
    }
}
