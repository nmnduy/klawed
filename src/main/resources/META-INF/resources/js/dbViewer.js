// SQLite Database Viewer Module
// Provides a modal UI for viewing and querying SQLite database files

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

class DBViewer {
    constructor() {
        // State
        this.sessionId = null;
        this.currentDbPath = null;
        this.currentDbName = null;
        this.currentTable = null;
        this.tables = [];
        this.schema = [];
        this.currentPage = 0;
        this.pageSize = 50;
        this.totalRows = 0;
        this.sortColumn = null;
        this.sortDirection = 'asc';
        this.searchTerm = '';
        this.columnFilters = {};
        this.queryHistory = [];
        this.historyIndex = -1;

        // DOM elements (created dynamically)
        this.modal = null;
        this.modalContent = null;

        // Query state
        this.isExecutingQuery = false;
        this.lastQueryResults = null;

        // LocalStorage key prefix
        this.HISTORY_KEY_PREFIX = 'dbViewer_queryHistory_';
        this.MAX_HISTORY_SIZE = 50;

        // Bind methods
        this.open = this.open.bind(this);
        this.close = this.close.bind(this);
        this.handleKeyDown = this.handleKeyDown.bind(this);
    }

    /**
     * Set session ID
     */
    setSession(sessionId) {
        this.sessionId = sessionId;
        this.loadQueryHistory();
    }

    /**
     * Open the viewer modal for a database file
     */
    async open(dbPath, fileName) {
        this.currentDbPath = dbPath;
        this.currentDbName = fileName || dbPath.split('/').pop();
        this.currentTable = null;
        this.tables = [];
        this.schema = [];
        this.currentPage = 0;
        this.totalRows = 0;
        this.sortColumn = null;
        this.sortDirection = 'asc';
        this.searchTerm = '';
        this.columnFilters = {};
        this.lastQueryResults = null;

        // Create modal if not exists
        this.createModal();

        // Show modal
        this.modal.classList.remove('hidden');
        document.body.style.overflow = 'hidden';

        // Add keyboard listener
        document.addEventListener('keydown', this.handleKeyDown);

        // Load tables
        await this.loadTables();
    }

    /**
     * Close the modal
     */
    close() {
        if (this.modal) {
            this.modal.classList.add('hidden');
            document.body.style.overflow = '';
        }
        document.removeEventListener('keydown', this.handleKeyDown);
    }

    /**
     * Handle keyboard shortcuts
     */
    handleKeyDown(e) {
        // Escape to close
        if (e.key === 'Escape') {
            this.close();
            return;
        }

        // Ctrl/Cmd+Enter to run query
        if ((e.ctrlKey || e.metaKey) && e.key === 'Enter') {
            const sqlInput = document.getElementById('dbv-sql-input');
            if (sqlInput && document.activeElement === sqlInput) {
                e.preventDefault();
                this.executeQueryFromInput();
            }
        }

        // Up/Down for history navigation in SQL input
        const sqlInput = document.getElementById('dbv-sql-input');
        if (sqlInput && document.activeElement === sqlInput) {
            if (e.key === 'ArrowUp' && this.isAtStartOfInput(sqlInput)) {
                e.preventDefault();
                this.navigateHistory(-1);
            } else if (e.key === 'ArrowDown' && this.isAtEndOfInput(sqlInput)) {
                e.preventDefault();
                this.navigateHistory(1);
            }
        }
    }

    /**
     * Check if cursor is at start of input
     */
    isAtStartOfInput(input) {
        return input.selectionStart === 0 && input.selectionEnd === 0;
    }

    /**
     * Check if cursor is at end of input
     */
    isAtEndOfInput(input) {
        return input.selectionStart === input.value.length;
    }

    /**
     * Create the modal HTML structure
     */
    createModal() {
        // Remove existing modal if present
        const existingModal = document.getElementById('db-viewer-modal');
        if (existingModal) {
            existingModal.remove();
        }

        this.modal = document.createElement('div');
        this.modal.id = 'db-viewer-modal';
        this.modal.className = 'fixed inset-0 z-50 flex items-center justify-center bg-black/50 backdrop-blur-sm hidden';

        this.modal.innerHTML = `
            <div class="bg-card w-full h-full sm:w-[90vw] sm:h-[90vh] sm:rounded-xl shadow-2xl flex flex-col overflow-hidden border-0 sm:border border-border">
                <!-- Header -->
                <div class="flex items-center justify-between px-3 sm:px-6 py-3 sm:py-4 border-b border-border bg-muted/30">
                    <div class="flex items-center gap-2 sm:gap-3 min-w-0">
                        <svg class="w-5 h-5 sm:w-6 sm:h-6 text-cyan-500 flex-shrink-0" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                            <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M4 7v10c0 2.21 3.582 4 8 4s8-1.79 8-4V7M4 7c0 2.21 3.582 4 8 4s8-1.79 8-4M4 7c0-2.21 3.582-4 8-4s8 1.79 8 4m0 5c0 2.21-3.582 4-8 4s-8-1.79-8-4" />
                        </svg>
                        <h2 class="text-body-m sm:text-heading-s text-foreground truncate" id="dbv-filename">${this.escapeHtml(this.currentDbName)}</h2>
                    </div>
                    <button id="dbv-close" class="p-2 hover:bg-muted rounded-lg transition flex-shrink-0" title="Close (Esc)">
                        <svg class="w-5 h-5 text-muted-foreground" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                            <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M6 18L18 6M6 6l12 12" />
                        </svg>
                    </button>
                </div>

                <!-- Main content -->
                <div class="flex flex-col sm:flex-row flex-1 overflow-hidden">
                    <!-- Left sidebar / Mobile toggle -->
                    <div class="sm:w-64 flex-shrink-0 border-b sm:border-b-0 sm:border-r border-border flex flex-col bg-muted/20">
                        <!-- Tables list with mobile toggle -->
                        <div class="flex-1 overflow-hidden flex flex-col">
                            <button id="dbv-tables-toggle" class="sm:hidden w-full px-3 py-3 flex items-center justify-between hover:bg-muted/50 transition border-b border-border">
                                <h3 class="text-caption-m-bold text-muted-foreground uppercase tracking-wider">Tables</h3>
                                <svg id="dbv-tables-chevron" class="w-4 h-4 text-muted-foreground transform rotate-0 transition-transform" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                                    <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M19 9l-7 7-7-7" />
                                </svg>
                            </button>
                            <div class="hidden sm:block px-3 sm:px-4 py-2 sm:py-3 border-b border-border">
                                <h3 class="text-caption-m-bold text-muted-foreground uppercase tracking-wider">Tables</h3>
                            </div>
                            <div id="dbv-tables-list" class="hidden sm:block flex-1 overflow-y-auto p-2">
                                <div class="flex items-center justify-center py-8">
                                    <svg class="w-5 h-5 animate-spin text-muted-foreground" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                                        <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M4 4v5h.582m15.356 2A8.001 8.001 0 004.582 9m0 0H9m11 11v-5h-.581m0 0a8.003 8.003 0 01-15.357-2m15.357 2H15" />
                                    </svg>
                                </div>
                            </div>
                        </div>

                        <!-- Schema panel -->
                        <div class="border-t border-border">
                            <button id="dbv-schema-toggle" class="w-full px-3 sm:px-4 py-2 sm:py-3 flex items-center justify-between hover:bg-muted/50 transition">
                                <h3 class="text-caption-m-bold text-muted-foreground uppercase tracking-wider">Schema</h3>
                                <svg id="dbv-schema-chevron" class="w-4 h-4 text-muted-foreground transform rotate-0 transition-transform" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                                    <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M19 9l-7 7-7-7" />
                                </svg>
                            </button>
                            <div id="dbv-schema-panel" class="hidden overflow-y-auto max-h-40 sm:max-h-48 p-2 bg-muted/10">
                                <p class="text-caption-s text-muted-foreground px-2 py-4 text-center">Select a table to view schema</p>
                            </div>
                        </div>
                    </div>

                    <!-- Main content area -->
                    <div class="flex-1 flex flex-col overflow-hidden">
                        <!-- SQL Runner section -->
                        <div class="border-b border-border">
                            <button id="dbv-sql-toggle" class="w-full px-3 sm:px-4 py-2 sm:py-3 flex items-center justify-between hover:bg-muted/30 transition">
                                <div class="flex items-center gap-2">
                                    <svg class="w-4 h-4 text-purple-500" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                                        <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M10 20l4-16m4 4l4 4-4 4M6 16l-4-4 4-4" />
                                    </svg>
                                    <h3 class="text-caption-m-bold text-foreground">SQL Runner</h3>
                                </div>
                                <svg id="dbv-sql-chevron" class="w-4 h-4 text-muted-foreground transform rotate-180 transition-transform" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                                    <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M19 9l-7 7-7-7" />
                                </svg>
                            </button>
                            <div id="dbv-sql-section" class="px-3 sm:px-4 pb-3 sm:pb-4">
                                <div class="flex flex-col sm:flex-row gap-2 sm:gap-3">
                                    <div class="flex-1">
                                        <textarea id="dbv-sql-input"
                                            class="w-full h-20 sm:h-20 px-3 py-2 bg-background border border-border rounded-lg text-caption-s sm:text-body-s font-mono text-foreground placeholder-muted-foreground focus:outline-none focus:ring-2 focus:ring-primary/50 resize-none"
                                            placeholder="Enter SQL query... (Ctrl+Enter to run)"></textarea>
                                    </div>
                                    <div class="flex sm:flex-col gap-2">
                                        <button id="dbv-run-query" class="flex-1 sm:flex-none px-3 sm:px-4 py-2 bg-primary text-primary-foreground rounded-lg hover:bg-primary/90 transition text-caption-m-bold flex items-center justify-center gap-2">
                                            <svg class="w-4 h-4" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                                                <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M14.752 11.168l-3.197-2.132A1 1 0 0010 9.87v4.263a1 1 0 001.555.832l3.197-2.132a1 1 0 000-1.664z" />
                                                <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M21 12a9 9 0 11-18 0 9 9 0 0118 0z" />
                                            </svg>
                                            <span class="hidden sm:inline">Run</span>
                                        </button>
                                        <div class="relative flex-1 sm:flex-none">
                                            <button id="dbv-history-btn" class="w-full px-3 sm:px-4 py-2 bg-muted hover:bg-muted/80 text-foreground rounded-lg transition text-caption-m flex items-center justify-center gap-2" title="Query history">
                                                <svg class="w-4 h-4" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                                                    <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M12 8v4l3 3m6-3a9 9 0 11-18 0 9 9 0 0118 0z" />
                                                </svg>
                                                <svg class="w-3 h-3" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                                                    <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M19 9l-7 7-7-7" />
                                                </svg>
                                            </button>
                                            <div id="dbv-history-dropdown" class="hidden absolute left-0 sm:right-0 sm:left-auto top-full mt-1 w-full sm:w-80 max-h-60 overflow-y-auto bg-card rounded-lg shadow-lg border border-border z-20">
                                                <div class="p-2 text-caption-s text-muted-foreground text-center">No query history</div>
                                            </div>
                                        </div>
                                    </div>
                                </div>
                                <!-- Query results -->
                                <div id="dbv-query-results" class="mt-3 hidden">
                                    <div class="bg-muted/30 rounded-lg border border-border overflow-hidden">
                                        <div class="px-3 py-2 border-b border-border flex items-center justify-between">
                                            <span class="text-caption-m-bold text-foreground">Query Results</span>
                                            <span id="dbv-query-stats" class="text-caption-s text-muted-foreground"></span>
                                        </div>
                                        <div id="dbv-query-results-content" class="max-h-40 overflow-auto">
                                        </div>
                                    </div>
                                </div>
                            </div>
                        </div>

                        <!-- Table data section -->
                        <div class="flex-1 flex flex-col overflow-hidden">
                            <!-- Toolbar -->
                            <div id="dbv-toolbar" class="hidden px-3 sm:px-4 py-2 sm:py-3 border-b border-border flex flex-col sm:flex-row items-stretch sm:items-center gap-2 sm:gap-4">
                                <!-- Search -->
                                <div class="flex-1 min-w-0 relative">
                                    <svg class="absolute left-3 top-1/2 -translate-y-1/2 w-4 h-4 text-muted-foreground pointer-events-none" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                                        <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M21 21l-6-6m2-5a7 7 0 11-14 0 7 7 0 0114 0z" />
                                    </svg>
                                    <input id="dbv-search" type="text"
                                        class="w-full pl-10 pr-4 py-2 bg-background border border-border rounded-lg text-caption-s sm:text-body-s text-foreground placeholder-muted-foreground focus:outline-none focus:ring-2 focus:ring-primary/50"
                                        placeholder="Search all columns...">
                                </div>

                                <!-- Export buttons -->
                                <div class="flex items-center gap-2">
                                    <button id="dbv-export-csv" class="flex-1 sm:flex-none px-3 py-2 bg-muted hover:bg-muted/80 text-foreground rounded-lg transition text-caption-m flex items-center justify-center gap-2">
                                        <svg class="w-4 h-4 text-green-500" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                                            <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M12 10v6m0 0l-3-3m3 3l3-3m2 8H7a2 2 0 01-2-2V5a2 2 0 012-2h5.586a1 1 0 01.707.293l5.414 5.414a1 1 0 01.293.707V19a2 2 0 01-2 2z" />
                                        </svg>
                                        CSV
                                    </button>
                                    <button id="dbv-export-json" class="flex-1 sm:flex-none px-3 py-2 bg-muted hover:bg-muted/80 text-foreground rounded-lg transition text-caption-m flex items-center justify-center gap-2">
                                        <svg class="w-4 h-4 text-amber-500" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                                            <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M12 10v6m0 0l-3-3m3 3l3-3m2 8H7a2 2 0 01-2-2V5a2 2 0 012-2h5.586a1 1 0 01.707.293l5.414 5.414a1 1 0 01.293.707V19a2 2 0 01-2 2z" />
                                        </svg>
                                        JSON
                                    </button>
                                </div>
                            </div>

                            <!-- Data table container -->
                            <div id="dbv-data-container" class="flex-1 overflow-auto">
                                <div class="flex items-center justify-center h-full">
                                    <div class="text-center">
                                        <svg class="w-12 h-12 mx-auto text-muted-foreground/50 mb-3" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                                            <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M4 7v10c0 2.21 3.582 4 8 4s8-1.79 8-4V7M4 7c0 2.21 3.582 4 8 4s8-1.79 8-4M4 7c0-2.21 3.582-4 8-4s8 1.79 8 4" />
                                        </svg>
                                        <p class="text-body-s text-muted-foreground">Select a table to view data</p>
                                    </div>
                                </div>
                            </div>

                            <!-- Pagination -->
                            <div id="dbv-pagination" class="hidden px-3 sm:px-4 py-2 sm:py-3 border-t border-border flex flex-col sm:flex-row items-center gap-3 sm:justify-between">
                                <div class="text-caption-s text-muted-foreground text-center sm:text-left">
                                    <span id="dbv-pagination-info">Showing 0 rows</span>
                                </div>
                                <div class="flex items-center gap-2">
                                    <button id="dbv-page-first" class="p-2 hover:bg-muted rounded-lg transition disabled:opacity-50 disabled:cursor-not-allowed touch-manipulation" title="First page">
                                        <svg class="w-4 h-4" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                                            <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M11 19l-7-7 7-7m8 14l-7-7 7-7" />
                                        </svg>
                                    </button>
                                    <button id="dbv-page-prev" class="p-2 hover:bg-muted rounded-lg transition disabled:opacity-50 disabled:cursor-not-allowed touch-manipulation" title="Previous page">
                                        <svg class="w-4 h-4" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                                            <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M15 19l-7-7 7-7" />
                                        </svg>
                                    </button>
                                    <span id="dbv-page-number" class="px-3 py-1 text-caption-m text-foreground whitespace-nowrap">Page 1</span>
                                    <button id="dbv-page-next" class="p-2 hover:bg-muted rounded-lg transition disabled:opacity-50 disabled:cursor-not-allowed touch-manipulation" title="Next page">
                                        <svg class="w-4 h-4" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                                            <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M9 5l7 7-7 7" />
                                        </svg>
                                    </button>
                                    <button id="dbv-page-last" class="p-2 hover:bg-muted rounded-lg transition disabled:opacity-50 disabled:cursor-not-allowed touch-manipulation" title="Last page">
                                        <svg class="w-4 h-4" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                                            <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M13 5l7 7-7 7M5 5l7 7-7 7" />
                                        </svg>
                                    </button>
                                </div>
                            </div>
                        </div>
                    </div>
                </div>
            </div>
        `;

        document.body.appendChild(this.modal);
        this.attachEventListeners();
    }

    /**
     * Attach event listeners to modal elements
     */
    attachEventListeners() {
        // Close button
        document.getElementById('dbv-close').addEventListener('click', () => this.close());

        // Click outside to close
        this.modal.addEventListener('click', (e) => {
            if (e.target === this.modal) {
                this.close();
            }
        });

        // Tables toggle (mobile)
        const tablesToggle = document.getElementById('dbv-tables-toggle');
        if (tablesToggle) {
            tablesToggle.addEventListener('click', () => {
                const tablesList = document.getElementById('dbv-tables-list');
                const tablesChevron = document.getElementById('dbv-tables-chevron');
                tablesList.classList.toggle('hidden');
                tablesList.classList.toggle('block');
                tablesChevron.classList.toggle('rotate-180');
            });
        }

        // Schema toggle
        document.getElementById('dbv-schema-toggle').addEventListener('click', () => {
            const panel = document.getElementById('dbv-schema-panel');
            const chevron = document.getElementById('dbv-schema-chevron');
            panel.classList.toggle('hidden');
            chevron.classList.toggle('rotate-180');
        });

        // SQL section toggle
        document.getElementById('dbv-sql-toggle').addEventListener('click', () => {
            const section = document.getElementById('dbv-sql-section');
            const chevron = document.getElementById('dbv-sql-chevron');
            section.classList.toggle('hidden');
            chevron.classList.toggle('rotate-180');
        });

        // Run query button
        document.getElementById('dbv-run-query').addEventListener('click', () => {
            this.executeQueryFromInput();
        });

        // History dropdown
        const historyBtn = document.getElementById('dbv-history-btn');
        const historyDropdown = document.getElementById('dbv-history-dropdown');

        historyBtn.addEventListener('click', () => {
            historyDropdown.classList.toggle('hidden');
            if (!historyDropdown.classList.contains('hidden')) {
                this.renderHistoryDropdown();
            }
        });

        // Close history dropdown when clicking outside
        document.addEventListener('click', (e) => {
            if (!historyBtn.contains(e.target) && !historyDropdown.contains(e.target)) {
                historyDropdown.classList.add('hidden');
            }
        });

        // Search input
        const searchInput = document.getElementById('dbv-search');
        let searchTimeout;
        searchInput.addEventListener('input', (e) => {
            clearTimeout(searchTimeout);
            searchTimeout = setTimeout(() => {
                this.searchTerm = e.target.value;
                this.currentPage = 0;
                this.loadData();
            }, 300);
        });

        // Export buttons
        document.getElementById('dbv-export-csv').addEventListener('click', () => {
            this.exportData('csv');
        });
        document.getElementById('dbv-export-json').addEventListener('click', () => {
            this.exportData('json');
        });

        // Pagination buttons
        document.getElementById('dbv-page-first').addEventListener('click', () => {
            this.handlePageChange(0);
        });
        document.getElementById('dbv-page-prev').addEventListener('click', () => {
            this.handlePageChange(this.currentPage - 1);
        });
        document.getElementById('dbv-page-next').addEventListener('click', () => {
            this.handlePageChange(this.currentPage + 1);
        });
        document.getElementById('dbv-page-last').addEventListener('click', () => {
            const lastPage = Math.max(0, Math.ceil(this.totalRows / this.pageSize) - 1);
            this.handlePageChange(lastPage);
        });
    }

    /**
     * Load list of tables from database
     */
    async loadTables() {
        const tablesList = document.getElementById('dbv-tables-list');
        tablesList.innerHTML = `
            <div class="flex items-center justify-center py-8">
                <svg class="w-5 h-5 animate-spin text-muted-foreground" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                    <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M4 4v5h.582m15.356 2A8.001 8.001 0 004.582 9m0 0H9m11 11v-5h-.581m0 0a8.003 8.003 0 01-15.357-2m15.357 2H15" />
                </svg>
            </div>
        `;

        try {
            const response = await fetch(`/app/explorer/sql/tables?path=${encodeURIComponent(this.currentDbPath)}`, {
                headers: {
                    'X-Session-ID': this.sessionId
                }
            });

            if (await handleAuthError(response)) {
                return;
            }

            if (!response.ok) {
                throw new Error(`HTTP ${response.status}: ${response.statusText}`);
            }

            const data = await response.json();

            if (data.success) {
                this.tables = data.tables || [];
                this.renderTablesList();
            } else {
                throw new Error(data.error || 'Failed to load tables');
            }
        } catch (error) {
            console.error('[db-viewer] Failed to load tables:', error);
            tablesList.innerHTML = `
                <div class="px-3 py-4 text-center">
                    <svg class="w-8 h-8 mx-auto text-red-400 mb-2" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                        <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M12 9v2m0 4h.01m-6.938 4h13.856c1.54 0 2.502-1.667 1.732-3L13.732 4c-.77-1.333-2.694-1.333-3.464 0L3.34 16c-.77 1.333.192 3 1.732 3z" />
                    </svg>
                    <p class="text-caption-s text-red-500">${this.escapeHtml(error.message)}</p>
                </div>
            `;
        }
    }

    /**
     * Render the tables list in the sidebar
     */
    renderTablesList() {
        const tablesList = document.getElementById('dbv-tables-list');

        if (this.tables.length === 0) {
            tablesList.innerHTML = `
                <div class="px-3 py-4 text-center text-caption-s text-muted-foreground">
                    No tables found in database
                </div>
            `;
            return;
        }

        tablesList.innerHTML = this.tables.map(table => `
            <button class="dbv-table-item w-full text-left px-3 py-2.5 rounded-lg hover:bg-muted/50 transition flex items-center gap-2 group touch-manipulation ${this.currentTable === table ? 'bg-primary/10 text-primary' : 'text-foreground'}"
                data-table="${this.escapeHtml(table)}">
                <svg class="w-4 h-4 flex-shrink-0 ${this.currentTable === table ? 'text-primary' : 'text-muted-foreground group-hover:text-foreground'}" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                    <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M3 10h18M3 14h18m-9-4v8m-7 0h14a2 2 0 002-2V8a2 2 0 00-2-2H5a2 2 0 00-2 2v8a2 2 0 002 2z" />
                </svg>
                <span class="text-caption-m truncate">${this.escapeHtml(table)}</span>
            </button>
        `).join('');

        // Attach click handlers
        tablesList.querySelectorAll('.dbv-table-item').forEach(btn => {
            btn.addEventListener('click', () => {
                this.selectTable(btn.dataset.table);
                // On mobile, collapse the tables list after selection
                if (window.innerWidth < 640) { // sm breakpoint
                    tablesList.classList.add('hidden');
                    const tablesChevron = document.getElementById('dbv-tables-chevron');
                    if (tablesChevron) {
                        tablesChevron.classList.remove('rotate-180');
                    }
                }
            });
        });
    }

    /**
     * Select a table and load its data
     */
    async selectTable(tableName) {
        this.currentTable = tableName;
        this.currentPage = 0;
        this.searchTerm = '';
        this.columnFilters = {};
        this.sortColumn = null;
        this.sortDirection = 'asc';

        // Update search input
        const searchInput = document.getElementById('dbv-search');
        if (searchInput) {
            searchInput.value = '';
        }

        // Update tables list to show selection
        this.renderTablesList();

        // Show toolbar and pagination
        document.getElementById('dbv-toolbar').classList.remove('hidden');
        document.getElementById('dbv-pagination').classList.remove('hidden');

        // Load schema and data in parallel
        await Promise.all([
            this.loadSchema(tableName),
            this.loadData()
        ]);
    }

    /**
     * Load schema for a table
     */
    async loadSchema(tableName) {
        const schemaPanel = document.getElementById('dbv-schema-panel');
        schemaPanel.innerHTML = `
            <div class="flex items-center justify-center py-4">
                <svg class="w-4 h-4 animate-spin text-muted-foreground" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                    <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M4 4v5h.582m15.356 2A8.001 8.001 0 004.582 9m0 0H9m11 11v-5h-.581m0 0a8.003 8.003 0 01-15.357-2m15.357 2H15" />
                </svg>
            </div>
        `;

        // Expand schema panel
        schemaPanel.classList.remove('hidden');
        document.getElementById('dbv-schema-chevron').classList.add('rotate-180');

        try {
            const response = await fetch(`/app/explorer/sql/schema/${encodeURIComponent(tableName)}?path=${encodeURIComponent(this.currentDbPath)}`, {
                headers: {
                    'X-Session-ID': this.sessionId
                }
            });

            if (await handleAuthError(response)) {
                return;
            }

            if (!response.ok) {
                throw new Error(`HTTP ${response.status}: ${response.statusText}`);
            }

            const data = await response.json();

            if (data.success) {
                this.schema = data.columns || [];
                this.renderSchema();
            } else {
                throw new Error(data.error || 'Failed to load schema');
            }
        } catch (error) {
            console.error('[db-viewer] Failed to load schema:', error);
            schemaPanel.innerHTML = `
                <p class="text-caption-s text-red-500 px-2 py-2">${this.escapeHtml(error.message)}</p>
            `;
        }
    }

    /**
     * Render the schema panel
     */
    renderSchema() {
        const schemaPanel = document.getElementById('dbv-schema-panel');

        if (this.schema.length === 0) {
            schemaPanel.innerHTML = `
                <p class="text-caption-s text-muted-foreground px-2 py-4 text-center">No columns found</p>
            `;
            return;
        }

        schemaPanel.innerHTML = `
            <div class="space-y-1">
                ${this.schema.map(col => `
                    <div class="px-2 py-1.5 rounded hover:bg-muted/30 flex items-center gap-2">
                        <span class="text-caption-m text-foreground font-mono">${this.escapeHtml(col.name)}</span>
                        <span class="text-caption-s text-muted-foreground">${this.escapeHtml(col.type)}</span>
                        ${col.pk ? '<span class="text-[10px] px-1.5 py-0.5 rounded bg-amber-100 dark:bg-amber-900/30 text-amber-700 dark:text-amber-300">PK</span>' : ''}
                        ${col.notnull ? '<span class="text-[10px] px-1.5 py-0.5 rounded bg-blue-100 dark:bg-blue-900/30 text-blue-700 dark:text-blue-300">NOT NULL</span>' : ''}
                    </div>
                `).join('')}
            </div>
        `;
    }

    /**
     * Load table data with pagination, search, and filters
     */
    async loadData() {
        if (!this.currentTable) return;

        const dataContainer = document.getElementById('dbv-data-container');
        dataContainer.innerHTML = `
            <div class="flex items-center justify-center h-full">
                <svg class="w-8 h-8 animate-spin text-muted-foreground" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                    <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M4 4v5h.582m15.356 2A8.001 8.001 0 004.582 9m0 0H9m11 11v-5h-.581m0 0a8.003 8.003 0 01-15.357-2m15.357 2H15" />
                </svg>
            </div>
        `;

        try {
            // Build query params
            const params = new URLSearchParams({
                path: this.currentDbPath,
                page: this.currentPage,
                pageSize: this.pageSize
            });

            if (this.searchTerm) {
                params.append('search', this.searchTerm);
            }

            if (this.sortColumn) {
                params.append('sortColumn', this.sortColumn);
                params.append('sortDirection', this.sortDirection);
            }

            if (Object.keys(this.columnFilters).length > 0) {
                params.append('filters', JSON.stringify(this.columnFilters));
            }

            const response = await fetch(`/app/explorer/sql/data/${encodeURIComponent(this.currentTable)}?${params}`, {
                headers: {
                    'X-Session-ID': this.sessionId
                }
            });

            if (await handleAuthError(response)) {
                return;
            }

            if (!response.ok) {
                throw new Error(`HTTP ${response.status}: ${response.statusText}`);
            }

            const data = await response.json();

            if (data.success) {
                this.totalRows = data.totalRows || 0;
                this.renderDataTable(data.columns || [], data.rows || []);
                this.renderPagination();
            } else {
                throw new Error(data.error || 'Failed to load data');
            }
        } catch (error) {
            console.error('[db-viewer] Failed to load data:', error);
            dataContainer.innerHTML = `
                <div class="flex items-center justify-center h-full">
                    <div class="text-center">
                        <svg class="w-12 h-12 mx-auto text-red-400 mb-3" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                            <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M12 9v2m0 4h.01m-6.938 4h13.856c1.54 0 2.502-1.667 1.732-3L13.732 4c-.77-1.333-2.694-1.333-3.464 0L3.34 16c-.77 1.333.192 3 1.732 3z" />
                        </svg>
                        <p class="text-body-s text-red-500">${this.escapeHtml(error.message)}</p>
                    </div>
                </div>
            `;
        }
    }

    /**
     * Render the data table
     */
    renderDataTable(columns, rows) {
        const dataContainer = document.getElementById('dbv-data-container');

        if (rows.length === 0) {
            dataContainer.innerHTML = `
                <div class="flex items-center justify-center h-full">
                    <div class="text-center">
                        <svg class="w-12 h-12 mx-auto text-muted-foreground/50 mb-3" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                            <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M20 13V6a2 2 0 00-2-2H6a2 2 0 00-2 2v7m16 0v5a2 2 0 01-2 2H6a2 2 0 01-2-2v-5m16 0h-2.586a1 1 0 00-.707.293l-2.414 2.414a1 1 0 01-.707.293h-3.172a1 1 0 01-.707-.293l-2.414-2.414A1 1 0 006.586 13H4" />
                        </svg>
                        <p class="text-body-s text-muted-foreground">No data found</p>
                    </div>
                </div>
            `;
            return;
        }

        // Build table HTML
        const tableHtml = `
            <div class="overflow-x-auto">
                <table class="w-full border-collapse min-w-max">
                <thead class="sticky top-0 bg-muted/50 backdrop-blur-sm z-10">
                    <tr>
                        ${columns.map(col => `
                            <th class="px-3 sm:px-4 py-2.5 sm:py-3 text-left text-caption-m-bold text-foreground border-b border-border cursor-pointer hover:bg-muted/70 active:bg-muted transition select-none whitespace-nowrap touch-manipulation"
                                data-column="${this.escapeHtml(col)}">
                                <div class="flex items-center gap-2">
                                    <span>${this.escapeHtml(col)}</span>
                                    ${this.sortColumn === col ? `
                                        <svg class="w-4 h-4 text-primary ${this.sortDirection === 'desc' ? 'rotate-180' : ''}" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                                            <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M5 15l7-7 7 7" />
                                        </svg>
                                    ` : ''}
                                </div>
                            </th>
                        `).join('')}
                    </tr>
                    <!-- Column filter row -->
                    <tr class="bg-muted/30">
                        ${columns.map(col => `
                            <th class="px-2 py-2 border-b border-border">
                                <input type="text"
                                    class="w-full min-w-[100px] px-2 py-1.5 text-caption-s bg-background border border-border rounded focus:outline-none focus:ring-1 focus:ring-primary/50"
                                    placeholder="Filter..."
                                    data-filter-column="${this.escapeHtml(col)}"
                                    value="${this.escapeHtml(this.columnFilters[col] || '')}">
                            </th>
                        `).join('')}
                    </tr>
                </thead>
                <tbody>
                    ${rows.map((row, rowIndex) => `
                        <tr class="hover:bg-muted/30 transition ${rowIndex % 2 === 0 ? '' : 'bg-muted/10'}">
                            ${columns.map(col => {
                                const value = row[col];
                                const isNull = value === null || value === undefined;
                                const displayValue = isNull ? 'NULL' : String(value);
                                const truncated = displayValue.length > 100;
                                return `
                                    <td class="px-3 sm:px-4 py-2 sm:py-2.5 text-caption-s sm:text-body-s border-b border-border/50 ${isNull ? 'text-muted-foreground italic' : 'text-foreground'}"
                                        title="${this.escapeHtml(displayValue)}">
                                        <span class="block max-w-[200px] sm:max-w-xs truncate">${this.escapeHtml(truncated ? displayValue.substring(0, 100) + '...' : displayValue)}</span>
                                    </td>
                                `;
                            }).join('')}
                        </tr>
                    `).join('')}
                </tbody>
                </table>
            </div>
        `;

        dataContainer.innerHTML = tableHtml;

        // Attach sort handlers
        dataContainer.querySelectorAll('thead th[data-column]').forEach(th => {
            th.addEventListener('click', () => {
                this.handleSort(th.dataset.column);
            });
        });

        // Attach filter handlers
        dataContainer.querySelectorAll('input[data-filter-column]').forEach(input => {
            let filterTimeout;
            input.addEventListener('input', (e) => {
                clearTimeout(filterTimeout);
                filterTimeout = setTimeout(() => {
                    this.handleColumnFilter(input.dataset.filterColumn, e.target.value);
                }, 300);
            });
        });
    }

    /**
     * Render pagination controls
     */
    renderPagination() {
        const totalPages = Math.ceil(this.totalRows / this.pageSize);
        const startRow = this.currentPage * this.pageSize + 1;
        const endRow = Math.min((this.currentPage + 1) * this.pageSize, this.totalRows);

        // Update info text
        document.getElementById('dbv-pagination-info').textContent =
            this.totalRows === 0 ? 'No rows' :
            `Showing ${startRow}-${endRow} of ${this.totalRows} rows`;

        // Update page number
        document.getElementById('dbv-page-number').textContent =
            totalPages === 0 ? 'No pages' : `Page ${this.currentPage + 1} of ${totalPages}`;

        // Update button states
        document.getElementById('dbv-page-first').disabled = this.currentPage === 0;
        document.getElementById('dbv-page-prev').disabled = this.currentPage === 0;
        document.getElementById('dbv-page-next').disabled = this.currentPage >= totalPages - 1;
        document.getElementById('dbv-page-last').disabled = this.currentPage >= totalPages - 1;
    }

    /**
     * Execute SQL query from input
     */
    async executeQueryFromInput() {
        const sqlInput = document.getElementById('dbv-sql-input');
        const sql = sqlInput.value.trim();

        if (!sql) {
            return;
        }

        await this.executeQuery(sql);
    }

    /**
     * Execute SQL query
     */
    async executeQuery(sql) {
        if (this.isExecutingQuery) {
            return;
        }

        this.isExecutingQuery = true;
        const runButton = document.getElementById('dbv-run-query');
        const queryResults = document.getElementById('dbv-query-results');
        const queryResultsContent = document.getElementById('dbv-query-results-content');
        const queryStats = document.getElementById('dbv-query-stats');

        // Show loading state
        runButton.disabled = true;
        runButton.innerHTML = `
            <svg class="w-4 h-4 animate-spin" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M4 4v5h.582m15.356 2A8.001 8.001 0 004.582 9m0 0H9m11 11v-5h-.581m0 0a8.003 8.003 0 01-15.357-2m15.357 2H15" />
            </svg>
            Running...
        `;

        queryResults.classList.remove('hidden');
        queryResultsContent.innerHTML = `
            <div class="flex items-center justify-center py-4">
                <svg class="w-5 h-5 animate-spin text-muted-foreground" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                    <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M4 4v5h.582m15.356 2A8.001 8.001 0 004.582 9m0 0H9m11 11v-5h-.581m0 0a8.003 8.003 0 01-15.357-2m15.357 2H15" />
                </svg>
            </div>
        `;

        try {
            const response = await fetch(`/app/explorer/sql/query?path=${encodeURIComponent(this.currentDbPath)}`, {
                method: 'POST',
                headers: {
                    'X-Session-ID': this.sessionId,
                    'Content-Type': 'application/json'
                },
                body: JSON.stringify({
                    sql: sql,
                    params: []
                })
            });

            if (await handleAuthError(response)) {
                return;
            }

            if (!response.ok) {
                throw new Error(`HTTP ${response.status}: ${response.statusText}`);
            }

            const data = await response.json();

            if (data.success) {
                this.lastQueryResults = data;
                this.addToHistory(sql);
                this.renderQueryResults(data);
                queryStats.textContent = `${data.rowCount || 0} rows returned in ${data.executionTime || 0}ms`;
            } else {
                throw new Error(data.error || 'Query execution failed');
            }
        } catch (error) {
            console.error('[db-viewer] Query execution failed:', error);
            queryResultsContent.innerHTML = `
                <div class="px-4 py-3 text-caption-m text-red-500">
                    <span class="font-bold">Error:</span> ${this.escapeHtml(error.message)}
                </div>
            `;
            queryStats.textContent = 'Query failed';
        } finally {
            this.isExecutingQuery = false;
            runButton.disabled = false;
            runButton.innerHTML = `
                <svg class="w-4 h-4" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                    <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M14.752 11.168l-3.197-2.132A1 1 0 0010 9.87v4.263a1 1 0 001.555.832l3.197-2.132a1 1 0 000-1.664z" />
                    <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M21 12a9 9 0 11-18 0 9 9 0 0118 0z" />
                </svg>
                Run
            `;
        }
    }

    /**
     * Render query results
     */
    renderQueryResults(results) {
        const queryResultsContent = document.getElementById('dbv-query-results-content');

        if (!results.rows || results.rows.length === 0) {
            queryResultsContent.innerHTML = `
                <div class="px-4 py-3 text-caption-m text-muted-foreground">
                    Query executed successfully. No rows returned.
                </div>
            `;
            return;
        }

        const columns = results.columns || Object.keys(results.rows[0] || {});

        queryResultsContent.innerHTML = `
            <table class="w-full border-collapse min-w-max">
                <thead class="sticky top-0 bg-muted/50">
                    <tr>
                        ${columns.map(col => `
                            <th class="px-3 py-2 text-left text-caption-s text-foreground border-b border-border whitespace-nowrap">
                                ${this.escapeHtml(col)}
                            </th>
                        `).join('')}
                    </tr>
                </thead>
                <tbody>
                    ${results.rows.map((row, rowIndex) => `
                        <tr class="${rowIndex % 2 === 0 ? '' : 'bg-muted/10'}">
                            ${columns.map(col => {
                                const value = row[col];
                                const isNull = value === null || value === undefined;
                                const displayValue = isNull ? 'NULL' : String(value);
                                return `
                                    <td class="px-3 py-1.5 text-caption-s border-b border-border/50 ${isNull ? 'text-muted-foreground italic' : 'text-foreground'}"
                                        title="${this.escapeHtml(displayValue)}">
                                        <span class="block max-w-[200px] truncate">${this.escapeHtml(displayValue)}</span>
                                    </td>
                                `;
                            }).join('')}
                        </tr>
                    `).join('')}
                </tbody>
            </table>
        `;
    }

    /**
     * Export table data
     */
    async exportData(format) {
        if (!this.currentTable) {
            return;
        }

        try {
            const response = await fetch(`/app/explorer/sql/export/${encodeURIComponent(this.currentTable)}?path=${encodeURIComponent(this.currentDbPath)}&format=${format}`, {
                headers: {
                    'X-Session-ID': this.sessionId
                }
            });

            if (await handleAuthError(response)) {
                return;
            }

            if (!response.ok) {
                throw new Error(`HTTP ${response.status}: ${response.statusText}`);
            }

            const blob = await response.blob();
            const url = URL.createObjectURL(blob);

            const a = document.createElement('a');
            a.href = url;
            a.download = `${this.currentTable}.${format}`;
            document.body.appendChild(a);
            a.click();
            document.body.removeChild(a);

            setTimeout(() => URL.revokeObjectURL(url), 30000);
        } catch (error) {
            console.error('[db-viewer] Export failed:', error);
            alert('Export failed: ' + error.message);
        }
    }

    /**
     * Handle search input
     */
    handleSearch(term) {
        this.searchTerm = term;
        this.currentPage = 0;
        this.loadData();
    }

    /**
     * Handle column filter
     */
    handleColumnFilter(column, value) {
        if (value) {
            this.columnFilters[column] = value;
        } else {
            delete this.columnFilters[column];
        }
        this.currentPage = 0;
        this.loadData();
    }

    /**
     * Handle column sort
     */
    handleSort(column) {
        if (this.sortColumn === column) {
            this.sortDirection = this.sortDirection === 'asc' ? 'desc' : 'asc';
        } else {
            this.sortColumn = column;
            this.sortDirection = 'asc';
        }
        this.currentPage = 0;
        this.loadData();
    }

    /**
     * Handle page change
     */
    handlePageChange(page) {
        const totalPages = Math.ceil(this.totalRows / this.pageSize);
        if (page < 0 || page >= totalPages) {
            return;
        }
        this.currentPage = page;
        this.loadData();
    }

    /**
     * Load query history from localStorage
     */
    loadQueryHistory() {
        if (!this.sessionId) {
            return;
        }

        try {
            const key = this.HISTORY_KEY_PREFIX + this.sessionId;
            const stored = localStorage.getItem(key);
            if (stored) {
                this.queryHistory = JSON.parse(stored);
            }
        } catch (error) {
            console.warn('[db-viewer] Failed to load query history:', error);
            this.queryHistory = [];
        }
    }

    /**
     * Save query history to localStorage
     */
    saveQueryHistory() {
        if (!this.sessionId) {
            return;
        }

        try {
            const key = this.HISTORY_KEY_PREFIX + this.sessionId;
            localStorage.setItem(key, JSON.stringify(this.queryHistory));
        } catch (error) {
            console.warn('[db-viewer] Failed to save query history:', error);
        }
    }

    /**
     * Add query to history
     */
    addToHistory(sql) {
        // Don't add duplicates of the most recent query
        if (this.queryHistory.length > 0 && this.queryHistory[0] === sql) {
            return;
        }

        // Add to beginning
        this.queryHistory.unshift(sql);

        // Trim to max size
        if (this.queryHistory.length > this.MAX_HISTORY_SIZE) {
            this.queryHistory = this.queryHistory.slice(0, this.MAX_HISTORY_SIZE);
        }

        // Reset history navigation
        this.historyIndex = -1;

        this.saveQueryHistory();
    }

    /**
     * Navigate query history
     */
    navigateHistory(direction) {
        const sqlInput = document.getElementById('dbv-sql-input');
        if (!sqlInput || this.queryHistory.length === 0) {
            return;
        }

        const newIndex = this.historyIndex + direction;

        if (newIndex < -1) {
            return;
        }

        if (newIndex >= this.queryHistory.length) {
            return;
        }

        this.historyIndex = newIndex;

        if (this.historyIndex === -1) {
            sqlInput.value = '';
        } else {
            sqlInput.value = this.queryHistory[this.historyIndex];
        }

        // Move cursor to end
        sqlInput.selectionStart = sqlInput.value.length;
        sqlInput.selectionEnd = sqlInput.value.length;
    }

    /**
     * Render history dropdown
     */
    renderHistoryDropdown() {
        const dropdown = document.getElementById('dbv-history-dropdown');

        if (this.queryHistory.length === 0) {
            dropdown.innerHTML = `
                <div class="p-3 text-caption-s text-muted-foreground text-center">No query history</div>
            `;
            return;
        }

        dropdown.innerHTML = this.queryHistory.map((sql, index) => `
            <button class="w-full text-left px-3 py-2 hover:bg-muted/50 transition text-caption-s text-foreground font-mono truncate"
                data-history-index="${index}"
                title="${this.escapeHtml(sql)}">
                ${this.escapeHtml(sql.length > 60 ? sql.substring(0, 60) + '...' : sql)}
            </button>
        `).join('');

        // Attach click handlers
        dropdown.querySelectorAll('button').forEach(btn => {
            btn.addEventListener('click', () => {
                const index = parseInt(btn.dataset.historyIndex, 10);
                const sqlInput = document.getElementById('dbv-sql-input');
                if (sqlInput && this.queryHistory[index]) {
                    sqlInput.value = this.queryHistory[index];
                    dropdown.classList.add('hidden');
                }
            });
        });
    }

    /**
     * Escape HTML special characters
     */
    escapeHtml(str) {
        if (str === null || str === undefined) {
            return '';
        }
        return String(str)
            .replace(/&/g, '&amp;')
            .replace(/</g, '&lt;')
            .replace(/>/g, '&gt;')
            .replace(/"/g, '&quot;')
            .replace(/'/g, '&#39;');
    }
}

// Expose to global scope
if (typeof window !== 'undefined') {
    window.DBViewer = DBViewer;
}
