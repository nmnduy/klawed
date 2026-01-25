#!/bin/bash

# Script to generate Stripe products and prices programmatically
# Usage: ./scripts/create-stripe-products.sh

set -e

echo "Creating Stripe products and prices..."
echo ""

if [ -z "$STRIPE_SECRET_KEY" ]; then
    echo "Error: STRIPE_SECRET_KEY environment variable not set"
    echo ""
    echo "Usage:"
    echo "  STRIPE_SECRET_KEY=sk_test_xxx ./scripts/create-stripe-products.sh"
    echo ""
    echo "Get your API key from: https://dashboard.stripe.com/apikeys"
    exit 1
fi

# Run the Java generator
mvn exec:java -Dexec.mainClass="com.filesurf.stripe.StripeProductGenerator" -q
