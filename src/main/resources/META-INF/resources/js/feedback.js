// Feedback Modal Module
// Handles the feedback/issue reporting modal functionality
// Follows the component pattern: export init(rootEl) that wires up events

const FEEDBACK_STORAGE_KEY = 'filesurf_feedback_count';
const MAX_FEEDBACK_PER_DAY = 5;

let feedbackModuleInstance = null;

/**
 * Check if user can submit more feedback today
 */
function canSubmitFeedback() {
    try {
        const today = new Date().toDateString();
        const stored = localStorage.getItem(FEEDBACK_STORAGE_KEY);

        if (!stored) return true;

        const data = JSON.parse(stored);
        if (data.date !== today) {
            // Reset count for new day
            localStorage.removeItem(FEEDBACK_STORAGE_KEY);
            return true;
        }

        return data.count < MAX_FEEDBACK_PER_DAY;
    } catch (e) {
        return true;
    }
}

/**
 * Increment feedback count
 */
function incrementFeedbackCount() {
    try {
        const today = new Date().toDateString();
        const stored = localStorage.getItem(FEEDBACK_STORAGE_KEY);
        let data = { date: today, count: 0 };

        if (stored) {
            const parsed = JSON.parse(stored);
            if (parsed.date === today) {
                data = parsed;
            }
        }

        data.count += 1;
        localStorage.setItem(FEEDBACK_STORAGE_KEY, JSON.stringify(data));
    } catch (e) {
        // Ignore storage errors
    }
}

/**
 * Collect system and browser information for error reports
 */
function collectEnvironmentInfo() {
    const info = {
        userAgent: navigator.userAgent,
        language: navigator.language,
        platform: navigator.platform,
        screenSize: `${window.screen.width}x${window.screen.height}`,
        viewportSize: `${window.innerWidth}x${window.innerHeight}`,
        timestamp: new Date().toISOString(),
        url: window.location.href,
        // Session storage check
        sessionStorage: typeof sessionStorage !== 'undefined',
        localStorage: typeof localStorage !== 'undefined',
    };

    // Try to get more specific info
    try {
        info.cookiesEnabled = navigator.cookieEnabled;
    } catch (e) {
        info.cookiesEnabled = 'unknown';
    }

    return info;
}

/**
 * Show toast notification
 */
function showToast(message, type = 'info') {
    // Dispatch custom event that other modules can listen to
    const event = new CustomEvent('filesurf-toast', {
        detail: { message, type }
    });
    window.dispatchEvent(event);

    // Also create a simple fallback toast if no listener
    const toastId = 'feedback-toast-' + Date.now();
    const toast = document.createElement('div');
    toast.id = toastId;
    toast.className = `fixed bottom-4 right-4 z-[100] px-4 py-3 rounded-lg shadow-lg text-white font-medium animate-fade-in ${
        type === 'success' ? 'bg-green-600' :
        type === 'error' ? 'bg-red-600' :
        'bg-blue-600'
    }`;
    toast.textContent = message;
    document.body.appendChild(toast);

    setTimeout(() => {
        toast.remove();
    }, 3000);
}

/**
 * Submit feedback to the server
 */
async function submitFeedback(data) {
    const feedbackData = {
        type: data.type,
        description: data.description,
        email: data.email || null,
        errorDetails: data.errorDetails || null,
        environment: collectEnvironmentInfo(),
        sessionId: sessionStorage.getItem('filesurf_sessionId') || null
    };

    try {
        const response = await fetch('/file-chat/http/feedback', {
            method: 'POST',
            headers: {
                'Content-Type': 'application/json'
            },
            body: JSON.stringify(feedbackData)
        });

        if (!response.ok) {
            const errorText = await response.text();
            throw new Error(errorText || `HTTP ${response.status}`);
        }

        const result = await response.json();
        return result;
    } catch (error) {
        console.error('[feedback] Failed to submit feedback:', error);
        throw error;
    }
}

class FeedbackModule {
    constructor() {
        this.modal = null;
        this.openButton = null;
        this.backdrop = null;
        this.panel = null;
        this.closeBtn = null;
        this.cancelBtn = null;
        this.form = null;
        this.successState = null;
        this.doneBtn = null;
        this.descriptionInput = null;
        this.errorSection = null;
        this.errorDetailsInput = null;
        this.typeButtons = null;
        this.submitBtn = null;
        this.submitText = null;
        this.submitLoading = null;

        this.currentType = 'bug';
        this.isOpen = false;
        this.initialized = false;
    }

    init() {
        if (this.initialized) return;

        // Find all elements in the document
        this.modal = document.querySelector('[data-feedback-modal]');
        this.openButton = document.querySelector('[data-feedback-button]');
        this.backdrop = document.querySelector('[data-feedback-backdrop]');
        this.panel = document.querySelector('[data-feedback-panel]');
        this.closeBtn = document.querySelector('[data-feedback-close]');
        this.cancelBtn = document.querySelector('[data-feedback-cancel]');
        this.form = document.querySelector('[data-feedback-form]');
        this.successState = document.querySelector('[data-feedback-success]');
        this.doneBtn = document.querySelector('[data-feedback-done]');
        this.descriptionInput = document.querySelector('[data-feedback-description]');
        this.errorSection = document.querySelector('[data-feedback-error-section]');
        this.errorDetailsInput = document.querySelector('[data-feedback-error-details]');
        this.typeButtons = document.querySelectorAll('[data-feedback-type]');
        this.submitBtn = document.querySelector('[data-feedback-submit]');
        this.submitText = this.submitBtn?.querySelector('[data-feedback-submit-text]');
        this.submitLoading = this.submitBtn?.querySelector('[data-feedback-submit-loading]');

        if (!this.modal) {
            console.warn('[feedback] Modal element not found');
            return;
        }

        this.bindEvents();
        this.initialized = true;
        console.log('[feedback] Feedback module initialized');
    }

    bindEvents() {
        // Type button selection
        this.typeButtons.forEach(btn => {
            btn.addEventListener('click', () => this.selectType(btn));
        });

        // Open modal
        this.openButton?.addEventListener('click', () => this.openModal());

        // Close modal
        this.closeBtn?.addEventListener('click', () => this.closeModal());
        this.cancelBtn?.addEventListener('click', () => this.closeModal());
        this.doneBtn?.addEventListener('click', () => this.closeModal());

        // Close on backdrop click
        this.backdrop?.addEventListener('click', () => this.closeModal());

        // Handle form submission
        this.form?.addEventListener('submit', (e) => this.handleSubmit(e));

        // Close on Escape key
        document.addEventListener('keydown', (e) => {
            if (e.key === 'Escape' && this.isOpen) {
                this.closeModal();
            }
        });

        // Prevent panel click from closing modal
        this.panel?.addEventListener('click', (e) => {
            e.stopPropagation();
        });
    }

    selectType(btn) {
        this.typeButtons.forEach(b => {
            b.classList.remove('active', 'ring-2', 'ring-orange-500', 'bg-orange-50', 'dark:bg-orange-900/20');
            b.setAttribute('aria-checked', 'false');
        });
        btn.classList.add('active', 'ring-2', 'ring-orange-500', 'bg-orange-50', 'dark:bg-orange-900/20');
        btn.setAttribute('aria-checked', 'true');
        this.currentType = btn.dataset.feedbackType;

        // Show/hide error section based on type
        if (this.currentType === 'bug') {
            this.errorSection?.classList.remove('hidden');
        } else {
            this.errorSection?.classList.add('hidden');
        }
    }

    openModal() {
        if (!this.modal) return;

        if (!canSubmitFeedback()) {
            showToast('You have reached the maximum number of feedback submissions for today. Please try again tomorrow.', 'error');
            return;
        }

        // Reset form
        this.form?.reset();
        this.errorSection?.classList.add('hidden');
        this.successState?.classList.add('hidden');
        this.form?.classList.remove('hidden');

        // Set default type
        this.currentType = 'bug';
        this.typeButtons.forEach(btn => {
            if (btn.dataset.feedbackType === 'bug') {
                btn.classList.add('active', 'ring-2', 'ring-orange-500', 'bg-orange-50', 'dark:bg-orange-900/20');
                btn.setAttribute('aria-checked', 'true');
            } else {
                btn.classList.remove('active', 'ring-2', 'ring-orange-500', 'bg-orange-50', 'dark:bg-orange-900/20');
                btn.setAttribute('aria-checked', 'false');
            }
        });

        this.modal.classList.remove('hidden');
        document.body.style.overflow = 'hidden';

        // Focus on description textarea
        setTimeout(() => {
            this.descriptionInput?.focus();
        }, 100);

        this.isOpen = true;
    }

    closeModal() {
        if (!this.modal) return;
        this.modal.classList.add('hidden');
        document.body.style.overflow = '';
        this.isOpen = false;
    }

    async handleSubmit(e) {
        e.preventDefault();

        const description = this.descriptionInput?.value.trim();
        if (!description) {
            this.descriptionInput?.focus();
            showToast('Please enter a description', 'error');
            return;
        }

        // Show loading state
        this.submitBtn.disabled = true;
        this.submitText?.classList.add('hidden');
        this.submitLoading?.classList.remove('hidden');

        try {
            await submitFeedback({
                type: this.currentType,
                description: description,
                email: document.querySelector('[data-feedback-email]')?.value.trim() || null,
                errorDetails: this.currentType === 'bug' ? (this.errorDetailsInput?.value.trim() || null) : null
            });

            // Success
            incrementFeedbackCount();
            this.form?.classList.add('hidden');
            this.successState?.classList.remove('hidden');
            showToast('Thank you for your feedback!', 'success');

        } catch (error) {
            showToast('Failed to submit feedback. Please try again.', 'error');
            console.error('[feedback] Submission error:', error);
        } finally {
            this.submitBtn.disabled = false;
            this.submitText?.classList.remove('hidden');
            this.submitLoading?.classList.add('hidden');
        }
    }
}

export function init(rootEl) {
    // Create or get the singleton instance
    if (!feedbackModuleInstance) {
        feedbackModuleInstance = new FeedbackModule();
    }
    feedbackModuleInstance.init();
}

// Auto-init when DOM is ready
function autoInit() {
    const modal = document.querySelector('[data-feedback-modal]');
    const button = document.querySelector('[data-feedback-button]');

    if (modal || button) {
        init();
    }
}

if (typeof document !== 'undefined') {
    if (document.readyState === 'loading') {
        document.addEventListener('DOMContentLoaded', autoInit);
    } else {
        autoInit();
    }
}

// Expose globally for programmatic access
if (typeof window !== 'undefined') {
    window.FilesurfFeedback = {
        open: () => feedbackModuleInstance?.openModal(),
        close: () => feedbackModuleInstance?.closeModal(),
        canSubmit: canSubmitFeedback
    };
}
