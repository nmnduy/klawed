// Tab Manager Module
export class TabManager {
    constructor(rootEl) {
        this.rootEl = rootEl;
        this.tabs = [];
        this.activeTab = null;
        
        this.init();
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
            
            // Set initial state
            if (tab.isActive) {
                this.activeTab = tab;
                panel.classList.remove('hidden');
                button.classList.add('tab-button--active');
            } else {
                panel.classList.add('hidden');
                button.classList.remove('tab-button--active');
            }
            
            // Add click event
            button.addEventListener('click', () => this.switchTab(tabId));
        });
        
        // Set default active tab if none is active
        if (!this.activeTab && this.tabs.length > 0) {
            this.switchTab(this.tabs[0].id);
        }
        
        console.log(`[tab-manager] Initialized ${this.tabs.length} tabs`);
    }
    
    switchTab(tabId) {
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