package com.filesurf.stripe;

import com.stripe.Stripe;
import com.stripe.exception.StripeException;
import com.stripe.model.checkout.Session;
import com.stripe.param.checkout.SessionCreateParams;
import jakarta.enterprise.context.ApplicationScoped;
import org.eclipse.microprofile.config.inject.ConfigProperty;
import org.jboss.logging.Logger;

import java.util.HashMap;
import java.util.Map;

@ApplicationScoped
public class StripeService {

    private static final Logger LOG = Logger.getLogger(StripeService.class);

    @ConfigProperty(name = "stripe.secret.key")
    String stripeSecretKey;

    @ConfigProperty(name = "stripe.public.key")
    String stripePublicKey;

    @ConfigProperty(name = "stripe.webhook.secret")
    String webhookSecret;

    @ConfigProperty(name = "app.base.url", defaultValue = "http://localhost:8080")
    String baseUrl;

    public void init() {
        Stripe.apiKey = stripeSecretKey;
        LOG.info("Stripe service initialized");
    }

    /**
     * Create a Stripe Checkout session for a pricing plan
     */
    public Session createCheckoutSession(String userId, String planCode, String priceId) throws StripeException {
        init();

        SessionCreateParams.Builder paramsBuilder = SessionCreateParams.builder()
                .setMode(SessionCreateParams.Mode.SUBSCRIPTION)
                .setSuccessUrl(baseUrl + "/pricing/success?session_id={CHECKOUT_SESSION_ID}")
                .setCancelUrl(baseUrl + "/pricing/cancel")
                .addLineItem(
                        SessionCreateParams.LineItem.builder()
                                .setPrice(priceId)
                                .setQuantity(1L)
                                .build()
                )
                .setClientReferenceId(userId);

        // Add customer email if available
        if (userId != null) {
            paramsBuilder.putMetadata("userId", userId);
        }

        SessionCreateParams params = paramsBuilder.build();
        Session session = Session.create(params);

        LOG.infof("Created Stripe checkout session: %s for user: %s, plan: %s", 
                  session.getId(), userId, planCode);

        return session;
    }

    /**
     * Create a customer portal session for managing subscription
     */
    public com.stripe.model.billingportal.Session createPortalSession(String customerId) throws StripeException {
        init();

        com.stripe.param.billingportal.SessionCreateParams params =
                com.stripe.param.billingportal.SessionCreateParams.builder()
                        .setCustomer(customerId)
                        .setReturnUrl(baseUrl + "/file-chat")
                        .build();

        return com.stripe.model.billingportal.Session.create(params);
    }

    /**
     * Verify webhook signature
     */
    public com.stripe.model.Event constructEvent(String payload, String sigHeader) throws StripeException {
        return com.stripe.model.Event.constructFrom(
                com.stripe.net.ApiResource.GSON.fromJson(payload, com.stripe.model.Event.class).getRawJsonObject(),
                stripeSecretKey,
                sigHeader,
                webhookSecret
        );
    }

    public String getPublicKey() {
        return stripePublicKey;
    }

    /**
     * Get price IDs for each plan (these should match your Stripe dashboard)
     */
    public Map<String, String> getPlanPriceIds() {
        Map<String, String> priceIds = new HashMap<>();
        priceIds.put("basic", "price_basic_monthly");  // Replace with actual Stripe price ID
        priceIds.put("pro", "price_pro_monthly");      // Replace with actual Stripe price ID
        return priceIds;
    }
}
