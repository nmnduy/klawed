package com.filesurf.util;

import jakarta.enterprise.context.ApplicationScoped;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.io.IOException;
import java.io.InputStream;
import java.util.HashMap;
import java.util.Map;
import java.util.Properties;

/**
 * Provides the hashed JS filenames for cache busting.
 * Reads from js-version.properties file generated during build.
 */
@ApplicationScoped
public class JsVersionProvider {
    
    private static final Logger LOG = LoggerFactory.getLogger(JsVersionProvider.class);
    private static final String VERSION_FILE = "js-version.properties";
    
    private final Map<String, String> jsFiles;
    
    public JsVersionProvider() {
        this.jsFiles = loadJsFiles();
        LOG.info("JS Version Provider initialized with {} files", jsFiles.size());
    }
    
    /**
     * Get the hashed JS filename for a given base name.
     * For example: getJsFilename("fileChat") returns "fileChat.[hash].js" in production
     * or "fileChat.js" if version file not found.
     */
    public String getJsFilename(String baseName) {
        return jsFiles.getOrDefault(baseName, baseName + ".js");
    }
    
    /**
     * Get the full JS path including /js/ prefix.
     */
    public String getJsPath(String baseName) {
        return "/js/" + getJsFilename(baseName);
    }
    
    /**
     * Get all JS file mappings (for debugging).
     */
    public Map<String, String> getAllJsFiles() {
        return new HashMap<>(jsFiles);
    }
    
    private Map<String, String> loadJsFiles() {
        Map<String, String> files = new HashMap<>();
        Properties props = new Properties();
        
        try (InputStream is = getClass().getClassLoader().getResourceAsStream(VERSION_FILE)) {
            if (is != null) {
                props.load(is);
                LOG.debug("Loaded JS version properties from {}", VERSION_FILE);
                
                // Extract all js.* properties
                props.stringPropertyNames().stream()
                    .filter(key -> key.startsWith("js.") && !key.equals("js.generated"))
                    .forEach(key -> {
                        String baseName = key.substring(3); // Remove "js." prefix
                        String filename = props.getProperty(key);
                        files.put(baseName, filename);
                    });
                    
                LOG.info("Loaded {} JS file mappings", files.size());
            } else {
                LOG.warn("JS version file not found: {}. Using default JS filenames. " +
                        "Run 'npm run build' to generate hashed JS files.", VERSION_FILE);
            }
        } catch (IOException e) {
            LOG.error("Error loading JS version properties from {}: {}", VERSION_FILE, e.getMessage());
        }
        
        return files;
    }
}
