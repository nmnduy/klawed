package com.filesurf;

import io.quarkus.qute.Template;
import io.quarkus.qute.TemplateInstance;
import jakarta.inject.Inject;
import jakarta.ws.rs.GET;
import jakarta.ws.rs.Path;
import jakarta.ws.rs.Produces;
import jakarta.ws.rs.core.MediaType;

import java.time.LocalDate;
import java.time.format.DateTimeFormatter;

/**
 * REST endpoint for privacy policy page.
 */
@Path("/privacy")
public class PrivacyPolicyResource {

    @Inject
    Template privacy;

    /**
     * Get privacy policy page
     */
    @GET
    @Produces(MediaType.TEXT_HTML)
    public TemplateInstance getPrivacyPolicy() {
        // Format current date as "January 8, 2026"
        String lastUpdated = LocalDate.now()
                .format(DateTimeFormatter.ofPattern("MMMM d, yyyy"));
        
        return privacy.data("lastUpdated", lastUpdated);
    }
}
