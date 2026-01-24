# Stripe Integration Documentation

## Overview

FileSurf v2 uses Stripe for payment processing and subscription management. This document covers the complete pricing plan implementation, Stripe integration, and setup instructions.

## Pricing Plans

### 1. Basic Plan - $99.99/month
- Limited use of heavy models (100 requests/month)
- Fallback to Cerebras models for faster responses
- Shared secured sandboxed compute environment
- Data privacy guaranteed
- 500GB storage limit

### 2. Professional Plan - $299.99/month (Most Popular)
- Higher limit on heavy models (1000 requests/month)
- Fallback to Cerebras models
- Shared secured sandboxed compute environment
- Data privacy guaranteed
- 2TB storage limit
- Priority support

### 3. Enterprise Plan - Custom Pricing
- Unlimited use of highest quality models
- Data privacy guaranteed
- Dedicated cloud compute nodes
- Unlimited storage
- Dedicated support team
- 99.9% uptime SLA
- Custom deployment options
- Contact sales for pricing

## Architecture

### Database Schema

The pricing system uses four main tables:

1. **pricing_plans**: Stores plan definitions (Basic, Pro, Enterprise)
2. **plan_features**: Stores feature definitions and limits for each plan
3. **user_subscriptions**: Tracks user subscriptions and Stripe IDs
4. **user_usage**: Tracks usage metrics per user per billing period

Schema location: `src/main/resources/db/pricing-schema.sql`

### Backend Components

1. **StripeService** (`com.filesurf.stripe.StripeService`)
   - Initializes Stripe SDK
   - Creates checkout sessions
   - Creates customer portal sessions
   - Verifies webhook signatures

2. **StripeResource** (`com.filesurf.stripe.StripeResource`)
   - REST endpoints for Stripe operations
   - Webhook handler for Stripe events
   - Handles subscription lifecycle events

3. **SubscriptionService** (`com.filesurf.stripe.SubscriptionService`)
   - Manages user subscriptions
   - Tracks usage and enforces limits
   - Queries plan features and limits

4. **LandingPageResource** (`com.filesurf.pricing.LandingPageResource`)
   - Serves landing page with pricing section
   - Success and cancel pages after checkout

### Frontend Components

1. **Landing Page** (`templates/landing.html`)
   - Hero section with call-to-action
   - Features showcase
   - Pricing section with three tiers
   - Stripe checkout integration

2. **Success Page** (`templates/pricingSuccess.html`)
   - Displayed after successful payment
   - Shows session ID
   - Links to dashboard

3. **Cancel Page** (`templates/pricingCancel.html`)
   - Displayed when user cancels checkout
   - Option to try again

## Setup Instructions

### Prerequisites

1. Stripe account (sign up at https://stripe.com)
2. Maven and Java 21
3. SQLite3

### Step 1: Run Setup Script

```bash
./scripts/setup-stripe.sh
```

This interactive script will:
- Prompt for your Stripe API keys
- Set up environment variables
- Initialize the database schema
- Guide you through Stripe Dashboard setup

### Step 2: Create Stripe Products

1. Go to https://dashboard.stripe.com/products
2. Create two products:

**Basic Plan:**
- Name: FileSurf Basic
- Description: Essential features for individuals
- Pricing: $99.99/month recurring
- Copy the Price ID (starts with `price_`)

**Professional Plan:**
- Name: FileSurf Professional
- Description: Advanced features for power users
- Pricing: $299.99/month recurring
- Copy the Price ID (starts with `price_`)

### Step 3: Update Price IDs

Edit `src/main/java/com/filesurf/stripe/StripeService.java`:

```java
public Map<String, String> getPlanPriceIds() {
    Map<String, String> priceIds = new HashMap<>();
    priceIds.put("basic", "price_YOUR_BASIC_PRICE_ID");
    priceIds.put("pro", "price_YOUR_PRO_PRICE_ID");
    return priceIds;
}
```

### Step 4: Configure Webhooks

1. Go to https://dashboard.stripe.com/webhooks
2. Click "Add endpoint"
3. Set endpoint URL:
   - Development: `http://localhost:9090/api/stripe/webhook`
   - Production: `https://your-domain.com/api/stripe/webhook`
4. Select events to listen for:
   - `checkout.session.completed`
   - `customer.subscription.created`
   - `customer.subscription.updated`
   - `customer.subscription.deleted`
   - `invoice.payment_succeeded`
   - `invoice.payment_failed`
5. Copy the webhook signing secret

### Step 5: Set Environment Variables

Create or update `.env.local` (development) or `/etc/filesurf/klawed.env` (production):

```bash
# Stripe API Keys
STRIPE_SECRET_KEY=sk_test_your_test_key
STRIPE_PUBLIC_KEY=pk_test_your_test_key
STRIPE_WEBHOOK_SECRET=whsec_your_webhook_secret

# Application Base URL
APP_BASE_URL=http://localhost:9090  # Development
# APP_BASE_URL=https://filesurf.io  # Production
```

### Step 6: Start Application

```bash
# Development
mvn quarkus:dev

# Production
java -jar target/quarkus-app/quarkus-run.jar
```

## API Endpoints

### Public Endpoints

- `GET /` - Landing page with pricing section
- `GET /pricing/success?session_id={id}` - Payment success page
- `GET /pricing/cancel` - Payment cancelled page

### Protected Endpoints (Require Authentication)

- `POST /api/stripe/create-checkout-session` - Create Stripe checkout
  ```json
  {
    "planCode": "basic",
    "priceId": "price_xxx"
  }
  ```
  Returns: `{ "sessionId": "cs_test_xxx" }`

- `POST /api/stripe/create-portal-session` - Create customer portal
  Returns: `{ "url": "https://billing.stripe.com/..." }`

### Webhook Endpoint

- `POST /api/stripe/webhook` - Stripe webhook handler
  - Requires `Stripe-Signature` header
  - Processes subscription lifecycle events

## Usage Enforcement

### Checking Limits

```java
@Inject
SubscriptionService subscriptionService;

// Check if user can make a heavy model request
boolean canUse = subscriptionService.checkLimit(userId, "heavy_model_limit", 1);
if (!canUse) {
    // User has exceeded their limit
    // Fallback to Cerebras or show upgrade prompt
}
```

### Incrementing Usage

```java
// After successful heavy model request
subscriptionService.incrementUsage(userId, "heavy_model_requests", 1);

// After file upload
subscriptionService.incrementUsage(userId, "storage_bytes", fileSize);
```

### Getting Plan Information

```java
// Get user's current plan
String planCode = subscriptionService.getUserPlan(userId);
// Returns: "basic", "pro", "enterprise", or null (free tier)

// Get plan limits
Map<String, Object> limits = subscriptionService.getPlanLimits(userId);
int heavyModelLimit = (int) limits.get("heavy_model_limit");

// Get current usage
Map<String, Integer> usage = subscriptionService.getCurrentUsage(userId);
int currentRequests = usage.get("heavy_model_requests");
```

## Testing

### Test Cards

Use Stripe's test cards (https://stripe.com/docs/testing#cards):

- **Success**: 4242 4242 4242 4242
- **Declined**: 4000 0000 0000 0002
- **Requires authentication**: 4000 0025 0000 3155

Any future expiration date and any 3-digit CVC will work.

### Testing Webhooks Locally

Use Stripe CLI to forward webhooks to localhost:

```bash
# Install Stripe CLI
# https://stripe.com/docs/stripe-cli

# Login
stripe login

# Forward webhooks
stripe listen --forward-to localhost:9090/api/stripe/webhook

# Trigger test events
stripe trigger checkout.session.completed
stripe trigger customer.subscription.created
```

## Customer Portal

Users can manage their subscription through Stripe's Customer Portal:

```javascript
// Frontend code to open portal
async function manageSubscription() {
    const response = await fetch('/api/stripe/create-portal-session', {
        method: 'POST'
    });
    const { url } = await response.json();
    window.location.href = url;
}
```

The portal allows users to:
- Update payment method
- View invoices
- Cancel subscription
- Update billing information

## Security Considerations

1. **API Keys**: Never commit real API keys to version control
2. **Webhook Signatures**: Always verify webhook signatures
3. **HTTPS**: Use HTTPS in production for secure cookie transmission
4. **Environment Variables**: Store sensitive data in environment variables
5. **Price IDs**: Validate price IDs server-side before creating checkout sessions

## Production Checklist

- [ ] Replace test API keys with production keys
- [ ] Update `APP_BASE_URL` to production domain
- [ ] Configure webhook endpoint with production URL
- [ ] Test complete checkout flow
- [ ] Test webhook event handling
- [ ] Set up monitoring for failed payments
- [ ] Configure email notifications for subscription events
- [ ] Test customer portal
- [ ] Document refund process
- [ ] Set up billing alerts

## Troubleshooting

### Webhook Not Receiving Events

1. Check webhook endpoint is publicly accessible
2. Verify webhook secret is correct
3. Check Stripe Dashboard > Webhooks for delivery status
4. Ensure correct events are selected
5. Check application logs for errors

### Checkout Session Creation Fails

1. Verify API keys are correct
2. Check price IDs are valid
3. Ensure user is authenticated
4. Check application logs for Stripe errors

### Subscription Not Created After Payment

1. Check webhook is configured correctly
2. Verify `checkout.session.completed` event is received
3. Check database for subscription records
4. Review application logs for errors

## Support

For Stripe-specific issues:
- Stripe Documentation: https://stripe.com/docs
- Stripe Support: https://support.stripe.com

For FileSurf-specific issues:
- Check application logs: `logs/application.log`
- Review database records: `sqlite3 data/filesurf.db`
- Contact support: support@filesurf.example.com
