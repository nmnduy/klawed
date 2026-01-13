package com.filesurf;

import com.filesurf.service.WaitlistService;
import jakarta.inject.Inject;
import jakarta.ws.rs.Consumes;
import jakarta.ws.rs.POST;
import jakarta.ws.rs.Path;
import jakarta.ws.rs.Produces;
import jakarta.ws.rs.core.MediaType;
import jakarta.ws.rs.core.Response;

@Path("/waitlist")
public class WaitlistResource {

    @Inject
    WaitlistService waitlistService;

    public static class WaitlistRequest {
        public String email;
        public String name;
        public String useCase;
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
