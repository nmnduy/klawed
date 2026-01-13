// File Explorer Module

// Authentication helper functions (inline to avoid module issues)
function isAuthRequired(response) {
    return response.status === 401 || response.status === 403;
}

async function handleAuthError(response, currentPath = null) {
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
    window.location.href = redirectUrl + '?redirect=' + encodeURIComponent(path);
    return true;
}

class FileExplorer {
    constructor() {
        // DOM Elements
        // Note: file-explorer-toggle button has been removed since we now have Files tab
        this.fileExplorerToggle = null;
        this.fileExplorerPanel = document.getElementById('file-explorer-panel');
        this.fileExplorerClose = document.getElementById('file-explorer-close');
        this.fileExplorerRefresh = document.getElementById('file-explorer-refresh');
        this.fileExplorerRetry = document.getElementById('file-explorer-retry');
        this.fileExplorerContent = document.getElementById('file-explorer-content');
        this.fileExplorerTree = document.getElementById('file-explorer-tree');
        this.fileExplorerLoading = document.getElementById('file-explorer-loading');
        this.fileExplorerEmpty = document.getElementById('file-explorer-empty');
        this.fileExplorerEmptyMessage = document.getElementById('file-explorer-empty-message');
        this.fileExplorerError = document.getElementById('file-explorer-error');
        this.fileExplorerErrorMessage = document.getElementById('file-explorer-error-message');
        this.fileExplorerPath = document.getElementById('file-explorer-path');
        this.fileExplorerBreadcrumbs = document.getElementById('file-explorer-breadcrumbs');
        this.fileExplorerStats = document.getElementById('file-explorer-stats');
        this.fileExplorerUp = document.getElementById('file-explorer-up');

        this.fileExplorerSearch = document.getElementById('file-explorer-search');
        this.fileExplorerSearchClear = document.getElementById('file-explorer-search-clear');
        this.fileExplorerFilterButtons = document.querySelectorAll('[data-filter]');
        this.fileExplorerSortButtons = document.querySelectorAll('[data-sort-field]');
        
        // Upload elements
        this.fileExplorerUpload = document.getElementById('file-explorer-upload');
        this.fileExplorerFileInput = document.getElementById('file-explorer-file-input');

        this.filePreviewPanel = document.getElementById('file-preview-panel');
        this.filePreviewClose = document.getElementById('file-preview-close');
        this.filePreviewContent = document.getElementById('file-preview-content');
        this.filePreviewLoading = document.getElementById('file-preview-loading');
        this.filePreviewText = document.getElementById('file-preview-text');
        this.filePreviewBinary = document.getElementById('file-preview-binary');
        this.filePreviewBinarySize = document.getElementById('file-preview-binary-size');
        this.filePreviewError = document.getElementById('file-preview-error');
        this.filePreviewErrorMessage = document.getElementById('file-preview-error-message');
        this.filePreviewPdf = document.getElementById('file-preview-pdf');
        this.filePreviewPdfFrame = document.getElementById('file-preview-pdf-frame');
        this.filePreviewPdfDownload = document.getElementById('file-preview-pdf-download');
        this.filePreviewPdfDownloadBtn = document.getElementById('file-preview-pdf-download-btn');
        this.filePreviewHtml = document.getElementById('file-preview-html');
        this.filePreviewHtmlFrame = document.getElementById('file-preview-html-frame');
        this.filePreviewHtmlDownload = document.getElementById('file-preview-html-download');
        this.filePreviewImage = document.getElementById('file-preview-image');
        this.filePreviewImageImg = document.getElementById('file-preview-image-img');
        this.filePreviewImageDownload = document.getElementById('file-preview-image-download');
        this.filePreviewTextOpenBtn = document.getElementById('file-preview-text-open-btn');
        this.filePreviewTextDownloadBtn = document.getElementById('file-preview-text-download-btn');
        this.filePreviewBinaryOpenBtn = document.getElementById('file-preview-binary-open-btn');
        this.filePreviewBinaryDownloadBtn = document.getElementById('file-preview-binary-download-btn');

        // State
        this.currentPath = '/';
        this.fileTreeState = {}; // Track expanded/collapsed state
        this.autoRefreshInterval = null;
        this.sessionId = null;
        this.userId = null;
        this.rawItems = [];
        this.activeFilter = 'all';
        this.activeSort = 'name';
        this.searchTerm = '';
        this.currentPreviewFilePath = null;
        this.currentPreviewFileName = null;
        
        // Workspace search state (for searching all files)
        this.searchDebounceTimer = null;
        this.isSearchingWorkspace = false; // True when showing workspace search results
        this.searchAbortController = null; // For cancelling in-flight search requests
        this.lastSearchResults = null; // Store last search results for persistence

        // File icons mapping
        this.fileIcons = {
            'folder': `<svg class="w-5 h-5 text-amber-500" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M3 7v10a2 2 0 002 2h14a2 2 0 002-2V9a2 2 0 00-2-2h-6l-2-2H5a2 2 0 00-2 2z" />
            </svg>`,
            'file': `<svg class="w-5 h-5 text-slate-400" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M9 12h6m-6 4h6m2 5H7a2 2 0 01-2-2V5a2 2 0 012-2h5.586a1 1 0 01.707.293l5.414 5.414a1 1 0 01.293.707V19a2 2 0 01-2 2z" />
            </svg>`,
            'file-pdf': `<svg class="w-5 h-5 text-red-500" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M9 12h6m-6 4h6m2 5H7a2 2 0 01-2-2V5a2 2 0 012-2h5.586a1 1 0 01.707.293l5.414 5.414a1 1 0 01.293.707V19a2 2 0 01-2 2z" />
            </svg>`,
            'file-image': `<svg class="w-5 h-5 text-emerald-500" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M4 16l4.586-4.586a2 2 0 012.828 0L16 16m-2-2l1.586-1.586a2 2 0 012.828 0L20 14m-6-6h.01M6 20h12a2 2 0 002-2V6a2 2 0 00-2-2H6a2 2 0 00-2 2v12a2 2 0 002 2z" />
            </svg>`,
            'file-text': `<svg class="w-5 h-5 text-blue-500" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M9 12h6m-6 4h6m2 5H7a2 2 0 01-2-2V5a2 2 0 012-2h5.586a1 1 0 01.707.293l5.414 5.414a1 1 0 01.293.707V19a2 2 0 01-2 2z" />
            </svg>`,
            'file-spreadsheet': `<svg class="w-5 h-5 text-green-500" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M9 17v-2m3 2v-4m3 4v-6m2 10H7a2 2 0 01-2-2V5a2 2 0 012-2h5.586a1 1 0 01.707.293l5.414 5.414a1 1 0 01.293.707V19a2 2 0 01-2 2z" />
            </svg>`,
            'file-archive': `<svg class="w-5 h-5 text-amber-600" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M5 8h14M5 8a2 2 0 110-4h14a2 2 0 110 4M5 8v10a2 2 0 002 2h10a2 2 0 002-2V8m-9 4h4" />
            </svg>`,
            'file-script': `<svg class="w-5 h-5 text-purple-500" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M10 20l4-16m4 4l4 4-4 4M6 16l-4-4 4-4" />
            </svg>`,
            'file-code': `<svg class="w-5 h-5 text-indigo-500" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M10 20l4-16m4 4l4 4-4 4M6 16l-4-4 4-4" />
            </svg>`,
            'file-database': `<svg class="w-5 h-5 text-cyan-500" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M4 7v10c0 2.21 3.582 4 8 4s8-1.79 8-4V7M4 7c0 2.21 3.582 4 8 4s8-1.79 8-4M4 7c0-2.21 3.582-4 8-4s8 1.79 8 4m0 5c0 2.21-3.582 4-8 4s-8-1.79-8-4" />
            </svg>`,
            'file-latex': `<svg class="w-5 h-5 text-orange-500" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M9 12h6m-6 4h6m2 5H7a2 2 0 01-2-2V5a2 2 0 012-2h5.586a1 1 0 01.707.293l5.414 5.414a1 1 0 01.293.707V19a2 2 0 01-2 2z" />
            </svg>`
        };

        // Bind methods
        this.toggleFileExplorer = this.toggleFileExplorer.bind(this);
        this.loadDirectory = this.loadDirectory.bind(this);
        this.previewFile = this.previewFile.bind(this);
        this.openFile = this.openFile.bind(this);
        this.startAutoRefresh = this.startAutoRefresh.bind(this);
        this.stopAutoRefresh = this.stopAutoRefresh.bind(this);
        this.applyFiltersAndSort = this.applyFiltersAndSort.bind(this);

        // Initialize event listeners
        this.initEventListeners();
    }

    // Set session ID and user ID
    setSession(sessionId, userId) {
        console.log('[file-explorer] setSession called, sessionId:', sessionId, 'userId:', userId);
        this.sessionId = sessionId;
        this.userId = userId;
        
        // Enable upload button when session is set
        if (this.fileExplorerUpload && sessionId) {
            this.fileExplorerUpload.disabled = false;
            console.log('[file-explorer] Upload button enabled');
        }
    }

    // Format file size
    formatFileSize(bytes) {
        if (bytes < 1024) return bytes + ' B';
        if (bytes < 1024 * 1024) return (bytes / 1024).toFixed(1) + ' KB';
        if (bytes < 1024 * 1024 * 1024) return (bytes / (1024 * 1024)).toFixed(1) + ' MB';
        return (bytes / (1024 * 1024 * 1024)).toFixed(1) + ' GB';
    }

    // Check if file is an image
    isImageFile(fileName) {
        const imageExtensions = ['.png', '.jpg', '.jpeg', '.gif', '.bmp', '.svg', '.webp', '.ico'];
        return imageExtensions.some(ext => fileName.endsWith(ext));
    }

    // Format date
    formatDate(dateStr) {
        try {
            const date = new Date(dateStr);
            return date.toLocaleDateString() + ' ' + date.toLocaleTimeString([], {hour: '2-digit', minute:'2-digit'});
        } catch (e) {
            return dateStr;
        }
    }

    // Highlight LaTeX syntax
    highlightLatex(content) {
        // Escape HTML to prevent XSS
        const escaped = content
            .replace(/&/g, '&amp;')
            .replace(/</g, '&lt;')
            .replace(/>/g, '&gt;')
            .replace(/"/g, '&quot;')
            .replace(/'/g, '&#039;');
        
        // Apply syntax highlighting in a specific order
        let highlighted = escaped;
        
        // 1. Handle environments first (they contain braces we don't want to highlight separately)
        highlighted = highlighted.replace(/\\begin\{([^}]+)\}/g, '<span class="latex-environment">\\begin{$1}</span>');
        highlighted = highlighted.replace(/\\end\{([^}]+)\}/g, '<span class="latex-environment">\\end{$1}</span>');
        
        // 2. Handle other elements
        highlighted = highlighted
            // Comments: % comment
            .replace(/(^|[^\\])%(.*)/g, '$1<span class="latex-comment">%$2</span>')
            // Commands: \command (but not those already in environment spans)
            .replace(/\\([a-zA-Z@]+)(?![^<]*>)/g, '<span class="latex-command">\\$1</span>')
            // Math mode: $...$ or $$...$$
            .replace(/(\$\$?)(.*?)\1/g, '<span class="latex-math">$1$2$1</span>')
            // Brackets: [ and ]
            .replace(/\[/g, '<span class="latex-bracket">[</span>')
            .replace(/\]/g, '<span class="latex-bracket">]</span>')
            // Braces: { and } (simple approach - will double-highlight some braces in environments)
            .replace(/\{/g, '<span class="latex-brace">{</span>')
            .replace(/\}/g, '<span class="latex-brace">}</span>')
            // Parameters: #1, #2, etc.
            .replace(/#(\d+)/g, '<span class="latex-parameter">#$1</span>');
        
        return highlighted;
    }

    // Auto-compile LaTeX and show PDF preview
    async compileLatexAndPreview(filePath, fileName, latexContent) {
        try {
            // Call compilation endpoint with xelatex engine for better Unicode support
            const response = await fetch(`/file-chat/explorer/compile-latex?path=${encodeURIComponent(filePath)}&engine=xelatex`, {
                method: 'POST',
                headers: {
                    'X-Session-ID': this.sessionId,
                    'Content-Type': 'application/json'
                }
            });

            if (!response.ok) {
                throw new Error(`HTTP ${response.status}: ${response.statusText}`);
            }

            const result = await response.json();

            if (!result.success) {
                throw new Error(result.error || 'Compilation failed');
            }

            // Success - preview the generated PDF
            this.previewFile(result.pdfPath, result.pdfPath.split('/').pop(), result.pdfSize);
            
        } catch (error) {
            console.error('Failed to compile LaTeX:', error);
            
            // Show LaTeX source code with retry option as fallback
            const fallbackDiv = document.createElement('div');
            fallbackDiv.className = 'space-y-4';
            fallbackDiv.innerHTML = `
                <div class="bg-red-50 border border-red-200 rounded-lg p-4">
                    <div class="flex items-center gap-3 mb-3">
                        <svg class="w-5 h-5 text-red-500" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                            <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M12 8v4m0 4h.01M21 12a9 9 0 11-18 0 9 9 0 0118 0z" />
                        </svg>
                        <div>
                            <p class="text-caption-m-bold text-red-700">Failed to compile LaTeX</p>
                            <p class="text-caption-s text-red-600 mt-1">${error.message}</p>
                        </div>
                    </div>
                    <div class="flex gap-2">
                        <button class="latex-retry-btn inline-flex items-center gap-2 px-3 py-1.5 bg-red-500 hover:bg-red-600 text-white rounded-lg text-caption-m-bold transition-all duration-200">
                            Retry Compilation
                        </button>
                        <button class="latex-show-source-btn inline-flex items-center gap-2 px-3 py-1.5 bg-slate-500 hover:bg-slate-600 text-white rounded-lg text-caption-m-bold transition-all duration-200">
                            Show LaTeX Source
                        </button>
                    </div>
                </div>
                <div id="latex-source-preview" class="hidden bg-slate-50 rounded-lg p-4 border border-slate-200">
                    <div class="flex items-center justify-between mb-3">
                        <span class="text-caption-m-bold text-slate-700">LaTeX Source Code</span>
                        <button class="latex-hide-source-btn inline-flex items-center gap-2 px-3 py-1.5 bg-slate-500 hover:bg-slate-600 text-white rounded-lg text-caption-m-bold transition-all duration-200">
                            Hide Source
                        </button>
                    </div>
                    <pre class="text-caption-m font-mono whitespace-pre-wrap break-words text-slate-700 latex-highlight max-h-60 overflow-y-auto">${this.highlightLatex(latexContent)}</pre>
                </div>
            `;

            // Replace loading state with fallback
            const filePreviewContent = document.getElementById('file-preview-content');
            const loadingDiv = filePreviewContent.querySelector('.text-center.py-8');
            if (loadingDiv) {
                loadingDiv.replaceWith(fallbackDiv);
            }

            // Add event listeners
            const retryBtn = fallbackDiv.querySelector('.latex-retry-btn');
            const showSourceBtn = fallbackDiv.querySelector('.latex-show-source-btn');
            const hideSourceBtn = fallbackDiv.querySelector('.latex-hide-source-btn');
            const sourcePreview = fallbackDiv.querySelector('#latex-source-preview');

            retryBtn.addEventListener('click', () => {
                this.compileLatexAndPreview(filePath, fileName, latexContent);
            });

            showSourceBtn.addEventListener('click', () => {
                sourcePreview.classList.remove('hidden');
            });

            hideSourceBtn.addEventListener('click', () => {
                sourcePreview.classList.add('hidden');
            });
        }
    }

    // Simple compile method for manual retry (used in fallback UI)
    async compileLatex(filePath, fileName) {
        // For manual retry, we need to get the LaTeX content first
        try {
            const response = await fetch(`/file-chat/explorer/preview?path=${encodeURIComponent(filePath)}`, {
                headers: {
                    'X-Session-ID': this.sessionId
                }
            });
            
            if (!response.ok) {
                throw new Error(`HTTP ${response.status}: ${response.statusText}`);
            }
            
            const latexContent = await response.text();
            this.compileLatexAndPreview(filePath, fileName, latexContent);
        } catch (error) {
            console.error('Failed to get LaTeX content:', error);
            // If we can't get the content, just try compilation
            this.compileLatexAndPreview(filePath, fileName, '');
        }
    }

    // Open PDF file in new tab
    openPdfFile(pdfPath) {
        const fileUrl = `/file-chat/explorer/open?path=${encodeURIComponent(pdfPath)}`;
        window.open(fileUrl, '_blank', 'noopener');
    }

    // Show loading state
    showLoading() {
        this.fileExplorerLoading.classList.remove('hidden');
        this.fileExplorerTree.classList.add('hidden');
        this.fileExplorerEmpty.classList.add('hidden');
        this.fileExplorerError.classList.add('hidden');
    }

    // Show error state
    showError(message) {
        this.fileExplorerLoading.classList.add('hidden');
        this.fileExplorerTree.classList.add('hidden');
        this.fileExplorerEmpty.classList.add('hidden');
        this.fileExplorerError.classList.remove('hidden');
        this.fileExplorerErrorMessage.textContent = message || 'Failed to load files';
    }

    // Show empty state
    showEmpty() {
        this.fileExplorerLoading.classList.add('hidden');
        this.fileExplorerTree.classList.add('hidden');
        this.fileExplorerEmpty.classList.remove('hidden');
        this.fileExplorerError.classList.add('hidden');
    }

    // Show file tree
    showFileTree() {
        this.fileExplorerLoading.classList.add('hidden');
        this.fileExplorerTree.classList.remove('hidden');
        this.fileExplorerEmpty.classList.add('hidden');
        this.fileExplorerError.classList.add('hidden');
    }

    // Fetch directory listing
    async fetchDirectory(path) {
        console.log('[file-explorer] fetchDirectory called, path:', path, 'sessionId:', this.sessionId);
        if (!this.sessionId) {
            console.error('[file-explorer] No sessionId available, showing error');
            this.showError('Connecting to server... Please wait or try refreshing.');
            return null;
        }

        try {
            const url = `/file-chat/explorer/list?path=${encodeURIComponent(path)}`;
            console.log('[file-explorer] Fetching directory:', url);
            const response = await fetch(url, {
                headers: {
                    'X-Session-ID': this.sessionId
                }
            });

            console.log('[file-explorer] Response status:', response.status);

            if (isAuthRequired(response)) {
                console.warn('[file-explorer] Auth required, redirecting');
                await handleAuthError(response);
                return null;
            }

            if (!response.ok) {
                throw new Error(`HTTP ${response.status}: ${response.statusText}`);
            }

            const data = await response.json();
            console.log('[file-explorer] Directory data received:', data);
            return data;
        } catch (error) {
            console.error('Failed to fetch directory:', error);
            this.showError(error.message);
            return null;
        }
    }

    // Build breadcrumbs from current path
    renderBreadcrumbs(path) {
        if (!this.fileExplorerBreadcrumbs) return;
        this.fileExplorerBreadcrumbs.innerHTML = '';

        // Normalize path (server returns relative with no leading slash)
        const normalized = path && path !== '' ? path.replace(/\\/g, '/') : '';
        const parts = normalized.split('/').filter(Boolean);

        const rootCrumb = document.createElement('button');
        rootCrumb.type = 'button';
        rootCrumb.className = 'text-caption-s text-slate-700 hover:text-orange-600 whitespace-nowrap';
        rootCrumb.textContent = '/';
        rootCrumb.addEventListener('click', () => this.loadDirectory('/'));
        this.fileExplorerBreadcrumbs.appendChild(rootCrumb);

        let accumulated = [];
        parts.forEach((part, index) => {
            const sep = document.createElement('span');
            sep.className = 'text-slate-300';
            sep.textContent = '›';
            this.fileExplorerBreadcrumbs.appendChild(sep);

            accumulated.push(part);
            const crumb = document.createElement('button');
            crumb.type = 'button';
            crumb.className = 'text-caption-s text-slate-700 hover:text-orange-600 truncate max-w-[10ch]';
            crumb.title = part;
            crumb.textContent = part;
            crumb.addEventListener('click', () => this.loadDirectory(accumulated.join('/')));
            this.fileExplorerBreadcrumbs.appendChild(crumb);
        });

        // Up button enable/disable
        if (this.fileExplorerUp) {
            this.fileExplorerUp.disabled = parts.length === 0;
        }
    }

    renderFileTree(items) {
        const isMac = navigator.platform.toUpperCase().indexOf('MAC') >= 0;

        if (!items || items.length === 0) {
            this.showEmpty();
            return;
        }

        // For large result sets, limit what we render for performance
        const MAX_RENDER_ITEMS = 200;
        const isLimited = items.length > MAX_RENDER_ITEMS;
        const itemsToRender = isLimited ? items.slice(0, MAX_RENDER_ITEMS) : items;

        // Use requestAnimationFrame to batch DOM updates
        if (this._renderRAF) {
            cancelAnimationFrame(this._renderRAF);
        }
        
        this._renderRAF = requestAnimationFrame(() => {
            // For search results or large changes, just rebuild the DOM
            // This is faster than trying to diff when most items change
            const shouldRebuild = this.isSearchingWorkspace || 
                Math.abs(this.fileExplorerTree.children.length - itemsToRender.length) > 20;
            
            if (shouldRebuild) {
                this._renderFileTreeFast(itemsToRender, isLimited, items.length);
            } else {
                this._renderFileTreeIncremental(itemsToRender);
            }
            
            this.showFileTree();
        });
    }
    
    /**
     * Fast render - rebuilds DOM completely. Better for search results.
     */
    _renderFileTreeFast(items, isLimited, totalCount) {
        // Build HTML string in one go (much faster than individual DOM operations)
        const html = items.map(item => this._buildFileItemHTML(item)).join('');
        
        // Single DOM update
        this.fileExplorerTree.innerHTML = html;
        
        // Add "show more" indicator if limited
        if (isLimited) {
            const moreDiv = document.createElement('div');
            moreDiv.className = 'px-3 py-2 text-center text-caption-s text-muted-foreground bg-muted/30';
            moreDiv.textContent = `Showing ${items.length} of ${totalCount} results. Refine your search to see more.`;
            this.fileExplorerTree.appendChild(moreDiv);
        }
        
        // Attach event listeners using delegation (much faster than per-element)
        this._attachFileTreeEventDelegation();
    }
    
    /**
     * Build HTML for a single file item (used by fast render)
     */
    _buildFileItemHTML(item) {
        const isDirectory = item.type === 'directory';
        const icon = this.fileIcons[item.icon] || this.fileIcons.file;
        const size = isDirectory ? '' : this.formatFileSize(item.size);
        const date = this.formatDate(item.modified);
        const showDirectory = this.isSearchingWorkspace && item.directory && item.directory !== '/';
        const directoryDisplay = showDirectory ? `<span class="text-muted-foreground text-caption-s truncate" title="${item.directory}">${item.directory}</span>` : '';
        
        // Escape HTML in item properties to prevent XSS
        const escapedName = this._escapeHtml(item.name);
        const escapedPath = this._escapeHtml(item.path);
        
        return `
            <div class="file-item group px-3 py-2 hover:bg-orange-50/60 dark:hover:bg-orange-950/20 transition cursor-pointer" 
                 data-path="${escapedPath}" data-type="${item.type}" data-name="${escapedName}">
                <div class="grid grid-cols-1 lg:grid-cols-[minmax(0,1fr)_112px] items-center gap-3">
                    <div class="flex items-center gap-3 min-w-0">
                        <div class="flex-shrink-0">${icon}</div>
                        <div class="min-w-0 flex-1">
                            <div class="flex items-center gap-2 min-w-0">
                                <span class="truncate text-body-s ${isDirectory ? 'text-foreground font-semibold' : 'text-foreground/80'}" title="${escapedName}">${escapedName}</span>
                                ${isDirectory ? '<span class="hidden sm:inline text-[11px] px-2 py-0.5 rounded-full bg-amber-50 dark:bg-amber-900/30 text-amber-700 dark:text-amber-300 border border-amber-100 dark:border-amber-800">Folder</span>' : ''}
                            </div>
                            ${directoryDisplay}
                            <div class="flex items-center gap-3 text-caption-s text-muted-foreground truncate lg:hidden">
                                <span class="tabular-nums">${size || ''}</span>
                                <span class="whitespace-nowrap">${date}</span>
                            </div>
                        </div>
                    </div>
                    <div class="hidden lg:flex items-center justify-end gap-2 text-caption-s text-muted-foreground text-right tabular-nums">
                        <span>${size}</span>
                        <div class="relative">
                            <button type="button" class="inline-flex items-center justify-center w-8 h-8 text-muted-foreground hover:text-foreground hover:bg-muted rounded transition file-options-btn ${isDirectory ? 'hidden' : ''}" title="File options">
                                <svg class="w-5 h-5" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                                    <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M12 5v.01M12 12v.01M12 19v.01M12 6a1 1 0 110-2 1 1 0 010 2zm0 7a1 1 0 110-2 1 1 0 010 2zm0 7a1 1 0 110-2 1 1 0 010 2z" />
                                </svg>
                            </button>
                            <div class="file-options-dropdown hidden absolute right-0 top-full mt-1 w-48 bg-card rounded-lg shadow-lg border border-border z-10 py-1">
                                <button type="button" class="w-full text-left px-4 py-2 text-caption-s text-foreground hover:bg-muted flex items-center gap-2 open-file-option">
                                    <svg class="w-4 h-4 text-primary" fill="none" stroke="currentColor" stroke-width="2" viewBox="0 0 24 24">
                                        <path stroke-linecap="round" stroke-linejoin="round" d="M3 9a2 2 0 012-2h5l2 2h7a2 2 0 012 2v6a2 2 0 01-2 2H5a2 2 0 01-2-2V9z" />
                                        <path stroke-linecap="round" stroke-linejoin="round" d="M12 11l5-5m0 0h-3m3 0v3" />
                                    </svg>
                                    <span>Open file</span>
                                </button>
                                <button type="button" class="w-full text-left px-4 py-2 text-caption-s text-foreground hover:bg-muted flex items-center gap-2 download-file-option">
                                    <svg class="w-4 h-4 text-cyan-500" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                                        <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M12 10v6m0 0l-3-3m3 3l3-3m2 8H7a2 2 0 01-2-2V5a2 2 0 012-2h5.586a1 1 0 01.707.293l5.414 5.414a1 1 0 01.293.707V19a2 2 0 01-2 2z" />
                                    </svg>
                                    <span>Download file</span>
                                </button>
                                <div class="border-t border-border my-1"></div>
                                <button type="button" class="w-full text-left px-4 py-2 text-caption-s text-red-600 hover:bg-red-50 dark:hover:bg-red-950/30 flex items-center gap-2 delete-file-option">
                                    <svg class="w-4 h-4" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                                        <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M19 7l-.867 12.142A2 2 0 0116.138 21H7.862a2 2 0 01-1.995-1.858L5 7m5 4v6m4-6v6m1-10V4a1 1 0 00-1-1h-4a1 1 0 00-1 1v3M4 7h16" />
                                    </svg>
                                    <span>Delete file</span>
                                </button>
                            </div>
                        </div>
                    </div>
                </div>
            </div>
        `;
    }
    
    /**
     * Escape HTML special characters
     */
    _escapeHtml(str) {
        if (!str) return '';
        return str.replace(/&/g, '&amp;')
                  .replace(/</g, '&lt;')
                  .replace(/>/g, '&gt;')
                  .replace(/"/g, '&quot;')
                  .replace(/'/g, '&#39;');
    }
    
    /**
     * Attach event delegation for file tree (more efficient than per-element listeners)
     */
    _attachFileTreeEventDelegation() {
        // Remove old delegation listener if exists
        if (this._fileTreeClickHandler) {
            this.fileExplorerTree.removeEventListener('click', this._fileTreeClickHandler);
        }
        
        this._fileTreeClickHandler = (e) => {
            const fileItem = e.target.closest('.file-item');
            if (!fileItem) return;
            
            const path = fileItem.dataset.path;
            const type = fileItem.dataset.type;
            const name = fileItem.dataset.name;
            
            // Handle options button click
            if (e.target.closest('.file-options-btn')) {
                e.stopPropagation();
                // Close other dropdowns
                document.querySelectorAll('.file-options-dropdown').forEach(d => d.classList.add('hidden'));
                const dropdown = fileItem.querySelector('.file-options-dropdown');
                if (dropdown) dropdown.classList.toggle('hidden');
                return;
            }
            
            // Handle open option
            if (e.target.closest('.open-file-option')) {
                e.stopPropagation();
                this.openFile(path, name);
                fileItem.querySelector('.file-options-dropdown')?.classList.add('hidden');
                return;
            }
            
            // Handle download option
            if (e.target.closest('.download-file-option')) {
                e.stopPropagation();
                this.downloadFile(path, name);
                fileItem.querySelector('.file-options-dropdown')?.classList.add('hidden');
                return;
            }
            
            // Handle delete option
            if (e.target.closest('.delete-file-option')) {
                e.stopPropagation();
                this.confirmDeleteFile(path, name);
                fileItem.querySelector('.file-options-dropdown')?.classList.add('hidden');
                return;
            }
            
            // Handle file/folder click
            if (type === 'directory') {
                this.clearSearchAndNavigate(path);
            } else {
                // Get size from the rendered content or default to 0
                const sizeEl = fileItem.querySelector('.tabular-nums');
                this.previewFile(path, name, 0);
            }
        };
        
        this.fileExplorerTree.addEventListener('click', this._fileTreeClickHandler);
    }
    
    /**
     * Incremental render - updates existing DOM. Better for small changes.
     */
    _renderFileTreeIncremental(items) {
        // Get existing items to minimize DOM changes
        const existingElements = Array.from(this.fileExplorerTree.children);
        const existingPaths = new Set(existingElements.map(el => el.dataset.path));
        const newPaths = new Set(items.map(item => item.path));

        // Remove items that no longer exist
        existingElements.forEach(el => {
            if (!newPaths.has(el.dataset.path)) {
                el.remove();
            }
        });

        // Update or add items
        items.forEach((item, index) => {
            const isDirectory = item.type === 'directory';
            const icon = this.fileIcons[item.icon] || this.fileIcons.file;
            const size = isDirectory ? '' : `${this.formatFileSize(item.size)}`;
            const date = this.formatDate(item.modified);
            
            // For workspace search, show the directory path
            const showDirectory = this.isSearchingWorkspace && item.directory && item.directory !== '/';
            const directoryDisplay = showDirectory ? `<span class="text-muted-foreground text-caption-s truncate" title="${item.directory}">${item.directory}</span>` : '';

            // Check if this item already exists in the DOM
            let itemElement = existingElements.find(el => el.dataset.path === item.path);
            const isNewElement = !itemElement;

            if (isNewElement) {
                // Create new element
                itemElement = document.createElement('div');
                itemElement.className = 'file-item group px-3 py-2 hover:bg-orange-50/60 transition cursor-pointer';
                itemElement.dataset.path = item.path;
                itemElement.dataset.type = item.type;
                itemElement.dataset.name = item.name;

                itemElement.innerHTML = `
                    <div class="grid grid-cols-1 lg:grid-cols-[minmax(0,1fr)_112px] items-center gap-3">
                        <div class="flex items-center gap-3 min-w-0">
                            <div class="flex-shrink-0">${icon}</div>
                            <div class="min-w-0 flex-1">
                                <div class="flex items-center gap-2 min-w-0">
                                    <span class="truncate text-body-s ${isDirectory ? 'text-slate-900 font-semibold' : 'text-slate-700'}" title="${item.name}">${item.name}</span>
                                    ${isDirectory ? '<span class="hidden sm:inline text-[11px] px-2 py-0.5 rounded-full bg-amber-50 text-amber-700 border border-amber-100">Folder</span>' : ''}
                                </div>
                                ${directoryDisplay}
                                <div class="flex items-center gap-3 text-caption-s text-slate-500 truncate lg:hidden">
                                    <span class="tabular-nums">${size || ''}</span>
                                    <span class="whitespace-nowrap">${date}</span>
                                </div>
                            </div>
                        </div>
                        <div class="hidden lg:flex items-center justify-end gap-2 text-caption-s text-slate-500 text-right tabular-nums">
                            <span>${size}</span>
                            <div class="relative">
                                <button type="button" class="inline-flex items-center justify-center w-8 h-8 text-slate-500 hover:text-slate-700 hover:bg-slate-100 rounded transition file-options-btn ${isDirectory ? 'hidden' : ''}" title="File options">
                                    <svg class="w-5 h-5" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                                        <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M12 5v.01M12 12v.01M12 19v.01M12 6a1 1 0 110-2 1 1 0 010 2zm0 7a1 1 0 110-2 1 1 0 010 2zm0 7a1 1 0 110-2 1 1 0 010 2z" />
                                    </svg>
                                </button>
                                <div class="file-options-dropdown hidden absolute right-0 top-full mt-1 w-48 bg-white rounded-lg shadow-lg border border-slate-200 z-10 py-1">
                                    <button type="button" class="w-full text-left px-4 py-2 text-caption-s text-slate-700 hover:bg-slate-50 flex items-center gap-2 open-file-option">
                                        <svg class="w-4 h-4 text-primary" fill="none" stroke="currentColor" stroke-width="2" viewBox="0 0 24 24">
                                            <path stroke-linecap="round" stroke-linejoin="round" d="M3 9a2 2 0 012-2h5l2 2h7a2 2 0 012 2v6a2 2 0 01-2 2H5a2 2 0 01-2-2V9z" />
                                            <path stroke-linecap="round" stroke-linejoin="round" d="M12 11l5-5m0 0h-3m3 0v3" />
                                        </svg>
                                        <span>Open file</span>
                                    </button>
                                    <button type="button" class="w-full text-left px-4 py-2 text-caption-s text-slate-700 hover:bg-slate-50 flex items-center gap-2 download-file-option">
                                        <svg class="w-4 h-4 text-cyan-500" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                                            <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M12 10v6m0 0l-3-3m3 3l3-3m2 8H7a2 2 0 01-2-2V5a2 2 0 012-2h5.586a1 1 0 01.707.293l5.414 5.414a1 1 0 01.293.707V19a2 2 0 01-2 2z" />
                                        </svg>
                                        <span>Download file</span>
                                    </button>
                                    <div class="border-t border-slate-200 my-1"></div>
                                    <button type="button" class="w-full text-left px-4 py-2 text-caption-s text-red-600 hover:bg-red-50 flex items-center gap-2 delete-file-option">
                                        <svg class="w-4 h-4" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                                            <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M19 7l-.867 12.142A2 2 0 0116.138 21H7.862a2 2 0 01-1.995-1.858L5 7m5 4v6m4-6v6m1-10V4a1 1 0 00-1-1h-4a1 1 0 00-1 1v3M4 7h16" />
                                        </svg>
                                        <span>Delete file</span>
                                    </button>
                                </div>
                            </div>
                        </div>
                    </div>
                `;

                // Add event listeners for new elements
                itemElement.addEventListener('click', () => {
                    if (isDirectory) {
                        // Clear search when navigating to directory
                        this.clearSearchAndNavigate(item.path);
                    } else {
                        this.previewFile(item.path, item.name, item.size);
                    }
                });

                // Three-dots menu functionality
                const optionsButton = itemElement.querySelector('.file-options-btn');
                if (optionsButton) {
                    optionsButton.addEventListener('click', (e) => {
                        e.stopPropagation();
                        
                        // Close any other open dropdowns
                        document.querySelectorAll('.file-options-dropdown').forEach(dropdown => {
                            dropdown.classList.add('hidden');
                        });
                        
                        // Toggle this dropdown
                        const dropdown = itemElement.querySelector('.file-options-dropdown');
                        if (dropdown) {
                            dropdown.classList.toggle('hidden');
                        }
                    });
                    
                    // Add click handlers for dropdown options
                    const openOption = itemElement.querySelector('.open-file-option');
                    if (openOption) {
                        openOption.addEventListener('click', (e) => {
                            e.stopPropagation();
                            this.openFile(item.path, item.name);
                            // Close dropdown after selection
                            const dropdown = itemElement.querySelector('.file-options-dropdown');
                            if (dropdown) {
                                dropdown.classList.add('hidden');
                            }
                        });
                    }
                    
                    const downloadOption = itemElement.querySelector('.download-file-option');
                    if (downloadOption) {
                        downloadOption.addEventListener('click', (e) => {
                            e.stopPropagation();
                            this.downloadFile(item.path, item.name);
                            // Close dropdown after selection
                            const dropdown = itemElement.querySelector('.file-options-dropdown');
                            if (dropdown) {
                                dropdown.classList.add('hidden');
                            }
                        });
                    }
                    
                    const deleteOption = itemElement.querySelector('.delete-file-option');
                    if (deleteOption) {
                        deleteOption.addEventListener('click', (e) => {
                            e.stopPropagation();
                            this.confirmDeleteFile(item.path, item.name);
                            // Close dropdown after selection
                            const dropdown = itemElement.querySelector('.file-options-dropdown');
                            if (dropdown) {
                                dropdown.classList.add('hidden');
                            }
                        });
                    }
                }
            } else {
                // Update existing element's data attributes (in case they changed)
                itemElement.dataset.type = item.type;
                itemElement.dataset.name = item.name;
            }

            // Ensure proper order (append to end, or insert at correct position)
            const currentPosition = Array.from(this.fileExplorerTree.children).indexOf(itemElement);
            if (currentPosition !== index) {
                if (index >= this.fileExplorerTree.children.length) {
                    this.fileExplorerTree.appendChild(itemElement);
                } else {
                    this.fileExplorerTree.insertBefore(itemElement, this.fileExplorerTree.children[index]);
                }
            }
        });
    }

    /**
     * Search files on the server.
     * This performs server-side search and returns matching files.
     */
    async searchFilesOnServer(query) {
        // Cancel any in-flight request
        if (this.searchAbortController) {
            this.searchAbortController.abort();
        }
        
        this.searchAbortController = new AbortController();
        
        try {
            const response = await fetch(`/file-chat/explorer/search?q=${encodeURIComponent(query)}&limit=100`, {
                headers: {
                    'X-Session-ID': this.sessionId
                },
                signal: this.searchAbortController.signal
            });
            
            if (await handleAuthError(response)) {
                return { items: [], hasMore: false };
            }
            
            if (!response.ok) {
                throw new Error(`HTTP ${response.status}`);
            }
            
            const data = await response.json();
            if (data.success) {
                return { 
                    items: data.items || [], 
                    hasMore: data.hasMore || false 
                };
            }
            
            return { items: [], hasMore: false };
        } catch (error) {
            if (error.name === 'AbortError') {
                // Request was cancelled, ignore
                return null;
            }
            console.error('[file-explorer] Failed to search files:', error);
            return { items: [], hasMore: false };
        }
    }
    
    /**
     * Invalidate the workspace files cache.
     * Call this after file uploads, deletes, or other changes.
     * (Now a no-op since we use server-side search)
     */
    invalidateWorkspaceCache() {
        // No-op: server-side search doesn't need client-side cache invalidation
    }

    applyFiltersAndSort(searchResults = null) {
        // If we have search results from server, use those
        // Otherwise use current directory items
        let filtered;
        
        if (searchResults !== null) {
            // Server-side search results (already filtered)
            filtered = searchResults.items;
            this.isSearchingWorkspace = true;
            this.searchHasMore = searchResults.hasMore;
            // Store for later use (e.g., after closing preview)
            this.lastSearchResults = searchResults;
        } else if (this.isSearchingWorkspace && this.lastSearchResults) {
            // We're in search mode and have stored results - use them
            filtered = this.lastSearchResults.items;
            this.searchHasMore = this.lastSearchResults.hasMore;
        } else if (this.isSearchingWorkspace) {
            // We're in search mode but no results available - show empty
            filtered = [];
        } else {
            // Use current directory items
            filtered = [...this.rawItems];
            this.searchHasMore = false;
        }

        // Filter by type (only for non-workspace search, as workspace only has files)
        if (!this.isSearchingWorkspace) {
            if (this.activeFilter === 'folders') {
                filtered = filtered.filter(i => i.type === 'directory');
            } else if (this.activeFilter === 'files') {
                filtered = filtered.filter(i => i.type !== 'directory');
            }
            
            // Apply local search filter for current directory
            if (this.searchTerm && this.searchTerm.trim() !== '') {
                const searchLower = this.searchTerm.toLowerCase();
                filtered = filtered.filter(item => 
                    item.name.toLowerCase().includes(searchLower)
                );
            }
        }

        // Sort results
        const field = this.activeSort;
        filtered.sort((a, b) => {
            if (field === 'size') {
                return (b.size || 0) - (a.size || 0);
            }
            if (field === 'modified') {
                return new Date(b.modified).getTime() - new Date(a.modified).getTime();
            }
            // default: name (for workspace search, sort by path for better grouping)
            if (this.isSearchingWorkspace) {
                return (a.path || '').localeCompare(b.path || '', undefined, { sensitivity: 'base' });
            }
            return (a.name || '').localeCompare(b.name || '', undefined, { sensitivity: 'base' });
        });

        this.renderFileTree(filtered);
        
        // Update stats with search context
        let statsText = `• ${filtered.length} item${filtered.length !== 1 ? 's' : ''}`;
        if (this.isSearchingWorkspace) {
            statsText += ' (workspace)';
            if (this.searchHasMore) {
                statsText += ' [more available]';
            }
        }
        this.fileExplorerStats.textContent = statsText;
        
        if (!filtered.length && this.fileExplorerEmptyMessage) {
            this.fileExplorerEmptyMessage.textContent = this.searchTerm ? 'No results match your search' : 'No files found';
        }
    }
    
    /**
     * Handle search input with debouncing and server-side search.
     */
    async handleSearchInput(searchTerm) {
        this.searchTerm = searchTerm || '';
        
        // Clear any pending debounce
        if (this.searchDebounceTimer) {
            clearTimeout(this.searchDebounceTimer);
        }
        
        // Update clear button visibility
        if (this.fileExplorerSearchClear) {
            this.fileExplorerSearchClear.classList.toggle('hidden', !this.searchTerm);
        }
        
        // If search is empty, return to current directory view immediately
        if (!this.searchTerm.trim()) {
            this.isSearchingWorkspace = false;
            this.lastSearchResults = null; // Clear stored search results
            this.applyFiltersAndSort();
            // Restore breadcrumbs visibility
            if (this.fileExplorerBreadcrumbs) {
                this.fileExplorerBreadcrumbs.classList.remove('hidden');
            }
            return;
        }
        
        // Debounce the actual search (250ms for smoother typing)
        this.searchDebounceTimer = setTimeout(async () => {
            // Show loading indicator in stats
            this.fileExplorerStats.textContent = '• Searching...';
            
            // Perform server-side search
            const results = await this.searchFilesOnServer(this.searchTerm);
            
            // If null, the request was cancelled (new search started)
            if (results === null) {
                return;
            }
            
            // Render results
            this.applyFiltersAndSort(results);
            
            // Update breadcrumbs to show search context
            if (this.fileExplorerBreadcrumbs) {
                this.fileExplorerBreadcrumbs.innerHTML = `
                    <span class="text-muted-foreground">Search results in workspace</span>
                `;
            }
        }, 250);
    }
    
    /**
     * Clear search and navigate to a directory.
     * Used when clicking on a folder from search results.
     */
    clearSearchAndNavigate(path) {
        // Clear search state
        this.searchTerm = '';
        this.isSearchingWorkspace = false;
        this.lastSearchResults = null; // Clear stored search results
        if (this.fileExplorerSearch) {
            this.fileExplorerSearch.value = '';
        }
        if (this.fileExplorerSearchClear) {
            this.fileExplorerSearchClear.classList.add('hidden');
        }
        
        // Navigate to the directory
        this.loadDirectory(path);
    }

    // Load directory
    async loadDirectory(path = '/', silent = false) {
        console.log('[file-explorer] loadDirectory called, path:', path, 'sessionId:', this.sessionId, 'silent:', silent);
        this.currentPath = path;
        this.fileExplorerPath.textContent = path || '/';
        this.renderBreadcrumbs(path);

        // Only show loading spinner if not a silent background refresh
        if (!silent) {
            this.showLoading();
        }

        const data = await this.fetchDirectory(path);
        console.log('[file-explorer] fetchDirectory returned:', data);
        if (data && data.success) {
            this.rawItems = data.items || [];
            console.log('[file-explorer] Loading', this.rawItems.length, 'items');
            this.applyFiltersAndSort();
        } else {
            console.error('[file-explorer] Failed to fetch directory or no success flag');
        }
    }

    // Open file (download or inline preview in browser)
    openFile(filePath, fileName) {
        if (!this.sessionId) return;
        const url = `/file-chat/explorer/open?path=${encodeURIComponent(filePath)}`;
        fetch(url, {
            headers: {
                'X-Session-ID': this.sessionId
            }
        })
            .then(resp => {
                if (!resp.ok) {
                    throw new Error(`HTTP ${resp.status}: ${resp.statusText}`);
                }
                return resp.blob();
            })
            .then(blob => {
                const objectUrl = URL.createObjectURL(blob);
                if (fileName && fileName.toLowerCase().endsWith('.pdf') && this.filePreviewPdf && this.filePreviewPdfFrame) {
                    this.filePreviewPdf.classList.remove('hidden');
                    this.filePreviewPdfFrame.src = objectUrl;
                    if (this.filePreviewPdfDownload) {
                        this.filePreviewPdfDownload.href = objectUrl;
                    }
                }
                const win = window.open(objectUrl, '_blank', 'noopener');
                if (!win) {
                    alert('Please allow popups to open the file.');
                }
                setTimeout(() => URL.revokeObjectURL(objectUrl), 30000);
            })
            .catch(err => {
                console.error('Failed to open file:', err);
                alert('Failed to open file: ' + err.message);
            });
    }

    // Download file (always forces download as attachment)
    downloadFile(filePath, fileName) {
        if (!this.sessionId) return;
        const url = `/file-chat/explorer/download?path=${encodeURIComponent(filePath)}`;
        fetch(url, {
            headers: {
                'X-Session-ID': this.sessionId
            }
        })
            .then(resp => {
                if (!resp.ok) {
                    throw new Error(`HTTP ${resp.status}: ${resp.statusText}`);
                }
                return resp.blob();
            })
            .then(blob => {
                const objectUrl = URL.createObjectURL(blob);
                const a = document.createElement('a');
                a.href = objectUrl;
                a.download = fileName || 'download';
                document.body.appendChild(a);
                a.click();
                document.body.removeChild(a);
                setTimeout(() => URL.revokeObjectURL(objectUrl), 30000);
            })
            .catch(err => {
                console.error('Failed to download file:', err);
                alert('Failed to download file: ' + err.message);
            });
    }

    // Show confirmation dialog and delete file
    confirmDeleteFile(filePath, fileName) {
        if (!this.sessionId) return;
        
        // Show confirmation dialog
        const confirmed = window.confirm(`Are you sure you want to delete "${fileName}"?\n\nThis action cannot be undone.`);
        
        if (confirmed) {
            this.deleteFile(filePath, fileName);
        }
    }

    // Delete file from the server
    async deleteFile(filePath, fileName) {
        if (!this.sessionId) return;
        
        try {
            const url = `/file-chat/explorer/delete?path=${encodeURIComponent(filePath)}`;
            const response = await fetch(url, {
                method: 'DELETE',
                headers: {
                    'X-Session-ID': this.sessionId
                }
            });

            if (!response.ok) {
                const errorData = await response.json().catch(() => ({}));
                throw new Error(errorData.error || `HTTP ${response.status}: ${response.statusText}`);
            }

            const result = await response.json();
            
            if (result.success) {
                this.showToast(`File "${fileName}" deleted successfully`, 'success');
                
                // Refresh the current directory to show the file is gone
                await this.loadDirectory(this.currentPath);
                
                // Invalidate workspace cache since files changed
                this.invalidateWorkspaceCache();
            } else {
                throw new Error(result.error || 'Failed to delete file');
            }
        } catch (error) {
            console.error('Failed to delete file:', error);
            this.showToast(`Failed to delete file: ${error.message}`, 'error');
        }
    }

    // Preview file
    async previewFile(filePath, fileName, fileSize) {
        // Store current file info for download buttons
        this.currentPreviewFilePath = filePath;
        this.currentPreviewFileName = fileName;

        // Show preview modal and disable body scroll
        this.filePreviewPanel.classList.remove('hidden');
        document.body.style.overflow = 'hidden';

        // Show loading and hide all preview types
        this.filePreviewLoading.classList.remove('hidden');
        this.filePreviewText.classList.add('hidden');
        this.filePreviewBinary.classList.add('hidden');
        this.filePreviewError.classList.add('hidden');
        if (this.filePreviewPdf) this.filePreviewPdf.classList.add('hidden');
        if (this.filePreviewPdfFrame) this.filePreviewPdfFrame.src = '';
        if (this.filePreviewHtml) this.filePreviewHtml.classList.add('hidden');
        if (this.filePreviewHtmlFrame) this.filePreviewHtmlFrame.src = '';
        if (this.filePreviewImage) this.filePreviewImage.classList.add('hidden');
        if (this.filePreviewImageImg) this.filePreviewImageImg.src = '';
        
        // Hide any previous warning banner
        const warningBanner = document.getElementById('file-preview-warning');
        if (warningBanner) warningBanner.classList.add('hidden');

        try {
            const lowerFileName = fileName ? fileName.toLowerCase() : '';
            
            // Handle images
            if (lowerFileName && this.isImageFile(lowerFileName)) {
                const fileUrl = `/file-chat/explorer/open?path=${encodeURIComponent(filePath)}`;
                const resp = await fetch(fileUrl, {
                    headers: {
                        'X-Session-ID': this.sessionId
                    }
                });
                if (!resp.ok) {
                    throw new Error(`HTTP ${resp.status}: ${resp.statusText}`);
                }
                const blob = await resp.blob();
                const objectUrl = URL.createObjectURL(blob);
                if (this.filePreviewImage && this.filePreviewImageImg) {
                    this.filePreviewImage.classList.remove('hidden');
                    this.filePreviewImageImg.onload = () => {
                        this.filePreviewLoading.classList.add('hidden');
                    };
                    this.filePreviewImageImg.onerror = () => {
                        this.filePreviewLoading.classList.add('hidden');
                        throw new Error('Failed to load image');
                    };
                    if (this.filePreviewImageDownload) {
                        this.filePreviewImageDownload.href = objectUrl;
                        this.filePreviewImageDownload.download = fileName;
                    }
                    this.filePreviewImageImg.src = objectUrl;
                } else {
                    window.open(objectUrl, '_blank', 'noopener');
                    this.filePreviewLoading.classList.add('hidden');
                }
                setTimeout(() => URL.revokeObjectURL(objectUrl), 30000);
                return;
            }
            
            // Handle HTML files
            if (lowerFileName && (lowerFileName.endsWith('.html') || lowerFileName.endsWith('.htm'))) {
                const fileUrl = `/file-chat/explorer/open?path=${encodeURIComponent(filePath)}`;
                const resp = await fetch(fileUrl, {
                    headers: {
                        'X-Session-ID': this.sessionId
                    }
                });
                if (!resp.ok) {
                    throw new Error(`HTTP ${resp.status}: ${resp.statusText}`);
                }
                const blob = await resp.blob();
                const objectUrl = URL.createObjectURL(blob);
                if (this.filePreviewHtml && this.filePreviewHtmlFrame) {
                    this.filePreviewHtml.classList.remove('hidden');
                    this.filePreviewHtmlFrame.onload = () => {
                        this.filePreviewLoading.classList.add('hidden');
                    };
                    if (this.filePreviewHtmlDownload) {
                        this.filePreviewHtmlDownload.href = objectUrl;
                        this.filePreviewHtmlDownload.download = fileName;
                    }
                    this.filePreviewHtmlFrame.src = objectUrl;
                } else {
                    window.open(objectUrl, '_blank', 'noopener');
                    this.filePreviewLoading.classList.add('hidden');
                }
                setTimeout(() => URL.revokeObjectURL(objectUrl), 30000);
                return;
            }
            
            // Handle PDFs
            if (lowerFileName && lowerFileName.endsWith('.pdf')) {
                const fileUrl = `/file-chat/explorer/open?path=${encodeURIComponent(filePath)}`;
                const resp = await fetch(fileUrl, {
                    headers: {
                        'X-Session-ID': this.sessionId
                    }
                });
                if (!resp.ok) {
                    throw new Error(`HTTP ${resp.status}: ${resp.statusText}`);
                }
                const blob = await resp.blob();
                const objectUrl = URL.createObjectURL(blob);
                if (this.filePreviewPdf && this.filePreviewPdfFrame) {
                    this.filePreviewPdf.classList.remove('hidden');
                    this.filePreviewPdfFrame.onload = () => {
                        this.filePreviewLoading.classList.add('hidden');
                    };
                    if (this.filePreviewPdfDownload) {
                        this.filePreviewPdfDownload.href = objectUrl;
                        // Update click handler to use download method
                        this.filePreviewPdfDownload.onclick = (e) => {
                            e.preventDefault();
                            this.downloadFile(filePath, fileName);
                        };
                    }
                    this.filePreviewPdfFrame.src = objectUrl;
                } else {
                    window.open(objectUrl, '_blank', 'noopener');
                    this.filePreviewLoading.classList.add('hidden');
                }
                setTimeout(() => URL.revokeObjectURL(objectUrl), 30000);
                return;
            }

            const response = await fetch(`/file-chat/explorer/preview?path=${encodeURIComponent(filePath)}`, {
                headers: {
                    'X-Session-ID': this.sessionId
                }
            });

            if (!response.ok) {
                throw new Error(`HTTP ${response.status}: ${response.statusText}`)
            }

            const responseText = await response.text();

            // Check if it's a binary file message or not previewable (plain text responses)
            if (responseText.startsWith('Binary file') || responseText.includes('File type not supported for preview')) {
                this.filePreviewBinarySize.textContent = fileSize ? this.formatFileSize(fileSize) : '';
                this.filePreviewBinary.classList.remove('hidden');
            } else {
                // Try to parse as JSON (new tiered response format)
                let content = responseText;
                let tier = 'small';
                let warning = null;
                
                try {
                    const jsonResponse = JSON.parse(responseText);
                    if (jsonResponse.tier) {
                        tier = jsonResponse.tier;
                        warning = jsonResponse.warning;
                        content = jsonResponse.content;
                        
                        // Handle "too_large" tier
                        if (tier === 'too_large') {
                            this.filePreviewBinarySize.textContent = jsonResponse.fileSizeFormatted || '';
                            // Update the binary preview text to show the warning
                            const binaryText = this.filePreviewBinary.querySelector('p.text-caption-m');
                            if (binaryText) {
                                binaryText.textContent = warning || 'File too large for preview';
                            }
                            this.filePreviewBinary.classList.remove('hidden');
                            this.filePreviewLoading.classList.add('hidden');
                            return;
                        }
                    }
                } catch (e) {
                    // Not JSON, use raw text (backward compatibility)
                    content = responseText;
                }
                
                let pre = this.filePreviewText.querySelector('pre');
                
                // Create pre element if it doesn't exist
                if (!pre) {
                    console.warn('pre element not found in filePreviewText, creating it');
                    pre = document.createElement('pre');
                    pre.className = 'text-caption-m font-mono whitespace-pre-wrap break-words text-slate-700 dark:text-slate-300';
                    this.filePreviewText.appendChild(pre);
                }
                
                // Show warning banner if present
                this.showPreviewWarning(warning, tier);
                
                // Handle LaTeX files specially - auto-compile and show PDF
                if (fileName && fileName.toLowerCase().endsWith('.tex')) {
                    // Show loading state for LaTeX compilation
                    const loadingDiv = document.createElement('div');
                    loadingDiv.className = 'text-center py-8';
                    loadingDiv.innerHTML = `
                        <div class="inline-flex flex-col items-center gap-3 px-6 py-4 bg-blue-50 rounded-lg border border-blue-200">
                            <svg class="w-8 h-8 animate-spin text-blue-500" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                                <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M4 4v5h.582m15.356 2A8.001 8.001 0 004.582 9m0 0H9m11 11v-5h-.581m0 0a8.003 8.003 0 01-15.357-2m15.357 2H15" />
                            </svg>
                            <div class="text-center">
                                <p class="text-caption-m-bold text-blue-700">Compiling LaTeX to PDF...</p>
                                <p class="text-caption-s text-blue-600 mt-1">This may take a few seconds</p>
                            </div>
                        </div>
                    `;
                    
                    // Replace the pre element with loading state
                    pre.parentNode.replaceChild(loadingDiv, pre);
                    this.filePreviewText.classList.remove('hidden');
                    
                    // Auto-compile the LaTeX file
                    this.compileLatexAndPreview(filePath, fileName, content);
                } else {
                    pre.textContent = content;
                    // For large files, use simpler styling (no syntax highlighting consideration)
                    if (tier === 'large') {
                        pre.className = 'text-caption-m font-mono whitespace-pre-wrap break-words text-slate-700 dark:text-slate-300 bg-white dark:bg-slate-800 p-3 rounded border border-border';
                    } else {
                        pre.className = 'text-caption-m font-mono whitespace-pre-wrap break-words text-slate-700 dark:text-slate-300';
                    }
                    this.filePreviewText.classList.remove('hidden');
                }
            }

            this.filePreviewLoading.classList.add('hidden');

        } catch (error) {
            console.error('Failed to preview file:', error);
            if (this.filePreviewErrorMessage) this.filePreviewErrorMessage.textContent = error.message;
            this.filePreviewLoading.classList.add('hidden');
            this.filePreviewError.classList.remove('hidden');
        }
    }

    // Show warning banner for preview
    showPreviewWarning(warning, tier) {
        // Get or create warning banner element
        let warningBanner = document.getElementById('file-preview-warning');
        
        if (!warning) {
            // Hide warning if no warning message
            if (warningBanner) {
                warningBanner.classList.add('hidden');
            }
            return;
        }
        
        // Create warning banner if it doesn't exist
        if (!warningBanner) {
            warningBanner = document.createElement('div');
            warningBanner.id = 'file-preview-warning';
            warningBanner.className = 'mb-3 px-4 py-2 rounded-lg flex items-center gap-2 text-caption-m';
            // Insert at the beginning of preview content
            this.filePreviewContent.insertBefore(warningBanner, this.filePreviewContent.firstChild);
        }
        
        // Set appropriate styling based on tier
        if (tier === 'large') {
            warningBanner.className = 'mb-3 px-4 py-2 rounded-lg flex items-center gap-2 text-caption-m bg-orange-50 dark:bg-orange-900/20 text-orange-700 dark:text-orange-300 border border-orange-200 dark:border-orange-800';
        } else {
            warningBanner.className = 'mb-3 px-4 py-2 rounded-lg flex items-center gap-2 text-caption-m bg-yellow-50 dark:bg-yellow-900/20 text-yellow-700 dark:text-yellow-300 border border-yellow-200 dark:border-yellow-800';
        }
        
        warningBanner.innerHTML = `
            <svg class="w-4 h-4 flex-shrink-0" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M12 9v2m0 4h.01m-6.938 4h13.856c1.54 0 2.502-1.667 1.732-3L13.732 4c-.77-1.333-2.694-1.333-3.464 0L3.34 16c-.77 1.333.192 3 1.732 3z" />
            </svg>
            <span>${warning}</span>
        `;
        
        warningBanner.classList.remove('hidden');
    }

    // Toggle file explorer
    toggleFileExplorer() {
        const isHidden = this.fileExplorerPanel.classList.contains('hidden');

        if (isHidden) {
            this.fileExplorerPanel.classList.remove('hidden');

            this.loadDirectory();
            this.startAutoRefresh();
        } else {
            this.fileExplorerPanel.classList.add('hidden');

            this.stopAutoRefresh();
        }
    }

    // Start auto-refresh
    startAutoRefresh() {
        this.stopAutoRefresh(); // Clear any existing interval
        this.autoRefreshInterval = setInterval(() => {
            // Skip auto-refresh when in search mode to preserve search results
            if (this.isSearchingWorkspace) {
                return;
            }
            // Use silent mode for background refreshes to avoid showing spinner
            this.loadDirectory(this.currentPath, true);
        }, 30000); // Refresh every 30 seconds
    }

    // Stop auto-refresh
    stopAutoRefresh() {
        if (this.autoRefreshInterval) {
            clearInterval(this.autoRefreshInterval);
            this.autoRefreshInterval = null;
        }
    }

    // Close preview modal
    closePreviewModal() {
        this.filePreviewPanel.classList.add('hidden');
        document.body.style.overflow = ''; // Re-enable body scroll
    }

    // Initialize event listeners
    initEventListeners() {
        // Note: file-explorer-toggle button has been removed since we now have Files tab
        // The file explorer is now shown/hidden via the tab system

        if (this.fileExplorerClose) {
            this.fileExplorerClose.addEventListener('click', () => {
                this.fileExplorerPanel.classList.add('hidden');
                this.stopAutoRefresh();
            });
        }

        if (this.fileExplorerRefresh) {
            this.fileExplorerRefresh.addEventListener('click', () => {
                this.loadDirectory(this.currentPath);
            });
        }

        if (this.fileExplorerRetry) {
            this.fileExplorerRetry.addEventListener('click', () => {
                this.loadDirectory(this.currentPath);
            });
        }

        // Search (workspace-wide with debouncing)
        if (this.fileExplorerSearch) {
            this.fileExplorerSearch.addEventListener('input', (e) => {
                this.handleSearchInput(e.target.value);
            });
        }

        if (this.fileExplorerSearchClear) {
            this.fileExplorerSearchClear.addEventListener('click', () => {
                if (this.fileExplorerSearch) {
                    this.fileExplorerSearch.value = '';
                }
                this.handleSearchInput('');
            });
        }

        // Upload functionality (file explorer manages its own upload components)
        if (this.fileExplorerUpload && this.fileExplorerFileInput) {
            this.fileExplorerUpload.addEventListener('click', () => {
                this.fileExplorerFileInput.click();
            });
            
            this.fileExplorerFileInput.addEventListener('change', (e) => {
                this.handleFileUpload(e.target.files);
            });
        }

        // Filters
        if (this.fileExplorerFilterButtons && this.fileExplorerFilterButtons.length) {
            this.fileExplorerFilterButtons.forEach(btn => {
                btn.addEventListener('click', () => {
                    this.activeFilter = btn.dataset.filter || 'all';
                    this.fileExplorerFilterButtons.forEach(b => b.classList.remove('bg-orange-50', 'text-orange-700', 'border', 'border-orange-200'));
                    btn.classList.add('bg-orange-50', 'text-orange-700', 'border', 'border-orange-200');
                    this.applyFiltersAndSort();
                });
            });
        }

        // Sorting
        if (this.fileExplorerSortButtons && this.fileExplorerSortButtons.length) {
            this.fileExplorerSortButtons.forEach(btn => {
                btn.addEventListener('click', () => {
                    this.activeSort = btn.dataset.sortField || 'name';
                    this.fileExplorerSortButtons.forEach(b => b.classList.remove('bg-orange-50', 'text-orange-700', 'border', 'border-orange-200'));
                    btn.classList.add('bg-orange-50', 'text-orange-700', 'border', 'border-orange-200');
                    this.applyFiltersAndSort();
                });
            });
        }

        // Panel width constraints are now handled by CSS (see index.css @media queries)

        if (this.fileExplorerUp) {
            this.fileExplorerUp.addEventListener('click', () => {
                if (!this.currentPath || this.currentPath === '/' || this.currentPath === '') return;
                const normalized = this.currentPath.replace(/\\/g, '/');
                const parts = normalized.split('/').filter(Boolean);
                parts.pop();
                const parent = parts.join('/');
                this.loadDirectory(parent || '/');
            });
        }

        if (this.filePreviewClose) {
            this.filePreviewClose.addEventListener('click', () => {
                this.closePreviewModal();
            });
        }

        // Close modal on backdrop click
        if (this.filePreviewPanel) {
            this.filePreviewPanel.addEventListener('click', (e) => {
                if (e.target === this.filePreviewPanel) {
                    this.closePreviewModal();
                }
            });
        }

        // Close modal on ESC key
        document.addEventListener('keydown', (e) => {
            if (e.key === 'Escape' && !this.filePreviewPanel.classList.contains('hidden')) {
                this.closePreviewModal();
            }
        });

        // Preview panel download buttons
        if (this.filePreviewPdfDownloadBtn) {
            this.filePreviewPdfDownloadBtn.addEventListener('click', () => {
                if (this.currentPreviewFilePath && this.currentPreviewFileName) {
                    this.downloadFile(this.currentPreviewFilePath, this.currentPreviewFileName);
                }
            });
        }

        if (this.filePreviewTextOpenBtn) {
            this.filePreviewTextOpenBtn.addEventListener('click', () => {
                if (this.currentPreviewFilePath && this.currentPreviewFileName) {
                    this.openFile(this.currentPreviewFilePath, this.currentPreviewFileName);
                }
            });
        }

        if (this.filePreviewTextDownloadBtn) {
            this.filePreviewTextDownloadBtn.addEventListener('click', () => {
                if (this.currentPreviewFilePath && this.currentPreviewFileName) {
                    this.downloadFile(this.currentPreviewFilePath, this.currentPreviewFileName);
                }
            });
        }

        if (this.filePreviewBinaryOpenBtn) {
            this.filePreviewBinaryOpenBtn.addEventListener('click', () => {
                if (this.currentPreviewFilePath && this.currentPreviewFileName) {
                    this.openFile(this.currentPreviewFilePath, this.currentPreviewFileName);
                }
            });
        }

        if (this.filePreviewBinaryDownloadBtn) {
            this.filePreviewBinaryDownloadBtn.addEventListener('click', () => {
                if (this.currentPreviewFilePath && this.currentPreviewFileName) {
                    this.downloadFile(this.currentPreviewFilePath, this.currentPreviewFileName);
                }
            });
        }

        // Set default active styles for filters/sorts
        if (this.fileExplorerFilterButtons && this.fileExplorerFilterButtons.length) {
            const defaultFilter = Array.from(this.fileExplorerFilterButtons).find(btn => btn.dataset.filter === this.activeFilter);
            if (defaultFilter) {
                defaultFilter.classList.add('bg-orange-50', 'text-orange-700', 'border', 'border-orange-200');
            }
        }
        if (this.fileExplorerSortButtons && this.fileExplorerSortButtons.length) {
            const defaultSort = Array.from(this.fileExplorerSortButtons).find(btn => btn.dataset.sortField === this.activeSort);
            if (defaultSort) {
                defaultSort.classList.add('bg-orange-50', 'text-orange-700', 'border', 'border-orange-200');
            }
        }

        // Close dropdowns when clicking outside
        document.addEventListener('click', (e) => {
            if (!e.target.closest('.file-options-btn') && !e.target.closest('.file-options-dropdown')) {
                document.querySelectorAll('.file-options-dropdown').forEach(dropdown => {
                    dropdown.classList.add('hidden');
                });
            }
        });
    }

    // Handle file upload
    async handleFileUpload(files) {
        if (!files || files.length === 0) return;

        console.log(`[file-explorer] Uploading ${files.length} file(s)`);
        
        const MAX_STANDARD_UPLOAD = 100 * 1024 * 1024; // 100 MB
        const largeFiles = [];
        const standardFiles = [];
        
        // Separate large files from standard files
        for (let i = 0; i < files.length; i++) {
            if (files[i].size > MAX_STANDARD_UPLOAD) {
                largeFiles.push(files[i]);
            } else {
                standardFiles.push(files[i]);
            }
        }
        
        const originalText = this.fileExplorerUpload.innerHTML;
        
        try {
            // Show loading state on button (minimal, since progress bar shows details)
            this.fileExplorerUpload.innerHTML = `
                <svg class="w-5 h-5 animate-spin" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                    <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M4 4v5h.582m15.356 2A8.001 8.001 0 004.582 9m0 0H9m11 11v-5h-.581m0 0a8.003 8.003 0 01-15.357-2m15.357 2H15" />
                </svg>
                <span class="hidden sm:inline">Uploading...</span>
            `;
            this.fileExplorerUpload.disabled = true;

            let uploadedCount = 0;
            const errors = [];

            // Upload standard files using StandardUploader (with progress)
            if (standardFiles.length > 0) {
                try {
                    console.log(`[file-explorer] Uploading ${standardFiles.length} standard file(s)`);
                    
                    const result = await StandardUploader.upload(standardFiles, this.sessionId, this.currentPath, {
                        useProgressUI: true
                    });

                    console.log('[file-explorer] Standard upload successful:', result);
                    uploadedCount += result.count || standardFiles.length;
                } catch (error) {
                    console.error('[file-explorer] Standard upload error:', error);
                    errors.push(`Standard files: ${error.message}`);
                }
            }

            // Upload large files using chunked upload (with progress)
            if (largeFiles.length > 0) {
                if (typeof ChunkedUploader === 'undefined') {
                    const errorMsg = 'ChunkedUploader not available. Cannot upload large files.';
                    console.error('[file-explorer]', errorMsg);
                    errors.push(errorMsg);
                } else {
                    for (const file of largeFiles) {
                        try {
                            console.log(`[file-explorer] Uploading large file via chunked upload: ${file.name} (${file.size} bytes)`);
                            
                            const uploader = new ChunkedUploader({
                                useProgressUI: true, // Use the progress UI
                                onError: (error) => {
                                    console.error(`[file-explorer] Chunked upload error for ${file.name}:`, error);
                                }
                            });

                            await uploader.upload(file, this.sessionId);
                            console.log(`[file-explorer] Successfully uploaded large file: ${file.name}`);
                            uploadedCount++;
                        } catch (error) {
                            console.error(`[file-explorer] Failed to upload large file ${file.name}:`, error);
                            errors.push(`${file.name}: ${error.message}`);
                        }
                    }
                }
            }

            // Refresh file list
            await this.loadDirectory(this.currentPath);
            
            // Invalidate workspace cache since files changed
            this.invalidateWorkspaceCache();

            // Show appropriate message
            if (uploadedCount === files.length) {
                this.showToast(`${uploadedCount} file(s) uploaded successfully`, 'success');
            } else if (uploadedCount > 0) {
                this.showToast(`${uploadedCount}/${files.length} file(s) uploaded. Errors: ${errors.join(', ')}`, 'warning');
            } else {
                throw new Error(errors.join(', ') || 'All uploads failed');
            }

        } catch (error) {
            console.error('[file-explorer] Upload error:', error);
            this.showToast(`Upload failed: ${error.message}`, 'error');
        } finally {
            // Reset button state
            this.fileExplorerUpload.innerHTML = originalText;
            this.fileExplorerUpload.disabled = false;
            
            // Clear file input
            if (this.fileExplorerFileInput) {
                this.fileExplorerFileInput.value = '';
            }
        }
    }

    // Show toast notification
    showToast(message, type = 'info') {
        // Create toast element
        const toast = document.createElement('div');
        toast.className = `fixed top-4 right-4 z-[9999] px-4 py-3 rounded-lg shadow-lg animate-fade-in max-w-md ${
            type === 'success' ? 'bg-green-50 text-green-800 border border-green-200' :
            type === 'error' ? 'bg-red-50 text-red-800 border border-red-200' :
            type === 'warning' ? 'bg-yellow-50 text-yellow-800 border border-yellow-200' :
            'bg-blue-50 text-blue-800 border border-blue-200'
        }`;
        
        toast.innerHTML = `
            <div class="flex items-start gap-2">
                <svg class="w-5 h-5 flex-shrink-0 mt-0.5" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                    ${type === 'success' ? '<path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M5 13l4 4L19 7" />' :
                      type === 'error' ? '<path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M6 18L18 6M6 6l12 12" />' :
                      type === 'warning' ? '<path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M12 9v2m0 4h.01m-6.938 4h13.856c1.54 0 2.502-1.667 1.732-3L13.732 4c-.77-1.333-2.694-1.333-3.464 0L3.34 16c-.77 1.333.192 3 1.732 3z" />' :
                      '<path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M13 16h-1v-4h-1m1-4h.01M21 12a9 9 0 11-18 0 9 9 0 0118 0z" />'}
                </svg>
                <span class="text-caption-m-bold break-words">${message}</span>
            </div>
        `;
        
        document.body.appendChild(toast);
        
        // Calculate duration based on message length (minimum 3s, add 1s per 50 chars, max 15s)
        const baseTimeout = 3000;
        const extraTime = Math.min(Math.floor(message.length / 50) * 1000, 12000);
        const totalTimeout = baseTimeout + extraTime;
        
        // Remove toast after calculated duration
        setTimeout(() => {
            toast.classList.add('animate-fade-out');
            setTimeout(() => {
                if (toast.parentNode) {
                    toast.parentNode.removeChild(toast);
                }
            }, 300);
        }, totalTimeout);
    }
}

// Expose to global scope so other scripts (e.g., invoiceChat) can instantiate it
if (typeof window !== 'undefined') {
    window.FileExplorer = FileExplorer;
}
