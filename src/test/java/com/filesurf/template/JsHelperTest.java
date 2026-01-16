package com.filesurf.template;

import com.filesurf.util.JsVersionProvider;
import io.quarkus.test.junit.QuarkusTest;
import jakarta.inject.Inject;
import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.*;

@QuarkusTest
public class JsHelperTest {

    @Inject
    JsHelper jsHelper;

    @Inject
    JsVersionProvider jsVersionProvider;

    @Test
    public void testJsHelperIsInjectable() {
        assertNotNull(jsHelper, "JsHelper should be injectable");
    }

    @Test
    public void testPathMethod() {
        String path = jsHelper.path("fileChat");
        assertNotNull(path, "Path should not be null");
        assertTrue(path.startsWith("/js/"), "Path should start with /js/");
        assertTrue(path.contains("fileChat"), "Path should contain base name");
    }

    @Test
    public void testFilenameMethod() {
        String filename = jsHelper.filename("fileChat");
        assertNotNull(filename, "Filename should not be null");
        assertTrue(filename.startsWith("fileChat"), "Filename should start with base name");
        assertTrue(filename.endsWith(".js"), "Filename should end with .js");
    }

    @Test
    public void testPathMatchesVersionProvider() {
        String baseName = "fileChat";
        String helperPath = jsHelper.path(baseName);
        String providerPath = jsVersionProvider.getJsPath(baseName);

        assertEquals(providerPath, helperPath,
            "JsHelper path should match JsVersionProvider path");
    }

    @Test
    public void testFilenameMatchesVersionProvider() {
        String baseName = "fileExplorer";
        String helperFilename = jsHelper.filename(baseName);
        String providerFilename = jsVersionProvider.getJsFilename(baseName);

        assertEquals(providerFilename, helperFilename,
            "JsHelper filename should match JsVersionProvider filename");
    }
}
