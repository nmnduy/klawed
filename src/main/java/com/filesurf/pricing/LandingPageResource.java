package com.filesurf.pricing;

import com.filesurf.util.CssVersionProvider;
import io.quarkus.qute.Template;
import io.quarkus.qute.TemplateInstance;
import jakarta.inject.Inject;
import jakarta.ws.rs.*;
import jakarta.ws.rs.core.MediaType;
import java.time.Year;

@Path("/")
@Produces(MediaType.TEXT_HTML)
public class LandingPageResource {

    @Inject
    Template index;

    @Inject
    Template pricingSuccess;

    @Inject
    Template pricingCancel;

    @Inject
    CssVersionProvider cssVersionProvider;

    /**
     * Landing page (index.html)
     */
    @GET
    public TemplateInstance getLandingPage() {
        return index
                .data("cssPath", cssVersionProvider.getCssPath())
                .data("currentYear", Year.now().getValue());
    }

    /**
     * Pricing success page
     */
    @GET
    @Path("/pricing/success")
    public TemplateInstance pricingSuccess(@QueryParam("session_id") String sessionId) {
        return pricingSuccess
                .data("cssPath", cssVersionProvider.getCssPath())
                .data("sessionId", sessionId);
    }

    /**
     * Pricing cancel page
     */
    @GET
    @Path("/pricing/cancel")
    public TemplateInstance pricingCancel() {
        return pricingCancel
                .data("cssPath", cssVersionProvider.getCssPath());
    }
}
