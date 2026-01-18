package com.filesurf;

import com.filesurf.service.ContactFormService;
import jakarta.inject.Inject;
import jakarta.ws.rs.Consumes;
import jakarta.ws.rs.GET;
import jakarta.ws.rs.POST;
import jakarta.ws.rs.Path;
import jakarta.ws.rs.Produces;
import jakarta.ws.rs.core.MediaType;
import jakarta.ws.rs.core.Response;
import io.quarkus.runtime.annotations.RegisterForReflection;

@Path("/http/contact")
public class ContactFormResource {

    @Inject
    ContactFormService contactFormService;

    @RegisterForReflection
    public static class ContactRequest {
        public String email;
        public String company;
        public String message;
    }

    @RegisterForReflection
    public static class ContactResponse {
        public boolean success;
        public String message;
        
        public ContactResponse() {}
        
        public ContactResponse(boolean success, String message) {
            this.success = success;
            this.message = message;
        }
    }

    @POST
    @Consumes(MediaType.APPLICATION_JSON)
    @Produces(MediaType.APPLICATION_JSON)
    public Response submitForm(ContactRequest request) {
        if (request == null || request.email == null || request.email.trim().isEmpty()) {
            return Response.status(Response.Status.BAD_REQUEST)
                    .entity(new ContactResponse(false, "Email is required"))
                    .build();
        }
        try {
            contactFormService.submitForm(request.email, request.company, request.message);
            return Response.ok()
                    .entity(new ContactResponse(true, "Form submitted successfully"))
                    .build();
        } catch (IllegalArgumentException e) {
            return Response.status(Response.Status.BAD_REQUEST)
                    .entity(new ContactResponse(false, e.getMessage()))
                    .build();
        } catch (Exception e) {
            return Response.status(Response.Status.INTERNAL_SERVER_ERROR)
                    .entity(new ContactResponse(false, "Failed to submit form"))
                    .build();
        }
    }

    @GET
    @Produces(MediaType.APPLICATION_JSON)
    public Response getSubmissions() {
        try {
            var submissions = contactFormService.getAllSubmissions();
            var count = contactFormService.getSubmissionCount();
            return Response.ok()
                    .entity(new ContactResponse(true, count + " total submissions"))
                    .build();
        } catch (Exception e) {
            return Response.status(Response.Status.INTERNAL_SERVER_ERROR)
                    .entity(new ContactResponse(false, "Failed to retrieve submissions"))
                    .build();
        }
    }
}
