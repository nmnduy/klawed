// File Explorer Module
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
        this.sessionId = sessionId;
        this.userId = userId;
    }

    // Format file size
    formatFileSize(bytes) {
        if (bytes < 1024) return bytes + ' B';
        if (bytes < 1024 * 1024) return (bytes / 1024).toFixed(1) + ' KB';
        if (bytes < 1024 * 1024 * 1024) return (bytes / (1024 * 1024)).toFixed(1) + ' MB';
        return (bytes / (1024 * 1024 * 1024)).toFixed(1) + ' GB';
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
            const response = await fetch(`/invoice-chat/explorer/compile-latex?path=${encodeURIComponent(filePath)}&engine=xelatex`, {
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
            const response = await fetch(`/invoice-chat/explorer/preview?path=${encodeURIComponent(filePath)}`, {
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
        const fileUrl = `/invoice-chat/explorer/open?path=${encodeURIComponent(pdfPath)}`;
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
        if (!this.sessionId) {
            this.showError('Connecting to server... Please wait or try refreshing.');
            return null;
        }

        try {
            const response = await fetch(`/invoice-chat/explorer/list?path=${encodeURIComponent(path)}`, {
                headers: {
                    'X-Session-ID': this.sessionId
                }
            });

            if (!response.ok) {
                throw new Error(`HTTP ${response.status}: ${response.statusText}`);
            }

            return await response.json();
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
        this.fileExplorerTree.innerHTML = '';

        if (!items || items.length === 0) {
            this.showEmpty();
            return;
        }

        items.forEach(item => {
            const isDirectory = item.type === 'directory';
            const icon = this.fileIcons[item.icon] || this.fileIcons.file;
            const size = isDirectory ? '' : `${this.formatFileSize(item.size)}`;
            const date = this.formatDate(item.modified);

            const itemElement = document.createElement('div');
            itemElement.className = 'file-item group px-3 py-2 hover:bg-orange-50/60 transition cursor-pointer';
            itemElement.dataset.path = item.path;
            itemElement.dataset.type = item.type;
            itemElement.dataset.name = item.name;

            itemElement.innerHTML = `
                <div class="grid grid-cols-1 lg:grid-cols-[minmax(0,1fr)_112px] items-center gap-3">
                    <div class="flex items-center gap-3 min-w-0">
                        <div class="flex-shrink-0">${icon}</div>
                        <div class="min-w-0">
                            <div class="flex items-center gap-2 min-w-0">
                                <span class="truncate text-body-s ${isDirectory ? 'text-slate-900 font-semibold' : 'text-slate-700'}" title="${item.name}">${item.name}</span>
                                ${isDirectory ? '<span class="hidden sm:inline text-[11px] px-2 py-0.5 rounded-full bg-amber-50 text-amber-700 border border-amber-100">Folder</span>' : ''}
                            </div>
                            <div class="flex items-center gap-3 text-caption-s text-slate-500 truncate lg:hidden">
                                <span class="tabular-nums">${size || ''}</span>
                                <span class="whitespace-nowrap">${date}</span>
                            </div>
                        </div>
                    </div>
                    <div class="hidden lg:flex items-center justify-end gap-2 text-caption-s text-slate-500 text-right tabular-nums">
                        <span>${size}</span>
                        <button type="button" class="inline-flex items-center gap-1 px-2 py-1 text-caption-s text-orange-700 hover:text-orange-800 hover:bg-orange-50 rounded transition open-file-btn ${isDirectory ? 'hidden' : ''}" title="${isMac ? 'Open in Finder' : 'Open in Explorer'}">
                            <svg class="w-4 h-4" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                                <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M13 7h6m0 0v6m0-6L10 16" />
                                <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M13 17H7a2 2 0 01-2-2V7a2 2 0 012-2h6" />
                            </svg>
                            <span class="hidden xl:inline">Open</span>
                        </button>
                    </div>
                </div>
            `;

            itemElement.addEventListener('click', () => {
                if (isDirectory) {
                    this.loadDirectory(item.path);
                } else {
                    this.previewFile(item.path, item.name, item.size);
                }
            });

            const openButton = itemElement.querySelector('.open-file-btn');
            if (openButton) {
                openButton.addEventListener('click', (e) => {
                    e.stopPropagation();
                    this.openFile(item.path, item.name);
                });
            }

            this.fileExplorerTree.appendChild(itemElement);
        });

        this.showFileTree();
    }

    applyFiltersAndSort() {
        let filtered = [...this.rawItems];

        // Filter by type
        if (this.activeFilter === 'folders') {
            filtered = filtered.filter(i => i.type === 'directory');
        } else if (this.activeFilter === 'files') {
            filtered = filtered.filter(i => i.type !== 'directory');
        }

        // Search by name (case-insensitive)
        if (this.searchTerm && this.searchTerm.trim() !== '') {
            const term = this.searchTerm.toLowerCase();
            filtered = filtered.filter(i => (i.name || '').toLowerCase().includes(term));
        }

        // Sort
        const field = this.activeSort;
        filtered.sort((a, b) => {
            if (field === 'size') {
                return (b.size || 0) - (a.size || 0);
            }
            if (field === 'modified') {
                return new Date(b.modified).getTime() - new Date(a.modified).getTime();
            }
            // default: name
            return (a.name || '').localeCompare(b.name || '', undefined, { sensitivity: 'base' });
        });

        this.renderFileTree(filtered);
        this.fileExplorerStats.textContent = `• ${filtered.length} item${filtered.length !== 1 ? 's' : ''}`;
        if (!filtered.length && this.fileExplorerEmptyMessage) {
            this.fileExplorerEmptyMessage.textContent = this.searchTerm ? 'No results match your search' : 'No files found';
        }
    }

    // Load directory
    async loadDirectory(path = '/') {
        this.currentPath = path;
        this.fileExplorerPath.textContent = path || '/';
        this.renderBreadcrumbs(path);

        this.showLoading();

        const data = await this.fetchDirectory(path);
        if (data && data.success) {
            this.rawItems = data.items || [];
            this.applyFiltersAndSort();
        }
    }

    // Open file (download or inline preview in browser)
    openFile(filePath, fileName) {
        if (!this.sessionId) return;
        const url = `/invoice-chat/explorer/open?path=${encodeURIComponent(filePath)}`;
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

    // Preview file
    async previewFile(filePath, fileName, fileSize) {
        // Show preview panel
        this.filePreviewPanel.classList.remove('hidden');

        // Show loading
        this.filePreviewLoading.classList.remove('hidden');
        this.filePreviewText.classList.add('hidden');
        this.filePreviewBinary.classList.add('hidden');
        this.filePreviewError.classList.add('hidden');
        if (this.filePreviewPdf) this.filePreviewPdf.classList.add('hidden');
        if (this.filePreviewPdfFrame) this.filePreviewPdfFrame.src = '';

        try {
            if (fileName && fileName.toLowerCase().endsWith('.pdf')) {
                const fileUrl = `/invoice-chat/explorer/open?path=${encodeURIComponent(filePath)}`;
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
                    }
                    this.filePreviewPdfFrame.src = objectUrl;
                } else {
                    window.open(objectUrl, '_blank', 'noopener');
                    this.filePreviewLoading.classList.add('hidden');
                }
                setTimeout(() => URL.revokeObjectURL(objectUrl), 30000);
                return;
            }

            const response = await fetch(`/invoice-chat/explorer/preview?path=${encodeURIComponent(filePath)}`, {
                headers: {
                    'X-Session-ID': this.sessionId
                }
            });

            if (!response.ok) {
                throw new Error(`HTTP ${response.status}: ${response.statusText}`);
            }

            const content = await response.text();

            // Check if it's a binary file message or not previewable
            if (content.startsWith('Binary file') || content.includes('File too large') || content.includes('File type not supported for preview')) {
                this.filePreviewBinarySize.textContent = fileSize ? this.formatFileSize(fileSize) : '';
                this.filePreviewBinary.classList.remove('hidden');
            } else {
                let pre = this.filePreviewText.querySelector('pre');
                
                // Create pre element if it doesn't exist
                if (!pre) {
                    console.warn('pre element not found in filePreviewText, creating it');
                    pre = document.createElement('pre');
                    pre.className = 'text-caption-m font-mono whitespace-pre-wrap break-words text-slate-700';
                    this.filePreviewText.appendChild(pre);
                }
                
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
                    pre.className = 'text-caption-m font-mono whitespace-pre-wrap break-words text-slate-700';
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

    // Toggle file explorer
    toggleFileExplorer() {
        const isHidden = this.fileExplorerPanel.classList.contains('hidden');

        if (isHidden) {
            this.fileExplorerPanel.classList.remove('hidden');
            this.fileExplorerPanel.classList.remove('w-0');
            this.fileExplorerPanel.classList.add('w-96');
            this.fileExplorerPanel.classList.add('md:max-w-[24rem]');
            this.fileExplorerPanel.classList.add('min-w-0');

            this.loadDirectory();
            this.startAutoRefresh();
        } else {
            this.fileExplorerPanel.classList.add('hidden');
            this.fileExplorerPanel.classList.add('w-0');
            this.fileExplorerPanel.classList.remove('w-96');
            this.fileExplorerPanel.classList.remove('md:max-w-[24rem]');
            this.fileExplorerPanel.classList.remove('min-w-0');

            this.stopAutoRefresh();
        }
    }

    // Start auto-refresh
    startAutoRefresh() {
        this.stopAutoRefresh(); // Clear any existing interval
        this.autoRefreshInterval = setInterval(() => {
            this.loadDirectory(this.currentPath);
        }, 30000); // Refresh every 30 seconds
    }

    // Stop auto-refresh
    stopAutoRefresh() {
        if (this.autoRefreshInterval) {
            clearInterval(this.autoRefreshInterval);
            this.autoRefreshInterval = null;
        }
    }

    // Initialize event listeners
    initEventListeners() {
        // Note: file-explorer-toggle button has been removed since we now have Files tab
        // The file explorer is now shown/hidden via the tab system

        if (this.fileExplorerClose) {
            this.fileExplorerClose.addEventListener('click', () => {
                this.fileExplorerPanel.classList.add('hidden');
                this.fileExplorerPanel.classList.add('w-0');
                this.fileExplorerPanel.classList.remove('w-96');
                this.fileExplorerPanel.classList.remove('md:max-w-[24rem]');
                this.fileExplorerPanel.classList.remove('min-w-0');
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

        // Search
        if (this.fileExplorerSearch) {
            this.fileExplorerSearch.addEventListener('input', (e) => {
                this.searchTerm = e.target.value || '';
                if (this.fileExplorerSearchClear) {
                    this.fileExplorerSearchClear.classList.toggle('hidden', !this.searchTerm);
                }
                this.applyFiltersAndSort();
            });
        }

        if (this.fileExplorerSearchClear) {
            this.fileExplorerSearchClear.addEventListener('click', () => {
                this.searchTerm = '';
                if (this.fileExplorerSearch) {
                    this.fileExplorerSearch.value = '';
                }
                this.fileExplorerSearchClear.classList.add('hidden');
                this.applyFiltersAndSort();
            });
        }

        // Upload functionality
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

        // Ensure panel width constraints remain applied after script initialization
        if (this.fileExplorerPanel) {
            this.fileExplorerPanel.classList.add('min-w-0');
            this.fileExplorerPanel.classList.add('md:max-w-[24rem]');
        }

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
                this.filePreviewPanel.classList.add('hidden');
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
    }

    // Handle file upload
    async handleFileUpload(files) {
        if (!files || files.length === 0) return;

        console.log(`[file-explorer] Uploading ${files.length} file(s)`);
        
        // Create FormData
        const formData = new FormData();
        for (let i = 0; i < files.length; i++) {
            formData.append('files', files[i]);
        }
        
        // Add current path
        formData.append('path', this.currentPath);
        
        try {
            // Show loading state
            const originalText = this.fileExplorerUpload.innerHTML;
            this.fileExplorerUpload.innerHTML = `
                <svg class="w-5 h-5 animate-spin" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                    <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M4 4v5h.582m15.356 2A8.001 8.001 0 004.582 9m0 0H9m11 11v-5h-.581m0 0a8.003 8.003 0 01-15.357-2m15.357 2H15" />
                </svg>
                <span class="hidden sm:inline">Uploading...</span>
            `;
            this.fileExplorerUpload.disabled = true;

            // Send upload request
            const response = await fetch('/api/files/upload', {
                method: 'POST',
                body: formData
            });

            if (!response.ok) {
                throw new Error(`Upload failed: ${response.status}`);
            }

            const result = await response.json();
            console.log('[file-explorer] Upload successful:', result);

            // Refresh file list
            await this.loadDirectory(this.currentPath);

            // Show success message
            this.showToast(`${files.length} file(s) uploaded successfully`, 'success');

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
        toast.className = `fixed top-4 right-4 z-50 px-4 py-3 rounded-lg shadow-lg animate-fade-in ${
            type === 'success' ? 'bg-green-50 text-green-800 border border-green-200' :
            type === 'error' ? 'bg-red-50 text-red-800 border border-red-200' :
            'bg-blue-50 text-blue-800 border border-blue-200'
        }`;
        
        toast.innerHTML = `
            <div class="flex items-center gap-2">
                <svg class="w-5 h-5" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                    ${type === 'success' ? '<path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M5 13l4 4L19 7" />' :
                      type === 'error' ? '<path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M6 18L18 6M6 6l12 12" />' :
                      '<path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M13 16h-1v-4h-1m1-4h.01M21 12a9 9 0 11-18 0 9 9 0 0118 0z" />'}
                </svg>
                <span class="text-caption-m-bold">${message}</span>
            </div>
        `;
        
        document.body.appendChild(toast);
        
        // Remove toast after 3 seconds
        setTimeout(() => {
            toast.classList.add('animate-fade-out');
            setTimeout(() => {
                if (toast.parentNode) {
                    toast.parentNode.removeChild(toast);
                }
            }, 300);
        }, 3000);
    }
}

// Expose to global scope so other scripts (e.g., invoiceChat) can instantiate it
if (typeof window !== 'undefined') {
    window.FileExplorer = FileExplorer;
}
