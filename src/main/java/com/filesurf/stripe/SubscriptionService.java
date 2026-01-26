package com.filesurf.stripe;

import jakarta.enterprise.context.ApplicationScoped;
import jakarta.inject.Inject;
import org.jboss.logging.Logger;

import javax.sql.DataSource;
import java.sql.*;
import java.time.Instant;
import java.util.HashMap;
import java.util.Map;

@ApplicationScoped
public class SubscriptionService {

    private static final Logger LOG = Logger.getLogger(SubscriptionService.class);

    @Inject
    DataSource dataSource;

    /**
     * Get the current plan for a user
     */
    public String getUserPlan(String userId) {
        String sql = """
            SELECT plan_code FROM user_subscriptions
            WHERE user_id = ? AND status = 'active'
            ORDER BY started_at DESC LIMIT 1
        """;

        try (Connection conn = dataSource.getConnection();
             PreparedStatement stmt = conn.prepareStatement(sql)) {
            
            stmt.setString(1, userId);
            ResultSet rs = stmt.executeQuery();

            if (rs.next()) {
                return rs.getString("plan_code");
            }
            return null; // No active subscription

        } catch (SQLException e) {
            LOG.error("Error getting user plan", e);
            return null;
        }
    }

    /**
     * Get plan limits for a user
     */
    public Map<String, Object> getPlanLimits(String userId) {
        String planCode = getUserPlan(userId);
        if (planCode == null) {
            // Return free tier limits
            return getFreeTierLimits();
        }

        String sql = """
            SELECT feature_key, feature_value
            FROM plan_features
            WHERE plan_code = ?
        """;

        Map<String, Object> limits = new HashMap<>();

        try (Connection conn = dataSource.getConnection();
             PreparedStatement stmt = conn.prepareStatement(sql)) {
            
            stmt.setString(1, planCode);
            ResultSet rs = stmt.executeQuery();

            while (rs.next()) {
                String key = rs.getString("feature_key");
                String value = rs.getString("feature_value");
                
                // Parse numeric values
                try {
                    int intValue = Integer.parseInt(value);
                    limits.put(key, intValue);
                } catch (NumberFormatException e) {
                    // Keep as string for boolean/text values
                    limits.put(key, value);
                }
            }

        } catch (SQLException e) {
            LOG.error("Error getting plan limits", e);
        }

        return limits;
    }

    /**
     * Get current usage for a user
     */
    public Map<String, Integer> getCurrentUsage(String userId) {
        Instant now = Instant.now();
        Instant periodStart = now.minusSeconds(30 * 24 * 60 * 60); // 30 days ago

        String sql = """
            SELECT heavy_model_requests, cerebras_requests, storage_bytes, compute_minutes
            FROM user_usage
            WHERE user_id = ? AND period_start >= ?
            ORDER BY period_start DESC LIMIT 1
        """;

        Map<String, Integer> usage = new HashMap<>();
        usage.put("heavy_model_requests", 0);
        usage.put("cerebras_requests", 0);
        usage.put("storage_bytes", 0);
        usage.put("compute_minutes", 0);

        try (Connection conn = dataSource.getConnection();
             PreparedStatement stmt = conn.prepareStatement(sql)) {
            
            stmt.setString(1, userId);
            stmt.setLong(2, periodStart.getEpochSecond());
            ResultSet rs = stmt.executeQuery();

            if (rs.next()) {
                usage.put("heavy_model_requests", rs.getInt("heavy_model_requests"));
                usage.put("cerebras_requests", rs.getInt("cerebras_requests"));
                usage.put("storage_bytes", rs.getInt("storage_bytes"));
                usage.put("compute_minutes", rs.getInt("compute_minutes"));
            }

        } catch (SQLException e) {
            LOG.error("Error getting current usage", e);
        }

        return usage;
    }

    /**
     * Check if user has exceeded their limits
     */
    public boolean checkLimit(String userId, String limitType, int amount) {
        Map<String, Object> limits = getPlanLimits(userId);
        Map<String, Integer> usage = getCurrentUsage(userId);

        Object limitObj = limits.get(limitType);
        if (limitObj == null) {
            return true; // No limit defined, allow
        }

        int limit = (limitObj instanceof Integer) ? (Integer) limitObj : -1;
        if (limit == -1) {
            return true; // Unlimited
        }

        int currentUsage = usage.getOrDefault(limitType, 0);
        return (currentUsage + amount) <= limit;
    }

    /**
     * Increment usage counter
     */
    public void incrementUsage(String userId, String usageType, int amount) {
        Instant now = Instant.now();
        Instant periodStart = now.minusSeconds(30 * 24 * 60 * 60); // 30 days ago
        Instant periodEnd = now.plusSeconds(30 * 24 * 60 * 60);   // 30 days from now

        String sql = """
            INSERT INTO user_usage (user_id, period_start, period_end, %s, last_updated)
            VALUES (?, ?, ?, ?, strftime('%%s', 'now'))
            ON CONFLICT (user_id, period_start) DO UPDATE
            SET %s = %s + ?, last_updated = strftime('%%s', 'now')
        """.formatted(usageType, usageType, usageType);

        try (Connection conn = dataSource.getConnection();
             PreparedStatement stmt = conn.prepareStatement(sql)) {
            
            stmt.setString(1, userId);
            stmt.setLong(2, periodStart.getEpochSecond());
            stmt.setLong(3, periodEnd.getEpochSecond());
            stmt.setInt(4, amount);
            stmt.setInt(5, amount);
            stmt.executeUpdate();

        } catch (SQLException e) {
            LOG.error("Error incrementing usage", e);
        }
    }

    /**
     * Get Stripe customer ID for a user
     */
    public String getStripeCustomerId(String userId) {
        String sql = """
            SELECT stripe_customer_id FROM user_subscriptions
            WHERE user_id = ? AND status = 'active'
            ORDER BY started_at DESC LIMIT 1
        """;

        try (Connection conn = dataSource.getConnection();
             PreparedStatement stmt = conn.prepareStatement(sql)) {
            
            stmt.setString(1, userId);
            ResultSet rs = stmt.executeQuery();

            if (rs.next()) {
                return rs.getString("stripe_customer_id");
            }

        } catch (SQLException e) {
            LOG.error("Error getting Stripe customer ID", e);
        }

        return null;
    }

    /**
     * Handle checkout completion from Stripe
     */
    public void handleCheckoutCompleted(String userId, String customerId, String subscriptionId) {
        // Get subscription details from Stripe to determine plan
        // For now, assume basic plan (this should be enhanced)
        String planCode = "basic";

        String sql = """
            INSERT INTO user_subscriptions 
            (user_id, plan_code, status, stripe_customer_id, stripe_subscription_id, started_at)
            VALUES (?, ?, 'active', ?, ?, strftime('%s', 'now'))
        """;

        try (Connection conn = dataSource.getConnection();
             PreparedStatement stmt = conn.prepareStatement(sql)) {
            
            stmt.setString(1, userId);
            stmt.setString(2, planCode);
            stmt.setString(3, customerId);
            stmt.setString(4, subscriptionId);
            stmt.executeUpdate();

            LOG.infof("Created subscription for user %s with plan %s", userId, planCode);

        } catch (SQLException e) {
            LOG.error("Error creating subscription", e);
        }
    }

    /**
     * Handle subscription created from Stripe
     */
    public void handleSubscriptionCreated(com.stripe.model.Subscription subscription) {
        // Implementation depends on how you map Stripe subscriptions to users
        LOG.infof("Subscription created: %s", subscription.getId());
    }

    /**
     * Handle subscription updated from Stripe
     */
    public void handleSubscriptionUpdated(com.stripe.model.Subscription subscription) {
        String sql = """
            UPDATE user_subscriptions
            SET status = ?
            WHERE stripe_subscription_id = ?
        """;

        String status = subscription.getStatus();

        try (Connection conn = dataSource.getConnection();
             PreparedStatement stmt = conn.prepareStatement(sql)) {
            
            stmt.setString(1, status);
            stmt.setString(2, subscription.getId());
            stmt.executeUpdate();

            LOG.infof("Updated subscription %s to status %s", subscription.getId(), status);

        } catch (SQLException e) {
            LOG.error("Error updating subscription", e);
        }
    }

    /**
     * Handle subscription deleted from Stripe
     */
    public void handleSubscriptionDeleted(com.stripe.model.Subscription subscription) {
        String sql = """
            UPDATE user_subscriptions
            SET status = 'cancelled', cancelled_at = strftime('%s', 'now')
            WHERE stripe_subscription_id = ?
        """;

        try (Connection conn = dataSource.getConnection();
             PreparedStatement stmt = conn.prepareStatement(sql)) {
            
            stmt.setString(1, subscription.getId());
            stmt.executeUpdate();

            LOG.infof("Cancelled subscription %s", subscription.getId());

        } catch (SQLException e) {
            LOG.error("Error cancelling subscription", e);
        }
    }

    /**
     * Handle payment succeeded from Stripe
     */
    public void handlePaymentSucceeded(com.stripe.model.Invoice invoice) {
        LOG.infof("Payment succeeded for subscription %s", invoice.getSubscription());
        // Update payment history, send receipt, etc.
    }

    /**
     * Handle payment failed from Stripe
     */
    public void handlePaymentFailed(com.stripe.model.Invoice invoice) {
        LOG.warnf("Payment failed for subscription %s", invoice.getSubscription());
        // Send notification to user, retry payment, etc.
    }

    private Map<String, Object> getFreeTierLimits() {
        Map<String, Object> limits = new HashMap<>();
        limits.put("heavy_model_limit", 10);
        limits.put("storage_gb", 1);
        limits.put("compute_minutes", 60);
        return limits;
    }
}
