#!/bin/bash
# Script to help configure Stripe integration for FileSurf

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

echo "================================================"
echo "FileSurf Stripe Configuration Setup"
echo "================================================"
echo ""

# Check if running in production
if [ -f "/etc/filesurf/klawed.env" ]; then
    ENV_FILE="/etc/filesurf/klawed.env"
    echo "Production environment detected"
    echo "Environment file: $ENV_FILE"
else
    ENV_FILE="$PROJECT_ROOT/.env.local"
    echo "Development environment detected"
    echo "Environment file: $ENV_FILE"
fi

echo ""
echo "Step 1: Get your Stripe API keys"
echo "================================================"
echo "1. Go to: https://dashboard.stripe.com/apikeys"
echo "2. Copy your keys (use test keys for development)"
echo ""

# Prompt for API keys
read -p "Enter your Stripe Secret Key (sk_test_...): " STRIPE_SECRET_KEY
read -p "Enter your Stripe Public Key (pk_test_...): " STRIPE_PUBLIC_KEY

echo ""
echo "Step 2: Set up Stripe Webhook"
echo "================================================"
echo "1. Go to: https://dashboard.stripe.com/webhooks"
echo "2. Click 'Add endpoint'"
echo "3. Set endpoint URL to: https://your-domain.com/api/stripe/webhook"
echo "4. Select events to listen for:"
echo "   - checkout.session.completed"
echo "   - customer.subscription.created"
echo "   - customer.subscription.updated"
echo "   - customer.subscription.deleted"
echo "   - invoice.payment_succeeded"
echo "   - invoice.payment_failed"
echo "5. Copy the webhook signing secret"
echo ""

read -p "Enter your Stripe Webhook Secret (whsec_...): " STRIPE_WEBHOOK_SECRET

echo ""
echo "Step 3: Create Stripe Products and Prices"
echo "================================================"
echo "You need to create products in Stripe Dashboard:"
echo ""
echo "Basic Plan:"
echo "  - Name: FileSurf Basic"
echo "  - Price: \$99.99/month"
echo "  - Copy the Price ID (price_...)"
echo ""
read -p "Enter Basic Plan Price ID: " PRICE_BASIC

echo ""
echo "Professional Plan:"
echo "  - Name: FileSurf Professional"
echo "  - Price: \$299.99/month"
echo "  - Copy the Price ID (price_...)"
echo ""
read -p "Enter Professional Plan Price ID: " PRICE_PRO

echo ""
echo "Step 4: Writing configuration..."
echo "================================================"

# Create or update environment file
cat > "$ENV_FILE" << EOF
# Stripe Configuration
STRIPE_SECRET_KEY=$STRIPE_SECRET_KEY
STRIPE_PUBLIC_KEY=$STRIPE_PUBLIC_KEY
STRIPE_WEBHOOK_SECRET=$STRIPE_WEBHOOK_SECRET

# Stripe Price IDs
STRIPE_PRICE_BASIC=$PRICE_BASIC
STRIPE_PRICE_PRO=$PRICE_PRO

# Application Base URL (update for production)
APP_BASE_URL=http://localhost:9090
EOF

echo "Configuration written to: $ENV_FILE"
echo ""

echo "Step 5: Initialize database schema"
echo "================================================"

# Check if SQLite is available
if ! command -v sqlite3 &> /dev/null; then
    echo "Warning: sqlite3 command not found. Please install SQLite3."
    echo "You can initialize the schema manually using:"
    echo "  sqlite3 data/filesurf.db < src/main/resources/db/pricing-schema.sql"
else
    DB_PATH="$PROJECT_ROOT/data/filesurf.db"
    SCHEMA_FILE="$PROJECT_ROOT/src/main/resources/db/pricing-schema.sql"
    
    if [ -f "$SCHEMA_FILE" ]; then
        mkdir -p "$(dirname "$DB_PATH")"
        sqlite3 "$DB_PATH" < "$SCHEMA_FILE"
        echo "Database schema initialized at: $DB_PATH"
    else
        echo "Warning: Schema file not found at $SCHEMA_FILE"
    fi
fi

echo ""
echo "================================================"
echo "Setup Complete!"
echo "================================================"
echo ""
echo "Next steps:"
echo "1. Update the Price IDs in StripeService.java:"
echo "   - Open: src/main/java/com/filesurf/stripe/StripeService.java"
echo "   - Update getPlanPriceIds() method with your actual price IDs"
echo ""
echo "2. Start your application:"
echo "   mvn quarkus:dev"
echo ""
echo "3. Access the landing page:"
echo "   http://localhost:9090/"
echo ""
echo "4. Test the checkout flow with Stripe test cards:"
echo "   https://stripe.com/docs/testing#cards"
echo ""
echo "For production deployment:"
echo "1. Update APP_BASE_URL in $ENV_FILE"
echo "2. Use production API keys (sk_live_... and pk_live_...)"
echo "3. Configure webhook endpoint to point to production URL"
echo ""
