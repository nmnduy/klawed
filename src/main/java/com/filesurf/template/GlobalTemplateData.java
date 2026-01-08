package com.filesurf.template;

import com.filesurf.util.CssVersionProvider;
import io.quarkus.qute.TemplateGlobal;
import jakarta.enterprise.context.ApplicationScoped;
import jakarta.inject.Inject;

/**
 * Provides global data to all Qute templates.
 * Makes CSS version information available for cache busting.
 * 
 * Note: Methods must be static for @TemplateGlobal to work.
 */
@ApplicationScoped
public class GlobalTemplateData {
    
    private static CssVersionProvider cssVersionProvider;
    
    @Inject
    public void setCssVersionProvider(CssVersionProvider provider) {
        GlobalTemplateData.cssVersionProvider = provider;
    }
    
    /**
     * Get the hashed CSS path for use in template link tags.
     * Usage in templates: <link rel="stylesheet" href="{cssPath}">
     */
    @TemplateGlobal
    public static String cssPath() {
        return cssVersionProvider != null ? cssVersionProvider.getCssPath() : "/assets/main.css";
    }
    
    /**
     * Get the CSS filename only (without /assets/ prefix).
     * Usage in templates: {cssFilename}
     */
    @TemplateGlobal
    public static String cssFilename() {
        return cssVersionProvider != null ? cssVersionProvider.getCssFilename() : "main.css";
    }
    
    /**
     * Get the CSS content hash.
     * Usage in templates: {cssHash}
     */
    @TemplateGlobal
    public static String cssHash() {
        return cssVersionProvider != null ? cssVersionProvider.getCssHash() : "unknown";
    }
}
