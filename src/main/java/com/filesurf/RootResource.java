package com.filesurf;

import com.filesurf.util.CssVersionProvider;
import io.quarkus.qute.Location;
import io.quarkus.qute.Template;
import io.quarkus.qute.TemplateInstance;
import jakarta.inject.Inject;
import jakarta.ws.rs.GET;
import jakarta.ws.rs.Path;
import jakarta.ws.rs.Produces;
import jakarta.ws.rs.core.MediaType;
import jakarta.ws.rs.core.Response;

import java.time.Year;
import java.util.logging.Logger;

/**
 * Root endpoint - serves landing page at /
 * Always shows landing page regardless of authentication status
 */
@Path("/")
public class RootResource {

    private static final Logger LOGGER = Logger.getLogger(RootResource.class.getName());

    @Inject
    @Location("index.html")
    Template landing;

    @Inject
    CssVersionProvider cssVersionProvider;

    @GET
    @Produces(MediaType.TEXT_HTML)
    public Response root() {
        // Always show landing page at root, regardless of authentication status
        LOGGER.info("Root accessed, serving landing page");
        return Response.ok(landing
                .data("cssPath", cssVersionProvider.getCssPath())
                .data("now", new CurrentYear()))
                .build();
    }

    public record CurrentYear() {
        public int year() {
            return Year.now().getValue();
        }
    }
}
