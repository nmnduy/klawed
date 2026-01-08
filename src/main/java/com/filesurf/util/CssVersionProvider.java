package com.filesurf.util;

import jakarta.enterprise.context.ApplicationScoped;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.io.IOException;
import java.io.InputStream;
import java.util.Properties;

/**
 * Provides the hashed CSS filename for cache busting.
 * Reads from css-version.properties file generated during build.
 */
@ApplicationScoped
public class CssVersionProvider {
    
    private static final Logger LOG = LoggerFactory.getLogger(CssVersionProvider.class);
    private static final String VERSION_FILE = "css-version.properties";
    private static final String DEFAULT_FILENAME = "main.css";
    
    private final String cssFilename;
    private final String cssHash;
    
    public CssVersionProvider() {
        Properties props = loadProperties();
        this.cssFilename = props.getProperty("css.filename", DEFAULT_FILENAME);
        this.cssHash = props.getProperty("css.hash", "unknown");
        
        LOG.info("CSS Version Provider initialized: filename={}, hash={}", cssFilename, cssHash);
    }
    
    /**
     * Get the hashed CSS filename for use in templates.
     * Returns "main.[hash].css" in production or "main.css" if version file not found.
     */
    public String getCssFilename() {
        return cssFilename;
    }
    
    /**
     * Get the CSS content hash.
     */
    public String getCssHash() {
        return cssHash;
    }
    
    /**
     * Get the full CSS path including /assets/ prefix.
     */
    public String getCssPath() {
        return "/assets/" + cssFilename;
    }
    
    private Properties loadProperties() {
        Properties props = new Properties();
        
        try (InputStream is = getClass().getClassLoader().getResourceAsStream(VERSION_FILE)) {
            if (is != null) {
                props.load(is);
                LOG.debug("Loaded CSS version properties from {}", VERSION_FILE);
            } else {
                LOG.warn("CSS version file not found: {}. Using default CSS filename. " +
                        "Run 'npm run build' to generate hashed CSS files.", VERSION_FILE);
            }
        } catch (IOException e) {
            LOG.error("Error loading CSS version properties from {}: {}", VERSION_FILE, e.getMessage());
        }
        
        return props;
    }
}
