/**
 * Pricing and Checkout Logic
 * Handles Stripe checkout and contact form interactions
 */

// Stripe instance (initialized with public key from template)
let stripe;

/**
 * Initialize Stripe with the provided public key
 * Called from the template after Stripe.js loads
 * @param {string} publicKey - Stripe public key
 */
function initStripe(publicKey) {
    stripe = Stripe(publicKey);
}

/**
 * Handle plan selection and checkout flow
 * @param {string} planCode - Plan identifier (basic, pro, enterprise)
 * @param {string} priceId - Stripe price ID
 */
async function selectPlan(planCode, priceId) {
    try {
        // Check if user is authenticated
        const authResponse = await fetch('/auth/status');
        const authData = await authResponse.json();
        
        if (!authData.authenticated) {
            // Redirect to login with return URL
            window.location.href = `/auth/login?return=/pricing/checkout?plan=${planCode}`;
            return;
        }

        // Create checkout session
        const response = await fetch('/api/stripe/create-checkout-session', {
            method: 'POST',
            headers: {
                'Content-Type': 'application/json',
            },
            body: JSON.stringify({
                planCode: planCode,
                priceId: priceId
            })
        });

        const session = await response.json();

        if (session.error) {
            alert('Error: ' + session.error);
            return;
        }

        // Redirect to Stripe Checkout
        const result = await stripe.redirectToCheckout({
            sessionId: session.sessionId
        });

        if (result.error) {
            alert(result.error.message);
        }
    } catch (error) {
        console.error('Error:', error);
        alert('An error occurred. Please try again.');
    }
}

/**
 * Scroll to contact form (for Enterprise plan)
 */
function contactSales() {
    const contactSection = document.getElementById('contact');
    if (contactSection) {
        contactSection.scrollIntoView({ behavior: 'smooth' });
    }
}

/**
 * Initialize contact form handler
 * Should be called after DOM is loaded
 */
function initContactForm() {
    const contactForm = document.getElementById('contact-form');
    if (!contactForm) return;

    contactForm.addEventListener('submit', async function(e) {
        e.preventDefault();

        const form = e.target;
        const submitBtn = document.getElementById('submit-btn');
        const thankYou = document.getElementById('thank-you');
        const formFields = form.querySelectorAll('input, textarea, button');

        submitBtn.disabled = true;
        submitBtn.innerHTML = '<span class="three-dot-loader"><span class="dot"></span><span class="dot"></span><span class="dot"></span></span>';

        try {
            const formData = {
                email: document.getElementById('email').value,
                company: document.getElementById('company').value,
                message: document.getElementById('message').value
            };

            const response = await fetch('/http/contact', {
                method: 'POST',
                headers: {
                    'Content-Type': 'application/json'
                },
                body: JSON.stringify(formData)
            });

            if (response.ok) {
                formFields.forEach(field => field.classList.add('hidden'));
                thankYou.classList.remove('hidden');
            } else {
                throw new Error('Submission failed');
            }
        } catch (error) {
            console.error('Form submission error:', error);
            submitBtn.disabled = false;
            submitBtn.textContent = 'Send Message';
            alert('Something went wrong. Please try again.');
        }
    });
}

// Initialize contact form when DOM is ready
if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', initContactForm);
} else {
    initContactForm();
}
