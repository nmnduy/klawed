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

import java.util.logging.Logger;

@Path("/app")
public class FileChatResource {

    private static final Logger LOGGER = Logger.getLogger(FileChatResource.class.getName());

    @Inject
    @Location("fileChat.html")
    Template fileChat;

    @Inject
    CssVersionProvider cssVersionProvider;

    @GET
    @Produces(MediaType.TEXT_HTML)
    public TemplateInstance get() {
        LOGGER.info("Loading File Chat interface");
        return fileChat.data("cssPath", cssVersionProvider.getCssPath());
    }
}
