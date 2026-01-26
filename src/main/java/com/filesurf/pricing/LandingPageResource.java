package com.filesurf.pricing;

import com.filesurf.stripe.StripeService;
import io.quarkus.qute.Template;
import io.quarkus.qute.TemplateInstance;
import jakarta.inject.Inject;
import jakarta.ws.rs.*;
import jakarta.ws.rs.core.MediaType;
import org.eclipse.microprofile.config.inject.ConfigProperty;

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
    StripeService stripeService;

    @ConfigProperty(name = "css.path", defaultValue = "main.css")
    String cssPath;

    /**
     * Landing page (index.html)
     */
    @GET
    public TemplateInstance getLandingPage() {
        return index
                .data("cssPath", cssPath);
    }

    /**
     * Pricing success page
     */
    @GET
    @Path("/pricing/success")
    public TemplateInstance pricingSuccess(@QueryParam("session_id") String sessionId) {
        return pricingSuccess
                .data("cssPath", cssPath)
                .data("sessionId", sessionId);
    }

    /**
     * Pricing cancel page
     */
    @GET
    @Path("/pricing/cancel")
    public TemplateInstance pricingCancel() {
        return pricingCancel
                .data("cssPath", cssPath);
    }
}
