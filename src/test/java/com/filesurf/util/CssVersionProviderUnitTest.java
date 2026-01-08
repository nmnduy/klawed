package com.filesurf.util;

import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.*;

/**
 * Unit test for CSS cache busting functionality (non-Quarkus).
 */
public class CssVersionProviderUnitTest {
    
    @Test
    public void testCssVersionProviderInitialization() {
        CssVersionProvider provider = new CssVersionProvider();
        
        assertNotNull(provider.getCssFilename());
        assertNotNull(provider.getCssPath());
        assertNotNull(provider.getCssHash());
    }
    
    @Test
    public void testCssFilenameFormat() {
        CssVersionProvider provider = new CssVersionProvider();
        String filename = provider.getCssFilename();
        
        assertTrue(filename.startsWith("main."), "CSS filename should start with 'main.'");
        assertTrue(filename.endsWith(".css"), "CSS filename should end with '.css'");
    }
    
    @Test
    public void testCssPathFormat() {
        CssVersionProvider provider = new CssVersionProvider();
        String path = provider.getCssPath();
        
        assertTrue(path.startsWith("/assets/"), "CSS path should start with '/assets/'");
        assertTrue(path.endsWith(".css"), "CSS path should end with '.css'");
        assertTrue(path.contains("main."), "CSS path should contain 'main.'");
    }
    
    @Test
    public void testHashedFilenameFormat() {
        CssVersionProvider provider = new CssVersionProvider();
        String filename = provider.getCssFilename();
        
        // If css-version.properties exists and has a hash, verify format
        if (!filename.equals("main.css")) {
            assertTrue(filename.matches("main\\.[a-f0-9]{8}\\.css"), 
                    "Hashed filename should match pattern 'main.[8-hex-chars].css', got: " + filename);
        } else {
            // If using default, that's fine too (dev mode or version file missing)
            assertEquals("main.css", filename);
        }
    }
    
    @Test
    public void testCssPathMatchesFilename() {
        CssVersionProvider provider = new CssVersionProvider();
        String path = provider.getCssPath();
        String filename = provider.getCssFilename();
        
        assertEquals("/assets/" + filename, path, "CSS path should be '/assets/' + filename");
    }
}
