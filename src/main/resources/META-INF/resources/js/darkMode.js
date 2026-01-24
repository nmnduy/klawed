/**
 * Dark Mode Utility
 * Handles theme switching and persistence using localStorage
 */

const THEME_STORAGE_KEY = 'filesurf-theme';
const DARK_CLASS = 'dark';

export class DarkModeManager {
    constructor() {
        this.theme = this.getStoredTheme() || this.getSystemTheme();
        this.listeners = [];
    }

    /**
     * Get theme from localStorage
     */
    getStoredTheme() {
        try {
            return localStorage.getItem(THEME_STORAGE_KEY);
        } catch (error) {
            console.warn('[dark-mode] Failed to read theme from localStorage:', error);
            return null;
        }
    }

    /**
     * Get system theme preference
     */
    getSystemTheme() {
        if (window.matchMedia && window.matchMedia('(prefers-color-scheme: dark)').matches) {
            return 'dark';
        }
        return 'light';
    }

    /**
     * Save theme to localStorage
     */
    saveTheme(theme) {
        try {
            localStorage.setItem(THEME_STORAGE_KEY, theme);
        } catch (error) {
            console.warn('[dark-mode] Failed to save theme to localStorage:', error);
        }
    }

    /**
     * Apply theme to document
     */
    applyTheme(theme) {
        const html = document.documentElement;

        if (theme === 'dark') {
            html.classList.add(DARK_CLASS);
        } else {
            html.classList.remove(DARK_CLASS);
        }

        this.theme = theme;
        this.notifyListeners(theme);
    }

    /**
     * Set theme and persist to localStorage
     */
    setTheme(theme) {
        this.applyTheme(theme);
        this.saveTheme(theme);
    }

    /**
     * Toggle between light and dark themes
     */
    toggle() {
        const newTheme = this.theme === 'dark' ? 'light' : 'dark';
        this.setTheme(newTheme);
        return newTheme;
    }

    /**
     * Get current theme
     */
    getTheme() {
        return this.theme;
    }

    /**
     * Check if dark mode is active
     */
    isDark() {
        return this.theme === 'dark';
    }

    /**
     * Initialize theme on page load
     */
    init() {
        this.applyTheme(this.theme);

        // Listen for system theme changes
        if (window.matchMedia) {
            window.matchMedia('(prefers-color-scheme: dark)').addEventListener('change', (e) => {
                // Only update if user hasn't set a preference
                if (!this.getStoredTheme()) {
                    this.applyTheme(e.matches ? 'dark' : 'light');
                }
            });
        }
    }

    /**
     * Add listener for theme changes
     */
    addListener(callback) {
        this.listeners.push(callback);
    }

    /**
     * Remove listener
     */
    removeListener(callback) {
        this.listeners = this.listeners.filter(l => l !== callback);
    }

    /**
     * Notify all listeners of theme change
     */
    notifyListeners(theme) {
        this.listeners.forEach(callback => {
            try {
                callback(theme);
            } catch (error) {
                console.error('[dark-mode] Error in listener:', error);
            }
        });
    }
}

// Create singleton instance
export const darkMode = new DarkModeManager();

// Initialize immediately to prevent flash of wrong theme
darkMode.init();

// Export for global access if needed
window.darkMode = darkMode;

/**
 * Initialize the theme toggle button(s)
 * Should be called after DOM is ready
 * Handles all toggle buttons (desktop and mobile)
 */
export function initThemeToggle() {
    const toggleButtons = document.querySelectorAll('[data-theme-toggle]');
    console.log('[dark-mode] initThemeToggle called, buttons found:', toggleButtons.length);
    if (toggleButtons.length === 0) {
        console.warn('[dark-mode] No toggle buttons found');
        return;
    }

    // Update all button states based on current theme
    console.log('[dark-mode] Current theme:', darkMode.getTheme());
    toggleButtons.forEach(button => {
        updateToggleButtonState(button, darkMode.getTheme());
    });

    // Listen for theme changes to update all button states
    darkMode.addListener((theme) => {
        console.log('[dark-mode] Theme changed to:', theme);
        toggleButtons.forEach(button => {
            updateToggleButtonState(button, theme);
        });
    });

    // Add click handler to each button
    toggleButtons.forEach(button => {
        button.addEventListener('click', () => {
            console.log('[dark-mode] Toggle button clicked, current theme:', darkMode.getTheme());
            button.setAttribute('data-transitioning', 'true');
            const newTheme = darkMode.toggle();
            console.log('[dark-mode] After toggle, new theme:', newTheme);

            // Remove transition state after animation
            setTimeout(() => {
                button.removeAttribute('data-transitioning');
            }, 300);
        });
    });
}

/**
 * Update the toggle button visual state based on current theme
 * Shows/hides moon or sun icon appropriately
 */
function updateToggleButtonState(button, theme) {
    if (!button) return;

    if (theme === 'dark') {
        button.classList.add('dark');
    } else {
        button.classList.remove('dark');
    }
}

// Auto-initialize if DOM is already ready, otherwise wait for DOMContentLoaded
if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', initThemeToggle);
} else {
    // DOM is already ready, but we need to wait a tick for other scripts
    setTimeout(initThemeToggle, 0);
}
