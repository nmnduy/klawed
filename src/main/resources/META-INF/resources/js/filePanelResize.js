// File Panel Resize and Collapse Module

class FilePanelResize {
    constructor() {
        // DOM Elements
        this.filePanel = document.querySelector('[data-file-panel]');
        this.resizeHandle = document.querySelector('[data-resize-handle]');
        this.collapseButton = document.querySelector('[data-collapse-button]');
        this.collapseIcon = document.querySelector('[data-collapse-icon]');
        this.chatPanel = document.getElementById('chat-panel');

        // State
        this.isResizing = false;
        this.isCollapsed = false;
        this.savedWidth = 400; // Default width
        this.minWidth = 250;
        this.maxWidth = 800;

        // Storage key for persisting width and collapsed state
        this.STORAGE_KEY_WIDTH = 'filesurf-file-panel-width';
        this.STORAGE_KEY_COLLAPSED = 'filesurf-file-panel-collapsed';

        // Bound methods (to maintain correct 'this' and allow proper event listener removal)
        this.boundHandleResize = this.handleResize.bind(this);
        this.boundStopResize = this.stopResize.bind(this);

        // Initialize
        this.init();
    }

    init() {
        if (!this.filePanel || !this.resizeHandle || !this.collapseButton) {
            return;
        }

        // Restore saved state from localStorage
        this.restoreState();

        // Bind event listeners
        this.resizeHandle.addEventListener('mousedown', this.startResize.bind(this));
        this.collapseButton.addEventListener('click', this.toggleCollapse.bind(this));

        // Handle window resize to ensure constraints
        window.addEventListener('resize', this.handleWindowResize.bind(this));
    }

    restoreState() {
        try {
            // Restore width
            const savedWidth = localStorage.getItem(this.STORAGE_KEY_WIDTH);
            if (savedWidth) {
                const width = parseInt(savedWidth, 10);
                if (width >= this.minWidth && width <= this.maxWidth) {
                    this.savedWidth = width;
                    this.filePanel.style.width = `${width}px`;
                }
            }

            // Restore collapsed state
            const savedCollapsed = localStorage.getItem(this.STORAGE_KEY_COLLAPSED);
            if (savedCollapsed === 'true') {
                this.collapse(false); // false = don't animate on page load
            }
        } catch (error) {
            console.error('Failed to restore file panel state:', error);
        }
    }

    startResize(e) {
        if (this.isCollapsed) {
            return; // Don't allow resize when collapsed
        }

        this.isResizing = true;
        document.body.style.cursor = 'col-resize';
        document.body.style.userSelect = 'none';

        // Add event listeners for mousemove and mouseup
        document.addEventListener('mousemove', this.boundHandleResize);
        document.addEventListener('mouseup', this.boundStopResize);

        e.preventDefault();
    }

    handleResize(e) {
        if (!this.isResizing) {
            return;
        }

        // Calculate new width based on mouse position
        const containerRect = this.filePanel.parentElement.getBoundingClientRect();
        const newWidth = containerRect.right - e.clientX;

        // Constrain width between min and max
        const constrainedWidth = Math.max(this.minWidth, Math.min(this.maxWidth, newWidth));

        // Apply new width
        this.filePanel.style.width = `${constrainedWidth}px`;
        this.savedWidth = constrainedWidth;

        e.preventDefault();
    }

    stopResize() {
        if (!this.isResizing) {
            return;
        }

        this.isResizing = false;
        document.body.style.cursor = '';
        document.body.style.userSelect = '';

        // Remove event listeners
        document.removeEventListener('mousemove', this.boundHandleResize);
        document.removeEventListener('mouseup', this.boundStopResize);

        // Save width to localStorage
        try {
            localStorage.setItem(this.STORAGE_KEY_WIDTH, this.savedWidth.toString());
        } catch (error) {
            console.error('Failed to save file panel width:', error);
        }
    }

    toggleCollapse() {
        if (this.isCollapsed) {
            this.expand();
        } else {
            this.collapse();
        }
    }

    collapse(animate = true) {
        this.isCollapsed = true;

        if (!animate) {
            // Disable transitions temporarily for instant collapse
            this.filePanel.style.transition = 'none';
            this.chatPanel.style.transition = 'none';
        }

        // Hide file panel and adjust chat panel
        this.filePanel.style.width = '0';
        this.filePanel.style.minWidth = '0';
        this.resizeHandle.style.display = 'none';

        // Rotate icon to point left (collapsed state)
        this.collapseIcon.style.transform = 'rotate(180deg)';
        this.collapseButton.title = 'Expand file explorer';

        // Position collapse button on the right edge when collapsed
        this.collapseButton.style.left = 'auto';
        this.collapseButton.style.right = '-12px';

        if (!animate) {
            // Re-enable transitions after a frame
            requestAnimationFrame(() => {
                this.filePanel.style.transition = '';
                this.chatPanel.style.transition = '';
            });
        }

        // Save collapsed state
        try {
            localStorage.setItem(this.STORAGE_KEY_COLLAPSED, 'true');
        } catch (error) {
            console.error('Failed to save collapsed state:', error);
        }
    }

    expand() {
        this.isCollapsed = false;

        // Show file panel with saved width
        this.filePanel.style.width = `${this.savedWidth}px`;
        this.filePanel.style.minWidth = `${this.minWidth}px`;
        this.resizeHandle.style.display = '';

        // Rotate icon to point right (expanded state)
        this.collapseIcon.style.transform = 'rotate(0deg)';
        this.collapseButton.title = 'Collapse file explorer';

        // Position collapse button on the left edge when expanded
        this.collapseButton.style.left = '-12px';
        this.collapseButton.style.right = 'auto';

        // Save expanded state
        try {
            localStorage.setItem(this.STORAGE_KEY_COLLAPSED, 'false');
        } catch (error) {
            console.error('Failed to save collapsed state:', error);
        }
    }

    handleWindowResize() {
        // Ensure panel width doesn't exceed constraints on window resize
        if (!this.isCollapsed && this.filePanel) {
            const currentWidth = this.filePanel.offsetWidth;
            if (currentWidth > this.maxWidth) {
                this.filePanel.style.width = `${this.maxWidth}px`;
                this.savedWidth = this.maxWidth;
            } else if (currentWidth < this.minWidth) {
                this.filePanel.style.width = `${this.minWidth}px`;
                this.savedWidth = this.minWidth;
            }
        }
    }
}

// Initialize when DOM is ready
if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', () => {
        new FilePanelResize();
    });
} else {
    new FilePanelResize();
}
