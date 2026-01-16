package com.filesurf.util;

import io.quarkus.test.junit.QuarkusTest;
import jakarta.inject.Inject;
import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.*;

/**
 * Test for CSS cache busting functionality.
 */
@QuarkusTest
public class CssVersionProviderTest {

    @Inject
    CssVersionProvider cssVersionProvider;

    @Test
    public void testCssFilenameNotNull() {
        String filename = cssVersionProvider.getCssFilename();
        assertNotNull(filename);
        assertTrue(filename.startsWith("main."), "CSS filename should start with 'main.'");
        assertTrue(filename.endsWith(".css"), "CSS filename should end with '.css'");
    }

    @Test
    public void testCssPathFormat() {
        String path = cssVersionProvider.getCssPath();
        assertNotNull(path);
        assertTrue(path.startsWith("/assets/"), "CSS path should start with '/assets/'");
        assertTrue(path.endsWith(".css"), "CSS path should end with '.css'");
    }

    @Test
    public void testCssHashNotNull() {
        String hash = cssVersionProvider.getCssHash();
        assertNotNull(hash);
        assertFalse(hash.isEmpty(), "CSS hash should not be empty");
    }

    @Test
    public void testHashedFilenameFormat() {
        String filename = cssVersionProvider.getCssFilename();

        // If css-version.properties exists, filename should be hashed
        // Format: main.[hash].css where hash is 8 hex characters
        if (!filename.equals("main.css")) {
            assertTrue(filename.matches("main\\.[a-f0-9]{8}\\.css"),
                    "Hashed filename should match pattern 'main.[8-hex-chars].css'");
        }
    }
}
