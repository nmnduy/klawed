package com.filesurf;

import com.filesurf.service.WaitlistService;
import io.quarkus.qute.Location;
import io.quarkus.qute.Template;
import io.quarkus.qute.TemplateInstance;
import io.quarkus.runtime.annotations.RegisterForReflection;
import jakarta.inject.Inject;
import jakarta.ws.rs.Consumes;
import jakarta.ws.rs.GET;
import jakarta.ws.rs.POST;
import jakarta.ws.rs.Path;
import jakarta.ws.rs.Produces;
import jakarta.ws.rs.core.MediaType;
import jakarta.ws.rs.core.Response;

@Path("/waitlist")
public class WaitlistResource {

    @Inject
    WaitlistService waitlistService;

    @Inject
    @Location("waitlist.html")
    Template waitlist;

    @RegisterForReflection
    public static class WaitlistRequest {
        public String email;
        public String name;
        public String useCase;
    }

    @GET
    @Produces(MediaType.TEXT_HTML)
    public TemplateInstance getWaitlistPage() {
        return waitlist.instance();
    }

    @POST
    @Consumes(MediaType.APPLICATION_JSON)
    @Produces(MediaType.APPLICATION_JSON)
    public Response addToWaitlist(WaitlistRequest request) {
        if (request == null || request.email == null || request.email.trim().isEmpty()) {
            return Response.status(Response.Status.BAD_REQUEST)
                    .entity("{\"error\": \"Email is required\"}")
                    .build();
        }
        try {
            var entry = waitlistService.addToWaitlist(request.email, request.name, request.useCase);
            return Response.ok()
                    .entity("{\"success\": true, \"id\": " + entry.getId() + "}")
                    .build();
        } catch (IllegalArgumentException e) {
            return Response.status(Response.Status.BAD_REQUEST)
                    .entity("{\"error\": \"" + e.getMessage() + "\"}")
                    .build();
        } catch (Exception e) {
            return Response.status(Response.Status.INTERNAL_SERVER_ERROR)
                    .entity("{\"error\": \"Failed to add to waitlist\"}")
                    .build();
        }
    }
}
