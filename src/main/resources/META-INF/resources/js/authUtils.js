// Authentication utilities for FileSurf
// Handles authentication errors and redirects

/**
 * Check if a fetch response indicates an authentication error
 * @param {Response} response - The fetch response
 * @returns {boolean} - True if authentication is required
 */
export function isAuthRequired(response) {
    return response.status === 401 || response.status === 403;
}

/**
 * Handle authentication error by redirecting to login
 * @param {Response} response - The fetch response
 * @param {string} currentPath - Current page path (optional, defaults to window.location.pathname)
 */
export async function handleAuthError(response, currentPath = null) {
    if (!isAuthRequired(response)) {
        return false;
    }

    const path = currentPath || window.location.pathname;
    let redirectUrl = '/auth/login';

    try {
        const errorData = await response.json();
        if (errorData.redirect) {
            redirectUrl = errorData.redirect;
        }
    } catch (e) {
        // Ignore JSON parse errors, use default redirect
    }

    // Redirect to login with return URL
    window.location.href = redirectUrl + '?redirect=' + encodeURIComponent(path);
    return true;
}

/**
 * Wrapper around fetch that handles authentication errors
 * @param {string} url - The URL to fetch
 * @param {object} options - Fetch options
 * @returns {Promise<Response>} - The fetch response
 */
export async function authFetch(url, options = {}) {
    const response = await fetch(url, options);

    if (isAuthRequired(response)) {
        await handleAuthError(response);
        // Return a never-resolving promise since we're redirecting
        return new Promise(() => {});
    }

    return response;
}

/**
 * Check authentication status
 * @returns {Promise<object>} - Authentication status object
 */
export async function checkAuthStatus() {
    try {
        const response = await fetch('/auth/status');
        if (!response.ok) {
            return { authenticated: false, reason: 'error' };
        }
        return await response.json();
    } catch (e) {
        return { authenticated: false, reason: 'error', error: e.message };
    }
}
