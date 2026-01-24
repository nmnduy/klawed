/**
 * Mobile Menu Manager
 * Handles the three-dot menu dropdown on mobile devices
 */

export function initMobileMenu(rootEl) {
    const menuButton = rootEl.querySelector('[data-mobile-menu-button]');
    const menu = rootEl.querySelector('[data-mobile-menu]');
    
    if (!menuButton || !menu) {
        console.log('[mobile-menu] Menu elements not found');
        return;
    }

    // Toggle menu on button click
    menuButton.addEventListener('click', (e) => {
        e.stopPropagation();
        const isOpen = menu.classList.contains('hidden');
        
        if (isOpen) {
            menu.classList.remove('hidden');
            menuButton.setAttribute('aria-expanded', 'true');
        } else {
            menu.classList.add('hidden');
            menuButton.setAttribute('aria-expanded', 'false');
        }
    });

    // Close menu when clicking outside
    document.addEventListener('click', (e) => {
        if (!menu.classList.contains('hidden') && 
            !menu.contains(e.target) && 
            !menuButton.contains(e.target)) {
            menu.classList.add('hidden');
            menuButton.setAttribute('aria-expanded', 'false');
        }
    });

    // Close menu when clicking a menu item
    const menuItems = menu.querySelectorAll('a, button');
    menuItems.forEach(item => {
        item.addEventListener('click', () => {
            menu.classList.add('hidden');
            menuButton.setAttribute('aria-expanded', 'false');
        });
    });

    // Close menu on escape key
    document.addEventListener('keydown', (e) => {
        if (e.key === 'Escape' && !menu.classList.contains('hidden')) {
            menu.classList.add('hidden');
            menuButton.setAttribute('aria-expanded', 'false');
            menuButton.focus();
        }
    });

    console.log('[mobile-menu] Initialized');
}
