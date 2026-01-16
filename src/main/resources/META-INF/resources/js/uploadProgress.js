/**
 * Upload Progress UI Component
 * Provides a reusable progress bar for file uploads
 */

class UploadProgress {
    constructor(options = {}) {
        this.containerId = options.containerId || 'upload-progress-container';
        this.container = null;
        this.progressBars = new Map(); // Map<uploadId, element>
    }

    /**
     * Show progress for a file upload
     * @param {string} uploadId - Unique identifier for this upload
     * @param {string} fileName - Name of the file being uploaded
     * @param {number} totalSize - Total file size in bytes
     */
    showProgress(uploadId, fileName, totalSize) {
        // Create container if it doesn't exist
        if (!this.container) {
            this.createContainer();
        }

        // Create progress bar element
        const progressBar = this.createProgressBar(uploadId, fileName, totalSize);
        this.progressBars.set(uploadId, progressBar);
        this.container.appendChild(progressBar);

        return progressBar;
    }

    /**
     * Update progress for a specific upload
     * @param {string} uploadId - Unique identifier for this upload
     * @param {number} progress - Progress percentage (0-100)
     * @param {number} uploadedSize - Bytes uploaded so far
     */
    updateProgress(uploadId, progress, uploadedSize) {
        const progressBar = this.progressBars.get(uploadId);
        if (!progressBar) return;

        const progressFill = progressBar.querySelector('[data-progress-fill]');
        const progressText = progressBar.querySelector('[data-progress-text]');
        const progressSize = progressBar.querySelector('[data-progress-size]');

        if (progressFill) {
            progressFill.style.width = `${progress}%`;
        }

        if (progressText) {
            progressText.textContent = `${Math.round(progress)}%`;
        }

        if (progressSize) {
            progressSize.textContent = this.formatBytes(uploadedSize);
        }
    }

    /**
     * Mark upload as complete
     * @param {string} uploadId - Unique identifier for this upload
     * @param {boolean} success - Whether upload succeeded
     */
    completeUpload(uploadId, success = true) {
        const progressBar = this.progressBars.get(uploadId);
        if (!progressBar) return;

        if (success) {
            // Show success state briefly, then remove
            const progressFill = progressBar.querySelector('[data-progress-fill]');
            if (progressFill) {
                progressFill.classList.remove('bg-orange-500');
                progressFill.classList.add('bg-green-500');
            }

            const progressText = progressBar.querySelector('[data-progress-text]');
            if (progressText) {
                progressText.textContent = '✓ Complete';
            }

            // Remove after 2 seconds
            setTimeout(() => {
                this.removeProgress(uploadId);
            }, 2000);
        } else {
            // Show error state
            const progressFill = progressBar.querySelector('[data-progress-fill]');
            if (progressFill) {
                progressFill.classList.remove('bg-orange-500');
                progressFill.classList.add('bg-red-500');
            }

            const progressText = progressBar.querySelector('[data-progress-text]');
            if (progressText) {
                progressText.textContent = '✗ Failed';
            }

            // Remove after 4 seconds
            setTimeout(() => {
                this.removeProgress(uploadId);
            }, 4000);
        }
    }

    /**
     * Remove a progress bar
     * @param {string} uploadId - Unique identifier for this upload
     */
    removeProgress(uploadId) {
        const progressBar = this.progressBars.get(uploadId);
        if (!progressBar) return;

        progressBar.classList.add('animate-fade-out');
        setTimeout(() => {
            progressBar.remove();
            this.progressBars.delete(uploadId);

            // Remove container if no more progress bars
            if (this.progressBars.size === 0 && this.container) {
                this.container.remove();
                this.container = null;
            }
        }, 300);
    }

    /**
     * Create the main container
     */
    createContainer() {
        this.container = document.createElement('div');
        this.container.id = this.containerId;
        this.container.className = 'fixed bottom-4 right-4 z-50 space-y-2 max-w-md w-full sm:w-96';
        document.body.appendChild(this.container);
    }

    /**
     * Create a progress bar element
     */
    createProgressBar(uploadId, fileName, totalSize) {
        const div = document.createElement('div');
        div.className = 'bg-white dark:bg-slate-800 rounded-lg shadow-lg border-2 border-slate-200 dark:border-slate-700 p-4 animate-fade-in';
        div.dataset.uploadId = uploadId;

        const truncatedName = fileName.length > 35 ? fileName.substring(0, 32) + '...' : fileName;

        div.innerHTML = `
            <div class="flex items-center justify-between mb-2">
                <div class="flex items-center gap-2 flex-1 min-w-0">
                    <svg class="w-5 h-5 text-orange-500 flex-shrink-0" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                        <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M7 16a4 4 0 01-.88-7.903A5 5 0 1115.9 6L16 6a5 5 0 011 9.9M15 13l-3-3m0 0l-3 3m3-3v12" />
                    </svg>
                    <div class="flex-1 min-w-0">
                        <div class="text-sm font-medium text-slate-900 dark:text-slate-100 truncate" title="${fileName}">${truncatedName}</div>
                        <div class="text-xs text-slate-500 dark:text-slate-400">
                            <span data-progress-size>0 B</span> / ${this.formatBytes(totalSize)}
                        </div>
                    </div>
                </div>
                <span class="text-sm font-semibold text-orange-600 dark:text-orange-400 ml-2" data-progress-text>0%</span>
            </div>
            <div class="w-full bg-slate-200 dark:bg-slate-700 rounded-full h-2 overflow-hidden">
                <div
                    class="bg-orange-500 h-full transition-all duration-300 ease-out rounded-full"
                    data-progress-fill
                    style="width: 0%"
                ></div>
            </div>
        `;

        return div;
    }

    /**
     * Format bytes to human-readable size
     */
    formatBytes(bytes) {
        if (bytes < 1024) return bytes + ' B';
        if (bytes < 1024 * 1024) return (bytes / 1024).toFixed(1) + ' KB';
        if (bytes < 1024 * 1024 * 1024) return (bytes / (1024 * 1024)).toFixed(1) + ' MB';
        return (bytes / (1024 * 1024 * 1024)).toFixed(2) + ' GB';
    }
}

// Create a global instance for easy access
if (typeof window !== 'undefined') {
    window.uploadProgress = new UploadProgress();
}

// Export for use in other modules
if (typeof module !== 'undefined' && module.exports) {
    module.exports = UploadProgress;
}
