package com.filesurf.stripe;

import com.stripe.Stripe;
import com.stripe.exception.StripeException;
import com.stripe.model.Price;
import com.stripe.model.Product;
import com.stripe.param.PriceCreateParams;
import com.stripe.param.ProductCreateParams;

import java.util.HashMap;
import java.util.Map;

/**
 * Utility to generate Stripe products and prices programmatically.
 * Run with: mvn exec:java -Dexec.mainClass="com.filesurf.stripe.StripeProductGenerator"
 */
public class StripeProductGenerator {

    public static void main(String[] args) {
        // Get API key from environment or use default
        String apiKey = System.getenv("STRIPE_SECRET_KEY");
        if (apiKey == null || apiKey.isEmpty()) {
            System.err.println("Error: STRIPE_SECRET_KEY environment variable not set");
            System.err.println("Usage: STRIPE_SECRET_KEY=sk_test_xxx mvn exec:java -Dexec.mainClass=\"com.filesurf.stripe.StripeProductGenerator\"");
            System.exit(1);
        }

        Stripe.apiKey = apiKey;

        System.out.println("Creating Stripe products and prices...\n");

        try {
            // Create Basic Plan
            Map<String, String> basic = createProductAndPrice(
                "FileSurf Basic",
                "Essential features for individuals - 100 heavy model requests/month, 500GB storage",
                9999L, // $99.99
                "month",
                "basic"
            );
            System.out.printf("Basic Plan - Product ID: %s, Price ID: %s%n%n", basic.get("product"), basic.get("price"));

            // Create Professional Plan
            Map<String, String> pro = createProductAndPrice(
                "FileSurf Professional",
                "Advanced features for power users - 1000 heavy model requests/month, 2TB storage, priority support",
                29999L, // $299.99
                "month",
                "pro"
            );
            System.out.printf("Professional Plan - Product ID: %s, Price ID: %s%n%n", pro.get("product"), pro.get("price"));

            // Create Enterprise Plan
            Map<String, String> enterprise = createProductAndPrice(
                "FileSurf Enterprise",
                "Unlimited access with dedicated compute nodes, unlimited storage, 99.9% SLA, custom deployment",
                99999L, // $999.99
                "month",
                "enterprise"
            );
            System.out.printf("Enterprise Plan - Product ID: %s, Price ID: %s%n%n", enterprise.get("product"), enterprise.get("price"));

            System.out.println("=================================");
            System.out.println("Products and prices created successfully!");
            System.out.println("=================================");
            System.out.println("\nUpdate StripeService.java with these Price IDs:");
            System.out.printf("  basic:    \"%s\"%n", basic.get("price"));
            System.out.printf("  pro:      \"%s\"%n", pro.get("price"));
            System.out.printf("  enterprise: \"%s\"%n", enterprise.get("price"));

        } catch (StripeException e) {
            System.err.println("Error creating Stripe products: " + e.getMessage());
            e.printStackTrace();
            System.exit(1);
        }
    }

    private static Map<String, String> createProductAndPrice(
            String name,
            String description,
            Long amount,
            String interval,
            String metadataKey
    ) throws StripeException {
        // Create product
        ProductCreateParams productParams = ProductCreateParams.builder()
                .setName(name)
                .setDescription(description)
                .putMetadata("plan", metadataKey)
                .build();

        Product product = Product.create(productParams);

        // Create price for the product
        PriceCreateParams.Recurring.Builder recurringBuilder = PriceCreateParams.Recurring.builder();
        if ("month".equals(interval)) {
            recurringBuilder.setInterval(PriceCreateParams.Recurring.Interval.MONTH);
        } else if ("year".equals(interval)) {
            recurringBuilder.setInterval(PriceCreateParams.Recurring.Interval.YEAR);
        } else {
            recurringBuilder.setInterval(PriceCreateParams.Recurring.Interval.DAY);
        }

        PriceCreateParams priceParams = PriceCreateParams.builder()
                .setProduct(product.getId())
                .setCurrency("usd")
                .setRecurring(recurringBuilder.build())
                .setUnitAmount(amount)
                .putMetadata("plan", metadataKey)
                .build();

        Price price = Price.create(priceParams);

        Map<String, String> result = new HashMap<>();
        result.put("product", product.getId());
        result.put("price", price.getId());
        return result;
    }
}
