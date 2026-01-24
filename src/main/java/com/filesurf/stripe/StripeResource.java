package com.filesurf.stripe;

import com.filesurf.auth.AuthService;
import com.stripe.exception.StripeException;
import com.stripe.model.Event;
import com.stripe.model.checkout.Session;
import jakarta.inject.Inject;
import jakarta.ws.rs.*;
import jakarta.ws.rs.core.MediaType;
import jakarta.ws.rs.core.Response;
import org.jboss.logging.Logger;

import java.util.HashMap;
import java.util.Map;

@Path("/api/stripe")
@Produces(MediaType.APPLICATION_JSON)
@Consumes(MediaType.APPLICATION_JSON)
public class StripeResource {

    private static final Logger LOG = Logger.getLogger(StripeResource.class);

    @Inject
    StripeService stripeService;

    @Inject
    AuthService authService;

    @Inject
    SubscriptionService subscriptionService;

    /**
     * Create a Stripe Checkout session
     */
    @POST
    @Path("/create-checkout-session")
    public Response createCheckoutSession(Map<String, String> request, @CookieParam("filesurf_userId") String userId) {
        try {
            if (userId == null) {
                return Response.status(Response.Status.UNAUTHORIZED)
                        .entity(Map.of("error", "Not authenticated"))
                        .build();
            }

            String planCode = request.get("planCode");
            String priceId = request.get("priceId");

            if (planCode == null || priceId == null) {
                return Response.status(Response.Status.BAD_REQUEST)
                        .entity(Map.of("error", "Missing planCode or priceId"))
                        .build();
            }

            Session session = stripeService.createCheckoutSession(userId, planCode, priceId);

            Map<String, String> response = new HashMap<>();
            response.put("sessionId", session.getId());

            return Response.ok(response).build();

        } catch (StripeException e) {
            LOG.error("Stripe error creating checkout session", e);
            return Response.status(Response.Status.INTERNAL_SERVER_ERROR)
                    .entity(Map.of("error", e.getMessage()))
                    .build();
        } catch (Exception e) {
            LOG.error("Error creating checkout session", e);
            return Response.status(Response.Status.INTERNAL_SERVER_ERROR)
                    .entity(Map.of("error", "Internal server error"))
                    .build();
        }
    }

    /**
     * Create a customer portal session
     */
    @POST
    @Path("/create-portal-session")
    public Response createPortalSession(@CookieParam("filesurf_userId") String userId) {
        try {
            if (userId == null) {
                return Response.status(Response.Status.UNAUTHORIZED)
                        .entity(Map.of("error", "Not authenticated"))
                        .build();
            }

            // Get customer ID from subscription
            String customerId = subscriptionService.getStripeCustomerId(userId);
            if (customerId == null) {
                return Response.status(Response.Status.BAD_REQUEST)
                        .entity(Map.of("error", "No active subscription found"))
                        .build();
            }

            com.stripe.model.billingportal.Session session = 
                stripeService.createPortalSession(customerId);

            return Response.ok(Map.of("url", session.getUrl())).build();

        } catch (StripeException e) {
            LOG.error("Stripe error creating portal session", e);
            return Response.status(Response.Status.INTERNAL_SERVER_ERROR)
                    .entity(Map.of("error", e.getMessage()))
                    .build();
        } catch (Exception e) {
            LOG.error("Error creating portal session", e);
            return Response.status(Response.Status.INTERNAL_SERVER_ERROR)
                    .entity(Map.of("error", "Internal server error"))
                    .build();
        }
    }

    /**
     * Webhook endpoint for Stripe events
     */
    @POST
    @Path("/webhook")
    @Consumes(MediaType.TEXT_PLAIN)
    public Response handleWebhook(String payload, @HeaderParam("Stripe-Signature") String sigHeader) {
        try {
            Event event = stripeService.constructEvent(payload, sigHeader);

            LOG.infof("Received Stripe webhook event: %s", event.getType());

            // Handle different event types
            switch (event.getType()) {
                case "checkout.session.completed":
                    handleCheckoutCompleted(event);
                    break;
                case "customer.subscription.created":
                    handleSubscriptionCreated(event);
                    break;
                case "customer.subscription.updated":
                    handleSubscriptionUpdated(event);
                    break;
                case "customer.subscription.deleted":
                    handleSubscriptionDeleted(event);
                    break;
                case "invoice.payment_succeeded":
                    handlePaymentSucceeded(event);
                    break;
                case "invoice.payment_failed":
                    handlePaymentFailed(event);
                    break;
                default:
                    LOG.infof("Unhandled event type: %s", event.getType());
            }

            return Response.ok().build();

        } catch (StripeException e) {
            LOG.error("Error verifying webhook signature", e);
            return Response.status(Response.Status.BAD_REQUEST).build();
        } catch (Exception e) {
            LOG.error("Error handling webhook", e);
            return Response.status(Response.Status.INTERNAL_SERVER_ERROR).build();
        }
    }

    private void handleCheckoutCompleted(Event event) {
        try {
            Session session = (Session) event.getDataObjectDeserializer().getObject().orElse(null);
            if (session != null) {
                String userId = session.getMetadata().get("userId");
                String customerId = session.getCustomer();
                String subscriptionId = session.getSubscription();

                LOG.infof("Checkout completed - userId: %s, customerId: %s, subscriptionId: %s",
                          userId, customerId, subscriptionId);

                // Update subscription in database
                subscriptionService.handleCheckoutCompleted(userId, customerId, subscriptionId);
            }
        } catch (Exception e) {
            LOG.error("Error handling checkout completed", e);
        }
    }

    private void handleSubscriptionCreated(Event event) {
        try {
            com.stripe.model.Subscription subscription = 
                (com.stripe.model.Subscription) event.getDataObjectDeserializer().getObject().orElse(null);
            
            if (subscription != null) {
                LOG.infof("Subscription created: %s", subscription.getId());
                subscriptionService.handleSubscriptionCreated(subscription);
            }
        } catch (Exception e) {
            LOG.error("Error handling subscription created", e);
        }
    }

    private void handleSubscriptionUpdated(Event event) {
        try {
            com.stripe.model.Subscription subscription = 
                (com.stripe.model.Subscription) event.getDataObjectDeserializer().getObject().orElse(null);
            
            if (subscription != null) {
                LOG.infof("Subscription updated: %s", subscription.getId());
                subscriptionService.handleSubscriptionUpdated(subscription);
            }
        } catch (Exception e) {
            LOG.error("Error handling subscription updated", e);
        }
    }

    private void handleSubscriptionDeleted(Event event) {
        try {
            com.stripe.model.Subscription subscription = 
                (com.stripe.model.Subscription) event.getDataObjectDeserializer().getObject().orElse(null);
            
            if (subscription != null) {
                LOG.infof("Subscription deleted: %s", subscription.getId());
                subscriptionService.handleSubscriptionDeleted(subscription);
            }
        } catch (Exception e) {
            LOG.error("Error handling subscription deleted", e);
        }
    }

    private void handlePaymentSucceeded(Event event) {
        try {
            com.stripe.model.Invoice invoice = 
                (com.stripe.model.Invoice) event.getDataObjectDeserializer().getObject().orElse(null);
            
            if (invoice != null) {
                LOG.infof("Payment succeeded for subscription: %s", invoice.getSubscription());
                subscriptionService.handlePaymentSucceeded(invoice);
            }
        } catch (Exception e) {
            LOG.error("Error handling payment succeeded", e);
        }
    }

    private void handlePaymentFailed(Event event) {
        try {
            com.stripe.model.Invoice invoice = 
                (com.stripe.model.Invoice) event.getDataObjectDeserializer().getObject().orElse(null);
            
            if (invoice != null) {
                LOG.infof("Payment failed for subscription: %s", invoice.getSubscription());
                subscriptionService.handlePaymentFailed(invoice);
            }
        } catch (Exception e) {
            LOG.error("Error handling payment failed", e);
        }
    }
}
