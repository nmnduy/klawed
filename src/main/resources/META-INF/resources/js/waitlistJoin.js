/**
 * Waitlist Join Form Handler
 * Handles form submission for the waitlist join section
 */

document.addEventListener('DOMContentLoaded', function() {
    const form = document.getElementById('waitlist-form');
    const submitButton = document.getElementById('waitlist-submit');
    const messageDiv = document.getElementById('waitlist-message');

    if (!form) {
        return; // Form not present on this page
    }

    form.addEventListener('submit', async function(e) {
        e.preventDefault();

        // Disable submit button
        submitButton.disabled = true;
        submitButton.textContent = 'Joining...';

        // Clear previous messages
        messageDiv.classList.add('hidden');
        messageDiv.className = 'hidden p-4 rounded-lg';

        // Get form data
        const formData = {
            email: document.getElementById('waitlist-email').value.trim(),
            name: document.getElementById('waitlist-name').value.trim() || null,
            useCase: document.getElementById('waitlist-usecase').value.trim() || null
        };

        try {
            const response = await fetch('/waitlist', {
                method: 'POST',
                headers: {
                    'Content-Type': 'application/json'
                },
                body: JSON.stringify(formData)
            });

            const data = await response.json();

            if (response.ok && data.success) {
                // Success message
                messageDiv.textContent = '✓ Success! You\'ve been added to the waitlist. We\'ll notify you soon.';
                messageDiv.classList.remove('hidden');
                messageDiv.classList.add('bg-semantic-success/10', 'text-semantic-success', 'border-2', 'border-semantic-success/30');
                
                // Clear form
                form.reset();
            } else {
                // Error message from server
                const errorMessage = data.error || 'Failed to join waitlist. Please try again.';
                messageDiv.textContent = '✗ ' + errorMessage;
                messageDiv.classList.remove('hidden');
                messageDiv.classList.add('bg-semantic-error/10', 'text-semantic-error', 'border-2', 'border-semantic-error/30');
            }
        } catch (error) {
            console.error('Waitlist submission error:', error);
            messageDiv.textContent = '✗ Network error. Please check your connection and try again.';
            messageDiv.classList.remove('hidden');
            messageDiv.classList.add('bg-semantic-error/10', 'text-semantic-error', 'border-2', 'border-semantic-error/30');
        } finally {
            // Re-enable submit button
            submitButton.disabled = false;
            submitButton.textContent = 'Join Waitlist';
        }
    });

    // Real-time email validation
    const emailInput = document.getElementById('waitlist-email');
    emailInput.addEventListener('blur', function() {
        if (this.value && !this.validity.valid) {
            this.classList.add('border-semantic-error');
            this.classList.remove('border-border');
        } else {
            this.classList.remove('border-semantic-error');
            this.classList.add('border-border');
        }
    });
});
