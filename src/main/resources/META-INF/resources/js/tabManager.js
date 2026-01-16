// Tab Manager Module
export class TabManager {
    constructor(rootEl) {
        this.rootEl = rootEl;
        this.tabs = [];
        this.activeTab = null;
        this.isDesktop = window.innerWidth >= 1024; // lg breakpoint

        this.init();
        this.setupResizeHandler();
    }

    init() {
        // Find all tab buttons
        const tabButtons = this.rootEl.querySelectorAll('[data-tab]');

        tabButtons.forEach(button => {
            const tabId = button.getAttribute('data-tab');
            const panelId = button.getAttribute('aria-controls');
            const panel = document.getElementById(panelId);

            if (!panel) {
                console.warn(`[tab-manager] Panel not found: ${panelId}`);
                return;
            }

            const tab = {
                id: tabId,
                button: button,
                panel: panel,
                isActive: button.getAttribute('aria-selected') === 'true'
            };

            this.tabs.push(tab);

            // Add click event
            button.addEventListener('click', () => this.switchTab(tabId));
        });

        // Apply initial state based on screen size
        this.updateLayoutForScreenSize();

        console.log(`[tab-manager] Initialized ${this.tabs.length} tabs`);
    }

    setupResizeHandler() {
        let resizeTimeout;
        window.addEventListener('resize', () => {
            clearTimeout(resizeTimeout);
            resizeTimeout = setTimeout(() => {
                const wasDesktop = this.isDesktop;
                this.isDesktop = window.innerWidth >= 1024;

                if (wasDesktop !== this.isDesktop) {
                    this.updateLayoutForScreenSize();
                }
            }, 150);
        });
    }

    updateLayoutForScreenSize() {
        if (this.isDesktop) {
            // Desktop: show both panels
            this.tabs.forEach(tab => {
                tab.panel.classList.remove('hidden');
                // Don't change button states on desktop as tabs are hidden
            });
            console.log('[tab-manager] Desktop mode: showing both panels');
        } else {
            // Mobile: use tab switching
            if (!this.activeTab && this.tabs.length > 0) {
                this.switchTab(this.tabs[0].id);
            } else if (this.activeTab) {
                // Restore the active tab state
                this.switchTab(this.activeTab.id);
            }
            console.log('[tab-manager] Mobile mode: using tab switching');
        }
    }

    switchTab(tabId) {
        // On desktop, don't switch tabs - both are always visible
        if (this.isDesktop) {
            console.log(`[tab-manager] Desktop mode: ignoring tab switch to ${tabId}`);
            return;
        }

        const targetTab = this.tabs.find(tab => tab.id === tabId);

        if (!targetTab) {
            console.warn(`[tab-manager] Tab not found: ${tabId}`);
            return;
        }

        if (targetTab === this.activeTab) {
            return; // Already active
        }

        // Deactivate current tab
        if (this.activeTab) {
            this.activeTab.button.setAttribute('aria-selected', 'false');
            this.activeTab.button.classList.remove('tab-button--active');
            this.activeTab.panel.classList.add('hidden');
        }

        // Activate new tab
        targetTab.button.setAttribute('aria-selected', 'true');
        targetTab.button.classList.add('tab-button--active');
        targetTab.panel.classList.remove('hidden');

        this.activeTab = targetTab;

        // Dispatch custom event
        const event = new CustomEvent('tab-switched', {
            detail: { tabId: tabId, tab: targetTab }
        });
        this.rootEl.dispatchEvent(event);

        console.log(`[tab-manager] Switched to tab: ${tabId}`);
    }

    getActiveTab() {
        return this.activeTab;
    }

    getTab(tabId) {
        return this.tabs.find(tab => tab.id === tabId);
    }
}