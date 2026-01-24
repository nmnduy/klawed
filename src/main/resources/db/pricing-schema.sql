-- Pricing Plans Schema for FileSurf v2

-- Plan definitions (immutable reference data)
CREATE TABLE IF NOT EXISTS pricing_plans (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    plan_code TEXT NOT NULL UNIQUE,  -- 'basic', 'pro', 'enterprise'
    plan_name TEXT NOT NULL,         -- 'Basic', 'Professional', 'Enterprise'
    price_cents INTEGER NOT NULL,    -- 9999, 29999, or -1 for contact sales
    description TEXT,
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP
);

-- Plan features (what each plan includes)
CREATE TABLE IF NOT EXISTS plan_features (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    plan_code TEXT NOT NULL,
    feature_key TEXT NOT NULL,       -- 'heavy_model_limit', 'storage_gb', etc.
    feature_value TEXT NOT NULL,     -- JSON string for complex values
    display_name TEXT NOT NULL,      -- Human readable feature name
    sort_order INTEGER DEFAULT 0,
    FOREIGN KEY (plan_code) REFERENCES pricing_plans(plan_code),
    UNIQUE(plan_code, feature_key)
);

-- User subscriptions
CREATE TABLE IF NOT EXISTS user_subscriptions (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    user_id TEXT NOT NULL,
    plan_code TEXT NOT NULL,
    status TEXT NOT NULL DEFAULT 'active',  -- active, cancelled, expired, trial
    stripe_customer_id TEXT,         -- Stripe customer ID
    stripe_subscription_id TEXT,     -- Stripe subscription ID
    started_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    expires_at TIMESTAMP,            -- NULL for active subscriptions
    trial_ends_at TIMESTAMP,         -- For trial period tracking
    cancelled_at TIMESTAMP,
    cancellation_reason TEXT,
    FOREIGN KEY (user_id) REFERENCES users(user_id),
    FOREIGN KEY (plan_code) REFERENCES pricing_plans(plan_code)
);

-- Usage tracking per user
CREATE TABLE IF NOT EXISTS user_usage (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    user_id TEXT NOT NULL,
    period_start TIMESTAMP NOT NULL,  -- Start of billing period
    period_end TIMESTAMP NOT NULL,    -- End of billing period
    heavy_model_requests INTEGER DEFAULT 0,
    cerebras_requests INTEGER DEFAULT 0,
    storage_bytes INTEGER DEFAULT 0,
    compute_minutes INTEGER DEFAULT 0,
    last_updated TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    FOREIGN KEY (user_id) REFERENCES users(user_id),
    UNIQUE(user_id, period_start)
);

-- Create indexes for performance
CREATE INDEX IF NOT EXISTS idx_user_subscriptions_user ON user_subscriptions(user_id);
CREATE INDEX IF NOT EXISTS idx_user_subscriptions_status ON user_subscriptions(status);
CREATE INDEX IF NOT EXISTS idx_user_usage_user ON user_usage(user_id);
CREATE INDEX IF NOT EXISTS idx_user_usage_period ON user_usage(period_start, period_end);

-- Insert default pricing plans
INSERT OR IGNORE INTO pricing_plans (plan_code, plan_name, price_cents, description) VALUES
    ('basic', 'Basic', 9999, 'Essential features for individuals'),
    ('pro', 'Professional', 29999, 'Advanced features for power users'),
    ('enterprise', 'Enterprise', -1, 'Custom solutions for organizations');

-- Insert plan features for Basic plan ($99.99)
INSERT OR IGNORE INTO plan_features (plan_code, feature_key, feature_value, display_name, sort_order) VALUES
    ('basic', 'heavy_model_limit', '100', 'Limited use of heavy models', 1),
    ('basic', 'cerebras_fallback', 'true', 'Fallback to Cerebras models', 2),
    ('basic', 'compute_environment', 'shared', 'Shared secured sandboxed compute environment', 3),
    ('basic', 'data_privacy', 'true', 'Data privacy', 4),
    ('basic', 'storage_gb', '500', 'Limited storage to 500GB', 5);

-- Insert plan features for Professional plan ($299.99)
INSERT OR IGNORE INTO plan_features (plan_code, feature_key, feature_value, display_name, sort_order) VALUES
    ('pro', 'heavy_model_limit', '1000', 'Higher limit on heavy models', 1),
    ('pro', 'cerebras_fallback', 'true', 'Fallback to Cerebras models', 2),
    ('pro', 'compute_environment', 'shared', 'Shared secured sandboxed compute environment', 3),
    ('pro', 'data_privacy', 'true', 'Data privacy', 4),
    ('pro', 'storage_gb', '2000', 'Storage up to 2TB', 5),
    ('pro', 'priority_support', 'true', 'Priority support', 6);

-- Insert plan features for Enterprise plan (Contact Sales)
INSERT OR IGNORE INTO plan_features (plan_code, feature_key, feature_value, display_name, sort_order) VALUES
    ('enterprise', 'heavy_model_limit', '-1', 'Unlimited use of highest quality models', 1),
    ('enterprise', 'data_privacy', 'true', 'Data privacy', 2),
    ('enterprise', 'compute_environment', 'dedicated', 'Dedicated cloud compute nodes', 3),
    ('enterprise', 'storage_gb', '-1', 'Unlimited storage', 4),
    ('enterprise', 'dedicated_support', 'true', 'Dedicated support team', 5),
    ('enterprise', 'sla', 'true', '99.9% uptime SLA', 6),
    ('enterprise', 'custom_deployment', 'true', 'Custom deployment options', 7);
