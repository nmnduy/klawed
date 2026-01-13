const SELECTORS = {
  loginForm: '[data-role="login-form"]',
  waitlistForm: '[data-role="waitlist-form"]',
  waitlistToggle: '[data-role="waitlist-toggle"]',
  waitlistPanel: '[data-role="waitlist-panel"]',
  waitlistSuccess: '[data-role="waitlist-success"]',
  waitlistError: '[data-role="waitlist-error"]',
  waitlistSubmit: '[data-role="waitlist-submit"]',
  waitlistEmail: '[data-role="waitlist-email"]',
  waitlistName: '[data-role="waitlist-name"]',
  waitlistUseCase: '[data-role="waitlist-usecase"]',
};

function setSubmitting(buttonEl, isSubmitting) {
  if (!buttonEl) return;
  const textEl = buttonEl.querySelector('[data-role="btn-text"]');
  const spinnerEl = buttonEl.querySelector('[data-role="btn-spinner"]');
  buttonEl.disabled = isSubmitting;
  if (textEl) textEl.classList.toggle('hidden', isSubmitting);
  if (spinnerEl) spinnerEl.classList.toggle('hidden', !isSubmitting);
}

function showMessage(el, message) {
  if (!el) return;
  el.textContent = message;
  el.classList.remove('hidden');
}

function hideMessage(el) {
  if (!el) return;
  el.classList.add('hidden');
  el.textContent = '';
}

function validateEmail(value) {
  if (!value) return false;
  const trimmed = value.trim();
  const at = trimmed.indexOf('@');
  if (at < 1) return false;
  const domain = trimmed.substring(at + 1);
  return domain.includes('.') && domain.indexOf('.') < domain.length - 1;
}

async function handleWaitlistSubmit(event) {
  const form = event.currentTarget;
  const emailInput = form.querySelector(SELECTORS.waitlistEmail);
  const nameInput = form.querySelector(SELECTORS.waitlistName);
  const useCaseInput = form.querySelector(SELECTORS.waitlistUseCase);
  const submitBtn = form.querySelector(SELECTORS.waitlistSubmit);
  const errorEl = form.querySelector(SELECTORS.waitlistError);
  const successEl = form.querySelector(SELECTORS.waitlistSuccess);

  hideMessage(errorEl);
  hideMessage(successEl);

  const email = emailInput ? emailInput.value.trim() : '';
  const name = nameInput ? nameInput.value.trim() : '';
  const useCase = useCaseInput ? useCaseInput.value.trim() : '';

  if (!email || !validateEmail(email)) {
    event.preventDefault();
    showMessage(errorEl, 'Please enter a valid email address');
    emailInput?.focus();
    return;
  }

  event.preventDefault();
  setSubmitting(submitBtn, true);

  try {
    const res = await fetch('/waitlist', {
      method: 'POST',
      headers: {
        'Content-Type': 'application/json',
        'Accept': 'application/json',
      },
      body: JSON.stringify({ email, name: name || null, useCase: useCase || null }),
    });

    if (!res.ok) {
      const data = await res.json().catch(() => ({}));
      const message = data.error || 'Failed to join the waitlist. Please try again.';
      showMessage(errorEl, message);
      return;
    }

    showMessage(successEl, 'You are on the waitlist. We will reach out soon.');
    if (form.closest('dialog')) {
      // no-op
    }
    form.reset();
  } catch (err) {
    showMessage(errorEl, 'Something went wrong. Please try again.');
  } finally {
    setSubmitting(submitBtn, false);
  }
}

export function init(rootEl = document) {
  const waitlistForm = rootEl.querySelector(SELECTORS.waitlistForm);
  const toggleBtn = rootEl.querySelector(SELECTORS.waitlistToggle);
  const waitlistPanel = rootEl.querySelector(SELECTORS.waitlistPanel);

  if (toggleBtn && waitlistPanel) {
    toggleBtn.addEventListener('click', () => {
      waitlistPanel.classList.toggle('hidden');
      if (!waitlistPanel.classList.contains('hidden')) {
        const emailInput = waitlistPanel.querySelector(SELECTORS.waitlistEmail);
        emailInput?.focus();
      }
    });
  }

  if (waitlistForm) {
    waitlistForm.addEventListener('submit', handleWaitlistSubmit);
  }
}

export default { init };
