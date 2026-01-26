# Pricing Section Added to Landing Page

## Summary
Added a complete pricing section to the main landing page (`index.html`) with Stripe integration for payment processing.

## Changes Made

### 1. Updated `index.html` Template
**Location:** `src/main/resources/templates/index.html`

**Changes:**
- Added Stripe.js script in the `<head>` section
- Added "Pricing" link to the navigation menu
- Inserted a new pricing section between Security and Contact sections
- Added JavaScript functions for Stripe checkout and sales contact

**Pricing Plans:**
- **Basic**: $99.99/month - Limited heavy models, 500GB storage
- **Professional**: $299.99/month - Higher limits, 2TB storage, priority support (marked as POPULAR)
- **Enterprise**: Custom pricing - Unlimited models, dedicated compute, 99.9% SLA

**Design:**
- Uses modern design tokens (hsl color system) consistent with the rest of the page
- Responsive grid layout (3 columns on desktop, stacks on mobile)
- Professional plan has a gradient background and "POPULAR" badge
- Hover effects on all pricing cards
- Call-to-action buttons for each plan

### 2. Updated `LandingPageResource.java`
**Location:** `src/main/java/com/filesurf/pricing/LandingPageResource.java`

**Changes:**
- Added `stripePublicKey` to template data in `getLandingPage()` method
- This provides the Stripe public key to the JavaScript for secure checkout

### 3. JavaScript Functionality

**Functions Added:**
- `selectPlan(planCode, priceId)` - Handles plan selection and Stripe checkout
  - Checks authentication status
  - Redirects to login if not authenticated
  - Creates Stripe checkout session
  - Redirects to Stripe payment page
  
- `contactSales()` - Scrolls to contact form for Enterprise plan

## How It Works

1. **User clicks "Get Started" on Basic or Professional plan:**
   - JavaScript checks if user is authenticated (`/auth/status`)
   - If not authenticated: redirects to login with return URL
   - If authenticated: creates Stripe checkout session via `/api/stripe/create-checkout-session`
   - Redirects to Stripe Checkout page

2. **User clicks "Contact Sales" on Enterprise plan:**
   - Smoothly scrolls to the contact form at the bottom of the page

3. **After successful payment:**
   - User is redirected to `/pricing/success?session_id={id}`

4. **After cancelled payment:**
   - User is redirected to `/pricing/cancel`

## Navigation

The pricing section is accessible via:
- Navigation menu link: "Pricing"
- Direct anchor: `/#pricing`
- Scroll from hero section

## Testing in Production

To see the pricing section in production:

1. Visit your production URL (e.g., `https://yourdomain.com/`)
2. Click "Pricing" in the navigation menu, or
3. Scroll down to the pricing section, or
4. Visit directly: `https://yourdomain.com/#pricing`

## Prerequisites

The following must be configured for pricing to work:

1. **Stripe Configuration** (see `docs/STRIPE_INTEGRATION.md`):
   - `stripe.public.key` - Stripe public key
   - `stripe.secret.key` - Stripe secret key
   - Pricing plans in database (`pricing_plans` table)
   
2. **Database Schema** (`src/main/resources/db/pricing-schema.sql`):
   - `pricing_plans` table with plan definitions
   - Default plans should be inserted

3. **Stripe API Endpoint**:
   - `/api/stripe/create-checkout-session` must be implemented

## Files Modified

1. `src/main/resources/templates/index.html`
2. `src/main/java/com/filesurf/pricing/LandingPageResource.java`

## Build and Deploy

```bash
# Build CSS/JS
bun run build:dev  # or: bun run build for production

# Run locally
mvn quarkus:dev

# Deploy to production
./deployment/deploy-rsync.sh
```

## Future Enhancements

Potential improvements:
- Add annual billing toggle (save 20%)
- Add feature comparison table
- Add FAQ section below pricing
- Add customer testimonials
- Implement usage-based pricing tier
