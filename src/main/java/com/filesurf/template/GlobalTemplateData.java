package com.filesurf.template;

import com.filesurf.util.CssVersionProvider;
import com.filesurf.util.JsVersionProvider;
import io.quarkus.qute.TemplateGlobal;
import jakarta.enterprise.context.ApplicationScoped;
import jakarta.inject.Inject;

/**
 * Provides global data to all Qute templates.
 * Makes CSS and JS version information available for cache busting.
 *
 * Note: Methods must be static for @TemplateGlobal to work.
 */
@ApplicationScoped
public class GlobalTemplateData {

    private static CssVersionProvider cssVersionProvider;
    private static JsVersionProvider jsVersionProvider;

    @Inject
    public void setCssVersionProvider(CssVersionProvider provider) {
        GlobalTemplateData.cssVersionProvider = provider;
    }

    @Inject
    public void setJsVersionProvider(JsVersionProvider provider) {
        GlobalTemplateData.jsVersionProvider = provider;
    }

    /**
     * Get the hashed CSS path for use in template link tags.
     * Usage in templates: <link rel="stylesheet" href="{cssPath}">
     */
    @TemplateGlobal
    public static String cssPath() {
        return cssVersionProvider != null ? cssVersionProvider.getCssPath() : "/dist/main.css";
    }

    /**
     * Get the CSS filename only (without /dist/ prefix).
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

    /**
     * Get the hashed JS path for a given base name.
     * NOT a TemplateGlobal because it takes parameters.
     * Access via CDI bean injection in templates.
     * Usage in templates: <script src="{inject:jsHelper.path('fileChat')}"></script>
     */
    public String jsPath(String baseName) {
        return jsVersionProvider != null ? jsVersionProvider.getJsPath(baseName) : "/dist/" + baseName + ".js";
    }

    /**
     * Get the JS filename only (without /dist/ prefix).
     * NOT a TemplateGlobal because it takes parameters.
     * Usage in templates: {inject:jsHelper.filename('fileChat')}
     */
    public String jsFilename(String baseName) {
        return jsVersionProvider != null ? jsVersionProvider.getJsFilename(baseName) : baseName + ".js";
    }
}
