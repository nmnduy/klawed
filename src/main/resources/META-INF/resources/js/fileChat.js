// Invoice Chat module
import { TabManager } from './tabManager.js';
import { initMobileMenu } from './mobileMenu.js';
import { handleAuthError, isAuthRequired } from './authUtils.js';
import { parseMarkdown, containsMarkdown } from './markdownUtils.js';

const DEBUG = false;
const PLACEHOLDER_MESSAGES_ENABLED = false; // demo seed disabled
const TOUR_STORAGE_KEY = 'filesurfTourCompleted';
const TOUR_URL_FLAG = 'showTour';

function debug(...args) {
    if (DEBUG) {
        console.debug('[invoice-chat]', ...args);
    }
}

export function init(rootEl) {
    if (!rootEl) {
        console.warn('[invoice-chat] init called without root element');
        return;
    }
    if (rootEl.__invoiceChatInitialized) {
        return;
    }
    rootEl.__invoiceChatInitialized = true;

    const elements = {
        statusIndicator: rootEl.querySelector('[data-status-indicator]'),
        statusPulse: rootEl.querySelector('[data-status-pulse]'),
        statusText: rootEl.querySelector('[data-status-text]'),
        // Mobile status pill elements
        mobileStatusPill: rootEl.querySelector('[data-mobile-status-pill]'),
        mobileStatusIndicator: rootEl.querySelector('[data-mobile-status-indicator]'),
        mobileStatusPulse: rootEl.querySelector('[data-mobile-status-pulse]'),
        mobileStatusText: rootEl.querySelector('[data-mobile-status-text]'),
        chatContainer: rootEl.querySelector('[data-chat-container]'),
        chatMessages: rootEl.querySelector('[data-chat-messages]'),
        chatEmpty: rootEl.querySelector('[data-chat-empty]'),
        messageForm: rootEl.querySelector('[data-message-form]'),
        messageInput: rootEl.querySelector('[data-message-input]'),
        sendButtons: rootEl.querySelectorAll('[data-send-button]'), // Changed to querySelectorAll to get both mobile and desktop buttons
        // Chat-specific upload components (scoped to avoid conflicts with file explorer)
        chatUploadButton: rootEl.querySelector('[data-chat-upload-button]'),
        chatFileInput: rootEl.querySelector('[data-chat-file-input]'),
        voiceButton: rootEl.querySelector('[data-voice-button]')
    };

    const messageParent = elements.chatMessages || elements.chatContainer || rootEl;
    const scrollContainer = elements.chatContainer || elements.chatMessages || rootEl;

    // Initialize mobile menu
    try {
        initMobileMenu(rootEl);
    } catch (error) {
        console.warn('[file-chat] Failed to initialize mobile menu:', error);
    }

    // Initialize tab manager
    let tabManager = null;
    try {
        tabManager = new TabManager(rootEl);

        // Listen for tab switches to load file explorer when Files tab is activated
        rootEl.addEventListener('tab-switched', (event) => {
            const { tabId } = event.detail;
            console.log('[file-chat] Tab switched to:', tabId);
            if (tabId === 'file') {
                console.log('[file-chat] Files tab activated, sessionId:', sessionId);
                // Ensure file explorer is initialized
                ensureFileExplorer();
                // Set session if available
                if (sessionId && fileExplorer && typeof fileExplorer.setSession === 'function') {
                    console.log('[file-chat] Setting file explorer session');
                    fileExplorer.setSession(sessionId, null);
                } else {
                    console.warn('[file-chat] Cannot set session - sessionId:', sessionId, 'fileExplorer:', !!fileExplorer);
                }
                // Load the file explorer when Files tab is activated
                if (fileExplorer) {
                    console.log('[file-chat] Loading file explorer directory');
                    fileExplorer.loadDirectory();
                    // Start auto-refresh for file explorer
                    if (typeof fileExplorer.startAutoRefresh === 'function') {
                        fileExplorer.startAutoRefresh();
                    }
                } else {
                    console.error('[file-chat] fileExplorer is null, cannot load directory');
                }

            } else if (fileExplorer && typeof fileExplorer.stopAutoRefresh === 'function') {
                // Stop auto-refresh when switching away from Files tab
                console.log('[file-chat] Stopping auto-refresh for Files tab');
                fileExplorer.stopAutoRefresh();
            }
        });
    } catch (error) {
        console.warn('[invoice-chat] Failed to initialize tab manager:', error);
    }

    // NOTE: Don't load file explorer on page load - wait for WebSocket connection
    // to establish and set sessionId first. This is handled in setFileExplorerSession()
    // which is called after connectWebSocket() completes.
    console.log('[file-chat] Page load - checking active tab');
    let isFilesTabVisibleOnLoad = false;
    if (tabManager) {
        const activeTab = tabManager.getActiveTab();
        console.log('[file-chat] Active tab on page load:', activeTab ? activeTab.id : 'none');

        // On desktop (>= 1024px), both panels are visible and activeTab is null
        // On mobile, only the active tab is visible
        const isDesktop = window.innerWidth >= 1024;
        console.log('[file-chat] Is desktop:', isDesktop);

        if (isDesktop) {
            // On desktop, file explorer panel is always visible
            isFilesTabVisibleOnLoad = true;
            console.log('[file-chat] Desktop mode: file explorer is visible');
        } else if (activeTab && activeTab.id === 'file') {
            // On mobile, check if files tab is the active one
            isFilesTabVisibleOnLoad = true;
            console.log('[file-chat] Mobile mode: files tab is active');
        }
    }

    let ws = null;
    let isConnected = false;
    let sessionId = null;
    let fileExplorer = null;
    let currentStreamMessageId = null;
    let currentStreamContent = '';
    let hasSeededPlaceholders = false;
    let lastApiCallTime = null;

    // Tool activity tracking
    let toolActivityEl = null;
    let pendingTools = new Map(); // Map<toolId, {toolName, startTime, element}>
    let completedTools = []; // Array of {toolName, toolId, isError, duration}

    // Auto-scroll behavior tracking
    let hasReceivedFirstAiResponse = false;
    let isUserScrolledUp = false;
    let scrollToBottomButton = null;
    let scrollUpdateTimeout = null; // For debouncing scroll events
    let pendingScrollFrame = null; // Track pending scroll animation frame

    // Reconnection logic with exponential backoff
    let reconnectAttempts = 0;
    const MAX_RECONNECT_ATTEMPTS = 10;
    const BASE_RECONNECT_DELAY = 1000; // Start with 1 second
    const MAX_RECONNECT_DELAY = 30000; // Cap at 30 seconds
    let reconnectTimeoutId = null;
    let isManuallyDisconnected = false;
    let isAuthFailure = false; // Prevent reconnection after auth failure

    // --- Guided Tour State ---
    let hasCompletedTour = false;


    function setDisabledState(disabled) {
        if (elements.messageInput) elements.messageInput.disabled = disabled;
        // Enable/disable all send buttons (both mobile and desktop)
        if (elements.sendButtons) {
            elements.sendButtons.forEach(btn => btn.disabled = disabled);
        }
        if (elements.chatUploadButton) elements.chatUploadButton.disabled = disabled;
        if (elements.voiceButton) elements.voiceButton.disabled = disabled;
    }

    function updateStatus(connected, message) {
        isConnected = connected;
        if (connected) {
            // Desktop status
            elements.statusIndicator && (elements.statusIndicator.className = 'status-indicator status-indicator--connected');
            elements.statusPulse && (elements.statusPulse.className = 'status-pulse status-pulse--connected');
            if (elements.statusText) {
                elements.statusText.textContent = message || 'Connected';
                elements.statusText.className = 'status-text status-text--connected';
            }
            // Mobile status pill - hide when connected
            if (elements.mobileStatusPill) {
                elements.mobileStatusPill.classList.add('hidden');
            }
            setDisabledState(false);
        } else {
            // Desktop status
            elements.statusIndicator && (elements.statusIndicator.className = 'status-indicator status-indicator--error');
            elements.statusPulse && (elements.statusPulse.className = 'status-pulse status-pulse--error');
            if (elements.statusText) {
                elements.statusText.textContent = message || 'Disconnected';
                elements.statusText.className = 'status-text status-text--error';
            }
            // Mobile status pill - show when disconnected
            if (elements.mobileStatusPill) {
                elements.mobileStatusPill.classList.remove('hidden');
                elements.mobileStatusIndicator && (elements.mobileStatusIndicator.className = 'status-indicator status-indicator--error');
                elements.mobileStatusPulse && (elements.mobileStatusPulse.className = 'status-pulse status-pulse--error');
                if (elements.mobileStatusText) {
                    elements.mobileStatusText.textContent = message || 'Disconnected';
                }
            }
            setDisabledState(true);
        }
    }

    function showKlawedStatus(statusMessage) {
        // Connection status should only show connection state, not "AI is thinking"
        // The typing indicator (3-dot bubble) handles showing AI processing state

        // Ignore messages about AI thinking - those are handled by the typing indicator
        if (statusMessage && /(processing|working|Klawed is|AI is thinking)/i.test(statusMessage)) {
            return; // Don't update status for processing messages
        }

        elements.statusIndicator && (elements.statusIndicator.className = 'status-indicator status-indicator--working');
        elements.statusPulse && (elements.statusPulse.className = 'status-pulse status-pulse--working');
        if (elements.statusText) {
            elements.statusText.textContent = statusMessage;
            elements.statusText.className = 'status-text status-text--working';
        }
        
        // Mobile status pill - show during connecting/working state
        if (elements.mobileStatusPill) {
            elements.mobileStatusPill.classList.remove('hidden');
            elements.mobileStatusIndicator && (elements.mobileStatusIndicator.className = 'status-indicator status-indicator--working');
            elements.mobileStatusPulse && (elements.mobileStatusPulse.className = 'status-pulse status-pulse--working');
            if (elements.mobileStatusText) {
                elements.mobileStatusText.textContent = statusMessage;
            }
        }

        if (statusMessage && (statusMessage.includes('completed processing') || statusMessage.includes('timeout reached'))) {
            addSystemMessage('• ' + statusMessage, 'info');
        }
    }


    function hideEmptyState() {
        if (elements.chatEmpty) {
            elements.chatEmpty.remove();
        }
        const demoSeed = rootEl.querySelector('[data-chat-demo-seed]');
        if (demoSeed) {
            demoSeed.remove();
        }
    }

    /**
     * Set up click handlers for file links (file:// protocol) within a message.
     * When clicked, these links will:
     * 1. Switch to the Files tab (on mobile)
     * 2. Navigate to the file's directory in the file explorer
     * 3. Highlight the file briefly
     * @param {HTMLElement} messageEl - The message element to search for file links
     */
    function setupFileLinkHandlers(messageEl) {
        if (!messageEl) return;
        
        const fileLinks = messageEl.querySelectorAll('a.file-link[data-file-path]');
        
        fileLinks.forEach(link => {
            link.addEventListener('click', async (e) => {
                e.preventDefault();
                e.stopPropagation();
                
                const filePath = link.dataset.filePath;
                if (!filePath) {
                    console.warn('[file-chat] File link missing data-file-path');
                    return;
                }
                
                console.log('[file-chat] File link clicked:', filePath);
                
                // Ensure file explorer is available
                ensureFileExplorer();
                
                if (!fileExplorer) {
                    console.error('[file-chat] FileExplorer not available');
                    addSystemMessage('✗ File explorer not available', 'error');
                    return;
                }
                
                // Set session if needed
                if (sessionId && typeof fileExplorer.setSession === 'function') {
                    fileExplorer.setSession(sessionId, null);
                }
                
                // Switch to Files tab on mobile
                const isDesktop = window.innerWidth >= 1024;
                if (!isDesktop && tabManager) {
                    // Use the public switchTab method
                    const fileTab = rootEl.querySelector('[data-tab="file"]');
                    if (fileTab) {
                        fileTab.click();
                    }
                }
                
                // Navigate to the file and highlight it
                try {
                    const found = await fileExplorer.navigateToFile(filePath);
                    if (found) {
                        console.log('[file-chat] Successfully navigated to file:', filePath);
                    } else {
                        console.warn('[file-chat] File not found:', filePath);
                    }
                } catch (error) {
                    console.error('[file-chat] Error navigating to file:', error);
                    addSystemMessage(`✗ Could not find file: ${filePath}`, 'error');
                }
            });
        });
    }

    function scrollToBottom(force = false, smooth = true) {
        if (!scrollContainer || typeof scrollContainer.scrollTo !== 'function') return;

        // Only auto-scroll if:
        // 1. Force is true (manual scroll or first response), OR
        // 2. We haven't received first AI response yet (welcome flow), OR
        // 3. User hasn't scrolled up
        if (force || !hasReceivedFirstAiResponse || !isUserScrolledUp) {
            // Cancel any pending scroll to avoid stacking animations
            if (pendingScrollFrame) {
                cancelAnimationFrame(pendingScrollFrame);
                pendingScrollFrame = null;
            }

            // Use requestAnimationFrame to ensure content is fully laid out
            // Double RAF ensures layout is complete (one for paint, one for scroll)
            pendingScrollFrame = requestAnimationFrame(() => {
                pendingScrollFrame = requestAnimationFrame(() => {
                    scrollContainer.scrollTo({ 
                        top: scrollContainer.scrollHeight, 
                        behavior: smooth ? 'smooth' : 'instant' 
                    });
                    pendingScrollFrame = null;
                });
            });
        }
    }

    function isScrolledToBottom() {
        if (!scrollContainer) return true;
        const threshold = 100; // pixels from bottom
        const scrollTop = scrollContainer.scrollTop;
        const scrollHeight = scrollContainer.scrollHeight;
        const clientHeight = scrollContainer.clientHeight;
        return (scrollHeight - scrollTop - clientHeight) < threshold;
    }

    function updateScrollState() {
        if (!scrollContainer) return;
        
        // Cancel pending update to debounce rapid scroll events
        if (scrollUpdateTimeout) {
            clearTimeout(scrollUpdateTimeout);
        }
        
        // Debounce: only update after scrolling pauses for 50ms
        // This prevents excessive recalculations during smooth scrolling
        scrollUpdateTimeout = setTimeout(() => {
            isUserScrolledUp = !isScrolledToBottom();
            updateScrollToBottomButton();
            scrollUpdateTimeout = null;
        }, 50);
    }

    function createScrollToBottomButton() {
        if (scrollToBottomButton) return scrollToBottomButton;

        const button = document.createElement('button');
        // Position relative to chat-container with bottom-6 to stay above the input bar
        // Use sticky positioning so it stays at the bottom of the visible area
        button.className = 'sticky bottom-6 ml-auto mr-6 z-30 w-12 h-12 rounded-full bg-gradient-to-br from-orange-500 to-orange-600 text-white shadow-lg hover:shadow-xl transition-all duration-200 flex items-center justify-center opacity-0 pointer-events-none hover:scale-110 active:scale-95';
        button.setAttribute('aria-label', 'Scroll to bottom');
        button.innerHTML = `
            <svg class="w-5 h-5" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2.5" d="M19 14l-7 7m0 0l-7-7m7 7V3"/>
            </svg>
        `;

        button.addEventListener('click', () => {
            if (scrollContainer) {
                // User explicitly clicked - force scroll with smooth animation
                isUserScrolledUp = false;
                updateScrollToBottomButton();
                scrollToBottom(true, true); // force=true, smooth=true
            }
        });

        scrollToBottomButton = button;
        // Append to chat container so it floats relative to the scrollable area
        const chatContainer = document.querySelector('[data-chat-container]');
        if (chatContainer) {
            chatContainer.appendChild(button);
        } else {
            document.body.appendChild(button); // Fallback
        }
        return button;
    }

    function updateScrollToBottomButton() {
        const button = createScrollToBottomButton();

        if (isUserScrolledUp) {
            // Show button with animation
            button.style.opacity = '1';
            button.style.pointerEvents = 'auto';
        } else {
            // Hide button with animation
            button.style.opacity = '0';
            button.style.pointerEvents = 'none';
        }
    }

    // Set up scroll listener
    if (scrollContainer) {
        scrollContainer.addEventListener('scroll', updateScrollState);
    }

    function addMessage(content, isUser = false, messageId = null) {
        try {
            if (content === undefined || content === null) {
                content = '';
            }

            hideEmptyState();

            const messageDiv = document.createElement('div');
            if (messageId) {
                messageDiv.dataset.messageId = messageId;
            }
            messageDiv.className = 'flex ' + (isUser ? 'justify-end' : 'justify-start') + ' animate-fade-in';

            const bubble = document.createElement('div');
            bubble.className = 'inline-block max-w-[78%] sm:max-w-2xl px-4 sm:px-5 py-3.5 rounded-2xl border shadow-sm transition-colors ' + (isUser ?
                'bg-orange-50 dark:bg-orange-900/60 border-orange-200 dark:border-orange-700 text-orange-950 dark:text-orange-100 shadow-orange-200/40 dark:shadow-orange-900/20' :
                'bg-slate-100 dark:bg-slate-800/80 border-slate-200 dark:border-slate-700 text-slate-900 dark:text-slate-200 shadow-slate-200/40 dark:shadow-slate-900/40');

            const textDiv = document.createElement('div');
            // Base classes - whitespace-pre-wrap is only for plain text, not markdown
            const baseClasses = 'break-words font-sans text-body-m leading-relaxed text-current';
            
            // For user messages, display as plain text
            // For AI messages, parse and render markdown
            if (isUser) {
                textDiv.className = baseClasses + ' whitespace-pre-wrap';
                textDiv.textContent = String(content);
            } else {
                // Check if content contains markdown formatting
                const markdownContent = String(content);
                if (containsMarkdown(markdownContent)) {
                    // Apply markdown rendering - no whitespace-pre-wrap since HTML handles formatting
                    textDiv.className = baseClasses;
                    textDiv.innerHTML = parseMarkdown(markdownContent);
                } else {
                    // Display as plain text if no markdown detected - use whitespace-pre-wrap
                    textDiv.className = baseClasses + ' whitespace-pre-wrap';
                    textDiv.textContent = markdownContent;
                }
            }

            const timestamp = document.createElement('div');
            timestamp.className = 'text-caption-s mt-2 ' + (isUser ?
                'text-orange-700/80 dark:text-orange-300/70' :
                'text-slate-500 dark:text-slate-400');
            timestamp.textContent = new Date().toLocaleTimeString('en-US', { hour: '2-digit', minute: '2-digit' });

            bubble.appendChild(textDiv);
            bubble.appendChild(timestamp);
            messageDiv.appendChild(bubble);

            if (elements.chatEmpty && elements.chatEmpty.parentElement) {
                hideEmptyState();
            }

            messageParent.appendChild(messageDiv);

            // Add click handler for file links (file:// protocol)
            if (!isUser) {
                setupFileLinkHandlers(messageDiv);
            }

            // Handle auto-scroll for first AI response
            if (!isUser && !hasReceivedFirstAiResponse) {
                hasReceivedFirstAiResponse = true;
                scrollToBottom(true); // Force scroll for first AI response
            } else {
                scrollToBottom(); // Regular scroll behavior
            }

            return messageDiv;
        } catch (error) {
            console.error('Error in addMessage:', error);
            return null;
        }
    }

    function updateMessage(messageId, content) {
        if (!messageId) return;
        const selector = `[data-message-id="${CSS.escape ? CSS.escape(messageId) : messageId}"]`;
        const messageDiv = messageParent.querySelector(selector);
        if (messageDiv) {
            const textElement = messageDiv.querySelector('div.whitespace-pre-wrap, pre');
            if (textElement) {
                textElement.textContent = content ?? '';
            }
            // Use instant scroll during streaming to avoid animation conflicts
            scrollToBottom(false, false); // force=false, smooth=false
        }
    }

    /**
     * Finalize a streamed message by re-rendering it with markdown
     * This should be called after streaming completes to properly format the content
     */
    function finalizeMessage(messageId, content) {
        if (!messageId) {
            console.warn('[finalizeMessage] No messageId provided');
            return;
        }
        const selector = `[data-message-id="${CSS.escape ? CSS.escape(messageId) : messageId}"]`;
        const messageDiv = messageParent.querySelector(selector);
        if (!messageDiv) {
            console.warn('[finalizeMessage] Message div not found for:', messageId);
            return;
        }

        // Find the text container (first div inside the bubble)
        const bubble = messageDiv.querySelector('.rounded-2xl, .rounded-lg');
        if (!bubble) {
            console.warn('[finalizeMessage] Bubble not found in message:', messageId);
            return;
        }

        const textElement = bubble.querySelector('div.whitespace-pre-wrap, div.break-words');
        if (!textElement) {
            console.warn('[finalizeMessage] Text element not found in bubble:', messageId);
            return;
        }

        const markdownContent = String(content ?? '');
        const baseClasses = 'break-words font-sans text-body-m leading-relaxed text-current';

        console.log('[finalizeMessage] Processing message:', messageId, 'containsMarkdown:', containsMarkdown(markdownContent));

        // Re-render with markdown if content contains markdown formatting
        if (containsMarkdown(markdownContent)) {
            textElement.className = baseClasses;
            const parsedHtml = parseMarkdown(markdownContent);
            console.log('[finalizeMessage] Parsed markdown, HTML length:', parsedHtml?.length);
            textElement.innerHTML = parsedHtml;
        } else {
            // Keep as plain text with whitespace-pre-wrap
            textElement.className = baseClasses + ' whitespace-pre-wrap';
            textElement.textContent = markdownContent;
        }

        // Set up file link handlers for any file:// links in the rendered markdown
        setupFileLinkHandlers(messageDiv);

        // Final scroll after rendering complete
        scrollToBottom();
    }

    function handleStreamChunk(content) {
        if (!currentStreamMessageId) {
            currentStreamMessageId = 'stream-' + Date.now();
            currentStreamContent = content;
            addMessage(content, false, currentStreamMessageId);
        } else {
            currentStreamContent += content;
            updateMessage(currentStreamMessageId, currentStreamContent);
        }
    }

    function handleStreamComplete(content) {
        console.log('[handleStreamComplete] Called with currentStreamMessageId:', currentStreamMessageId, 'content length:', content?.length);
        if (currentStreamMessageId) {
            // First update with final content as plain text
            updateMessage(currentStreamMessageId, content ?? '');
            // Then re-render with markdown formatting
            console.log('[handleStreamComplete] Calling finalizeMessage for:', currentStreamMessageId);
            finalizeMessage(currentStreamMessageId, content ?? '');
            currentStreamMessageId = null;
            currentStreamContent = '';

            // Mark first AI response as received
            if (!hasReceivedFirstAiResponse) {
                hasReceivedFirstAiResponse = true;
                scrollToBottom(true); // Force scroll for first complete response
            }
        } else {
            addMessage(content ?? '', false);
        }
    }

    function addSystemMessage(content, type = 'info') {
        const messageDiv = document.createElement('div');
        messageDiv.className = 'text-center animate-fade-in';

        const span = document.createElement('span');
        const colors = {
            info: 'bg-cyan-50 dark:bg-cyan-950/40 text-cyan-700 dark:text-cyan-300 border-cyan-200 dark:border-cyan-800',
            success: 'bg-emerald-50 dark:bg-emerald-950/40 text-emerald-700 dark:text-emerald-300 border-emerald-200 dark:border-emerald-800',
            error: 'bg-coral-50 dark:bg-coral-950/40 text-coral-700 dark:text-coral-300 border-coral-200 dark:border-coral-800',
            warning: 'bg-amber-50 dark:bg-amber-950/40 text-amber-700 dark:text-amber-300 border-amber-200 dark:border-amber-800'
        };
        span.className = 'inline-flex items-center gap-2 px-4 py-2 rounded-full text-caption-m-bold border shadow-sm ' + (colors[type] || colors.info);
        span.textContent = content;

        messageDiv.appendChild(span);
        messageParent.appendChild(messageDiv);
        scrollToBottom();
    }

    function showAiWorkingIndicator() {
        const indicator = document.getElementById('ai-working-indicator');
        if (indicator) {
            indicator.classList.remove('hidden');
        }
    }

    function hideAiWorkingIndicator() {
        const indicator = document.getElementById('ai-working-indicator');
        if (indicator) {
            indicator.classList.add('hidden');
        }
    }

    // ----------------------
    // Tool Activity Display
    // ----------------------

    /**
     * Get a friendly display name and icon for a tool
     */
    function getToolDisplayInfo(toolName) {
        const toolMap = {
            'Read': { icon: 'fileText', label: 'Reading file', verb: 'Read' },
            'Write': { icon: 'penLine', label: 'Writing file', verb: 'Wrote' },
            'Edit': { icon: 'wrench', label: 'Editing file', verb: 'Edited' },
            'MultiEdit': { icon: 'wrench', label: 'Editing file', verb: 'Edited' },
            'Grep': { icon: 'search', label: 'Searching', verb: 'Searched' },
            'Glob': { icon: 'folder', label: 'Finding files', verb: 'Found files' },
            'Bash': { icon: 'terminal', label: 'Running command', verb: 'Ran command' },
            'Subagent': { icon: 'bot', label: 'Running subagent', verb: 'Subagent completed' },
            'TodoWrite': { icon: 'checkSquare', label: 'Updating tasks', verb: 'Updated tasks' },
            'MemoryStore': { icon: 'database', label: 'Storing memory', verb: 'Stored memory' },
            'MemoryRecall': { icon: 'brain', label: 'Recalling memory', verb: 'Recalled memory' },
            'MemorySearch': { icon: 'search', label: 'Searching memory', verb: 'Searched memory' },
            'UploadImage': { icon: 'image', label: 'Processing image', verb: 'Processed image' },
            'Sleep': { icon: 'clock', label: 'Waiting', verb: 'Waited' },
            'CheckSubagentProgress': { icon: 'RefreshCw', label: 'Checking progress', verb: 'Checked progress' },
            'InterruptSubagent': { icon: 'slash', label: 'Stopping subagent', verb: 'Stopped subagent' }
        };
        return toolMap[toolName] || { icon: 'wrench', label: toolName, verb: toolName };
    }

    /**
     * Extract a summary from tool input for display
     */
    function getToolInputSummary(toolName, toolInput) {
        if (!toolInput) return null;

        try {
            const input = typeof toolInput === 'string' ? JSON.parse(toolInput) : toolInput;

            switch (toolName) {
                case 'Read':
                    if (input.file_path) {
                        const fileName = input.file_path.split('/').pop();
                        const lines = input.start_line && input.end_line
                            ? ` (lines ${input.start_line}-${input.end_line})`
                            : '';
                        return fileName + lines;
                    }
                    break;
                case 'Write':
                case 'Edit':
                case 'MultiEdit':
                    if (input.file_path) {
                        return input.file_path.split('/').pop();
                    }
                    break;
                case 'Grep':
                    if (input.pattern) {
                        const path = input.path ? ` in ${input.path}` : '';
                        return `"${input.pattern}"${path}`;
                    }
                    break;
                case 'Glob':
                    if (input.pattern) {
                        return input.pattern;
                    }
                    break;
                case 'Bash':
                    if (input.command) {
                        // Truncate long commands
                        const cmd = input.command.length > 50
                            ? input.command.substring(0, 47) + '...'
                            : input.command;
                        return cmd;
                    }
                    break;
                case 'Subagent':
                    if (input.prompt) {
                        const prompt = input.prompt.length > 60
                            ? input.prompt.substring(0, 57) + '...'
                            : input.prompt;
                        return prompt;
                    }
                    break;
            }
        } catch (e) {
            // Ignore parse errors
        }
        return null;
    }

    /**
     * Extract a summary from tool result for display
     */
    function getToolResultSummary(toolName, result, isError) {
        if (isError) {
            return 'Error';
        }

        if (!result) return 'Done';

        try {
            const output = typeof result === 'string' ? result : JSON.stringify(result);

            // For Grep, try to extract match count
            if (toolName === 'Grep' && output.includes('match_count')) {
                const match = output.match(/"match_count"\s*:\s*(\d+)/);
                if (match) {
                    return `${match[1]} matches`;
                }
            }

            // For Glob, try to extract file count
            if (toolName === 'Glob' && output.includes('"count"')) {
                const match = output.match(/"count"\s*:\s*(\d+)/);
                if (match) {
                    return `${match[1]} files`;
                }
            }

            // For Read, show line count
            if (toolName === 'Read' && output.includes('total_lines')) {
                const match = output.match(/"total_lines"\s*:\s*(\d+)/);
                if (match) {
                    return `${match[1]} lines`;
                }
            }

            return 'Done';
        } catch (e) {
            return 'Done';
        }
    }

    /**
     * Create or get the tool activity container
     */
    function ensureToolActivityContainer() {
        if (toolActivityEl) return toolActivityEl;

        const wrapper = document.createElement('div');
        wrapper.className = 'flex justify-start animate-fade-in';
        wrapper.setAttribute('data-tool-activity', '');

        const container = document.createElement('div');
        container.className = 'inline-block max-w-[85%] sm:max-w-2xl rounded-2xl border shadow-sm transition-all duration-300 ' +
            'bg-slate-50 dark:bg-slate-900/50 border-slate-200 dark:border-slate-700 overflow-hidden';

        // Header (always visible, clickable to expand/collapse)
        const header = document.createElement('div');
        header.className = 'flex items-center gap-2 px-4 py-2.5 cursor-pointer hover:bg-slate-100 dark:hover:bg-slate-800/50 transition-colors';
        header.innerHTML = `
            <svg class="w-4 h-4 text-slate-500 dark:text-slate-400" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M14.7 6.3a1 1 0 0 0 0 1.4l1.6 1.6a1 1 0 0 0 1.4 0l3.77-3.77a6 6 0 0 1-7.94 7.94l-6.91 6.91a2.12 2.12 0 0 1-3-3l6.91-6.91a6 6 0 0 1 7.94-7.94l-3.76 3.76z"/>
            </svg>
            <span class="text-sm font-medium text-slate-700 dark:text-slate-300" data-tool-summary>Working...</span>
            <span class="ml-auto text-xs text-slate-400 dark:text-slate-500" data-tool-count></span>
            <svg class="w-4 h-4 text-slate-400 dark:text-slate-500 transition-transform" data-tool-chevron fill="none" stroke="currentColor" viewBox="0 0 24 24">
                <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M19 9l-7 7-7-7"/>
            </svg>
        `;
        header.setAttribute('data-tool-header', '');

        // Details (collapsible)
        const details = document.createElement('div');
        details.className = 'border-t border-slate-200 dark:border-slate-700 max-h-0 overflow-hidden transition-all duration-300';
        details.setAttribute('data-tool-details', '');

        const detailsList = document.createElement('div');
        detailsList.className = 'px-4 py-2 space-y-1.5';
        detailsList.setAttribute('data-tool-list', '');

        details.appendChild(detailsList);
        container.appendChild(header);
        container.appendChild(details);
        wrapper.appendChild(container);

        // Toggle expand/collapse on header click
        let isExpanded = false;
        header.addEventListener('click', () => {
            isExpanded = !isExpanded;
            const chevron = header.querySelector('[data-tool-chevron]');
            if (isExpanded) {
                details.style.maxHeight = details.scrollHeight + 'px';
                chevron.style.transform = 'rotate(180deg)';
            } else {
                details.style.maxHeight = '0px';
                chevron.style.transform = 'rotate(0deg)';
            }
        });

        toolActivityEl = wrapper;
        return toolActivityEl;
    }

    /**
     * Add or update tool activity in the UI
     */
    function addToolActivity(toolName, toolId, toolInput) {
        hideEmptyState();
        // Don't remove typing indicator here - AI is still processing
        // The typing indicator will be removed by END_AI_TURN message

        const container = ensureToolActivityContainer();
        const toolList = container.querySelector('[data-tool-list]');
        const toolSummary = container.querySelector('[data-tool-summary]');
        const toolCount = container.querySelector('[data-tool-count]');
        const toolDetails = container.querySelector('[data-tool-details]');

        // Get display info
        const displayInfo = getToolDisplayInfo(toolName);
        const inputSummary = getToolInputSummary(toolName, toolInput);

        // Create tool item element
        const toolItem = document.createElement('div');
        toolItem.className = 'flex items-center gap-2 text-sm animate-fade-in';
        toolItem.setAttribute('data-tool-id', toolId);

        // Spinner for in-progress
        const spinner = `<svg class="w-3.5 h-3.5 animate-spin text-orange-500" fill="none" viewBox="0 0 24 24">
            <circle class="opacity-25" cx="12" cy="12" r="10" stroke="currentColor" stroke-width="4"></circle>
            <path class="opacity-75" fill="currentColor" d="M4 12a8 8 0 018-8V0C5.373 0 0 5.373 0 12h4zm2 5.291A7.962 7.962 0 014 12H0c0 3.042 1.135 5.824 3 7.938l3-2.647z"></path>
        </svg>`;

        toolItem.innerHTML = `
            <span class="flex-shrink-0" data-tool-status>${spinner}</span>
            <i data-lucide="${displayInfo.icon}" class="w-4 h-4 text-slate-600 dark:text-slate-400"></i>
            <span class="text-slate-700 dark:text-slate-300">${displayInfo.label}</span>
            ${inputSummary ? `<span class="text-slate-500 dark:text-slate-400 truncate max-w-[200px]" title="${inputSummary}">${inputSummary}</span>` : ''}
            <span class="ml-auto text-xs text-slate-400 dark:text-slate-500" data-tool-result></span>
        `;

        toolList.appendChild(toolItem);

        // Initialize Lucide icons for the new tool item
        if (typeof window.lucide !== 'undefined' && window.lucide.createIcons) {
            window.lucide.createIcons({ 
                root: toolItem,
                icons: window.lucide.icons 
            });
        }

        // Track pending tool
        pendingTools.set(toolId, {
            toolName,
            startTime: Date.now(),
            element: toolItem
        });

        // Update summary
        const pendingCount = pendingTools.size;
        const completedCount = completedTools.length;
        toolSummary.textContent = pendingCount > 0
            ? `${displayInfo.label}...`
            : 'Completed';
        toolCount.textContent = completedCount > 0
            ? `${completedCount} done`
            : '';

        // Ensure container is in DOM
        if (!container.parentElement) {
            messageParent.appendChild(container);
        }

        // Auto-expand details if this is the first tool
        if (pendingTools.size === 1 && completedTools.length === 0) {
            toolDetails.style.maxHeight = toolDetails.scrollHeight + 100 + 'px';
            container.querySelector('[data-tool-chevron]').style.transform = 'rotate(180deg)';
        }

        scrollToBottom();
    }

    /**
     * Mark a tool as complete
     */
    function completeToolActivity(toolName, toolId, isError, result) {
        const pending = pendingTools.get(toolId);
        if (!pending) {
            debug('completeToolActivity: Tool not found in pending:', toolId);
            return;
        }

        const duration = Date.now() - pending.startTime;
        const displayInfo = getToolDisplayInfo(toolName);
        const resultSummary = getToolResultSummary(toolName, result, isError);

        // Update the element
        const element = pending.element;
        if (element) {
            const statusEl = element.querySelector('[data-tool-status]');
            const resultEl = element.querySelector('[data-tool-result]');

            if (isError) {
                statusEl.innerHTML = `<i data-lucide="XCircle" class="w-3.5 h-3.5 text-red-500"></i>`;
                element.classList.add('text-red-600', 'dark:text-red-400');
            } else {
                statusEl.innerHTML = `<i data-lucide="CheckCircle" class="w-3.5 h-3.5 text-emerald-500"></i>`;
            }

            // Re-initialize Lucide icons for the updated status
            if (typeof window.lucide !== 'undefined' && window.lucide.createIcons && statusEl) {
                window.lucide.createIcons({ 
                    root: statusEl,
                    icons: window.lucide.icons 
                });
            }

            resultEl.textContent = resultSummary + (duration > 1000 ? ` (${(duration/1000).toFixed(1)}s)` : '');
        }

        // Move from pending to completed
        pendingTools.delete(toolId);
        completedTools.push({ toolName, toolId, isError, duration });

        // Update summary
        const container = ensureToolActivityContainer();
        const toolSummary = container.querySelector('[data-tool-summary]');
        const toolCount = container.querySelector('[data-tool-count]');

        if (pendingTools.size > 0) {
            // Still have pending tools, show the current one
            const [, nextPending] = pendingTools.entries().next().value;
            const nextInfo = getToolDisplayInfo(nextPending.toolName);
            toolSummary.textContent = `${nextInfo.label}...`;
        } else {
            // All done
            toolSummary.textContent = `Completed ${completedTools.length} action${completedTools.length > 1 ? 's' : ''}`;
        }
        toolCount.textContent = completedTools.length > 0
            ? `${completedTools.length} done`
            : '';

        scrollToBottom();
    }

    /**
     * Clear tool activity (call when AI response is complete)
     */
    function clearToolActivity() {
        pendingTools.clear();
        completedTools = [];
        if (toolActivityEl && toolActivityEl.parentElement) {
            // Keep it visible but mark as complete
            const toolSummary = toolActivityEl.querySelector('[data-tool-summary]');
            if (toolSummary && pendingTools.size === 0) {
                // Already shows completed state
            }
        }
        // Reset for next interaction
        toolActivityEl = null;
    }

    function shouldSeedPlaceholderMessages() {
        if (!PLACEHOLDER_MESSAGES_ENABLED) return false;
        if (hasSeededPlaceholders) return false;

        const urlHasDemo = typeof window !== 'undefined' && window.location.search.includes('chatDemo=1');
        const dataFlag = rootEl.dataset.chatDemo === 'true';
        return urlHasDemo || dataFlag;
    }

    function seedPlaceholderMessages() {
        if (!shouldSeedPlaceholderMessages()) return;
        hasSeededPlaceholders = true;

        hideEmptyState();

        const demoScript = [
            { from: 'assistant', text: 'Hi! I can help with file management, document processing, and organization. What do you need?' },
            { from: 'user', text: "Create a net-30 invoice for Acme Labs for $8,750 with a late fee." },
            { type: 'system', text: 'API call • generating draft with validation and tax rules…' },
            { from: 'assistant', text: 'Draft ready. Summary:\n• Client: Acme Labs\n• Amount: $8,750\n• Terms: Net 30 + 2% late fee\n• Attachments: prepared PDF, CSV ledger' },
            { from: 'user', text: 'Schedule it for tomorrow 9am and email finance@acme.test.' },
            { from: 'assistant', text: 'Scheduled for tomorrow 09:00 with CC to finance@acme.test and a follow-up reminder set for +3 days.' }
        ];

        let delay = 250;
        demoScript.forEach(step => {
            setTimeout(() => {
                if (step.type === 'system') {
                    addSystemMessage(step.text, 'info');
                    return;
                }
                addMessage(step.text, step.from === 'user');
            }, delay);
            delay += 400;
        });
    }

    // ----------------------
    // Guided Tour Utilities
    // ----------------------
    function shouldForceShowTourFromUrl() {
        if (typeof window === 'undefined') return false;
        const params = new URLSearchParams(window.location.search);
        return params.has(TOUR_URL_FLAG);
    }

    function hasCompletedTourFlag() {
        if (typeof window === 'undefined') return false;
        try {
            return window.localStorage.getItem(TOUR_STORAGE_KEY) === 'true';
        } catch (error) {
            console.warn('[invoice-chat] Unable to read tour flag', error);
            return false;
        }
    }

    function setCompletedTourFlag() {
        if (typeof window === 'undefined') return;
        try {
            window.localStorage.setItem(TOUR_STORAGE_KEY, 'true');
        } catch (error) {
            console.warn('[invoice-chat] Unable to write tour flag', error);
        }
    }

    function shouldShowTour() {
        if (shouldForceShowTourFromUrl()) return true;
        return !hasCompletedTourFlag();
    }

    function getPrimaryActions() {
        return {
            chatInput: elements.messageInput,
            sendButton: elements.sendButtons ? elements.sendButtons[0] : null, // Use first send button for tour highlighting
            filesTab: rootEl.querySelector('#file-tab'),
            chatTab: rootEl.querySelector('#chat-tab'),
            uploadButton: elements.chatUploadButton,
            fileExplorerUploadButton: rootEl.querySelector('#file-explorer-upload'), // Specific ID for file explorer upload
            statusText: elements.statusText,
            emptyState: rootEl.querySelector('[data-chat-empty]')
        };
    }

    function switchToTab(tabId) {
        const tabButton = rootEl.querySelector(`[data-tab="${tabId}"]`);
        if (tabButton) {
            tabButton.click();
        }
    }

    function highlightElement(el) {
        if (!el) return () => {};
        const prevZ = el.style.zIndex;
        const prevPos = el.style.position;
        if (!prevPos || prevPos === 'static') {
            el.style.position = 'relative';
        }
        el.style.zIndex = '40';
        // Enhanced highlight box - more prominent with primary color and animation
        el.classList.add('ring-4', 'ring-orange-500', 'ring-offset-4', 'shadow-[0_0_20px_rgba(249,115,22,0.5)]', 'animate-bounce-subtle');
        return () => {
            el.style.zIndex = prevZ;
            el.style.position = prevPos;
            el.classList.remove('ring-4', 'ring-orange-500', 'ring-offset-4', 'shadow-[0_0_20px_rgba(249,115,22,0.5)]', 'animate-bounce-subtle');
        };
    }

    function addOverlay() {
        const overlay = document.createElement('div');
        // Transparent overlay for click handling only - no dimming
        overlay.className = 'fixed inset-0 z-20';
        overlay.setAttribute('data-tour-overlay', '');
        document.body.appendChild(overlay);
        return overlay;
    }

    function removeOverlay() {
        const overlay = document.querySelector('[data-tour-overlay]');
        if (overlay) overlay.remove();
    }

    function createTooltip(text) {
        const tooltip = document.createElement('div');
        tooltip.className = 'fixed z-40 max-w-md px-5 py-4 rounded-2xl bg-white shadow-[0_0_30px_rgba(249,115,22,0.3)] border-2 border-orange-500 text-slate-800 text-sm leading-relaxed animate-pulse-subtle';
        tooltip.innerHTML = `
            <div class="space-y-1">${text}</div>
            <div class="text-right text-xs text-orange-600 font-medium mt-3 pt-2 border-t border-slate-100">Click to continue →</div>
        `;
        tooltip.setAttribute('data-tour-tooltip', '');
        document.body.appendChild(tooltip);
        return tooltip;
    }

    function positionTooltip(tooltip, target, placement = 'bottom') {
        if (!tooltip || !target) return;
        const rect = target.getBoundingClientRect();
        const margin = 12;
        const viewportWidth = window.innerWidth;
        const viewportHeight = window.innerHeight;
        let top = rect.bottom + margin;
        let left = rect.left;
        if (placement === 'top') {
            top = rect.top - tooltip.offsetHeight - margin;
        }
        if (placement === 'right') {
            top = rect.top;
            left = rect.right + margin;
        }
        if (placement === 'left') {
            top = rect.top;
            left = rect.left - tooltip.offsetWidth - margin;
        }
        // keep inside viewport
        top = Math.min(Math.max(top, margin), viewportHeight - tooltip.offsetHeight - margin);
        left = Math.min(Math.max(left, margin), viewportWidth - tooltip.offsetWidth - margin);
        tooltip.style.top = `${top}px`;
        tooltip.style.left = `${left}px`;
    }

    function createWelcomeModal(onContinue, onSkip) {
        // Create backdrop
        const backdrop = document.createElement('div');
        backdrop.className = 'fixed inset-0 z-50 bg-black/60 backdrop-blur-sm flex items-center justify-center p-4';

        // Create modal with scrollable content
        const modal = document.createElement('div');
        modal.className = 'bg-white rounded-2xl shadow-2xl max-w-lg w-full max-h-[90vh] flex flex-col animate-fade-in';
        modal.innerHTML = `
            <div class="overflow-y-auto flex-1 p-4 sm:p-8">
                <div class="text-center mb-4 sm:mb-6">
                    <div class="w-16 h-16 sm:w-20 sm:h-20 bg-gradient-to-br from-orange-500 to-orange-600 rounded-2xl flex items-center justify-center mx-auto mb-4 sm:mb-5 shadow-lg shadow-orange-200/70">
                        <svg class="w-8 h-8 sm:w-10 sm:h-10 text-white" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                            <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M9.75 17L9 20l-1 1h8l-1-1-.75-3M3 13h18M5 17h14a2 2 0 002-2V5a2 2 0 00-2-2H5a2 2 0 00-2 2v10a2 2 0 002 2z" />
                        </svg>
                    </div>
                    <h2 class="text-xl sm:text-2xl font-bold text-slate-900 mb-2 sm:mb-3">Welcome to FileSurf</h2>
                    <p class="text-slate-600 text-base sm:text-lg mb-3 sm:mb-4">Your personal AI-powered computer in the cloud</p>
                </div>

                <div class="space-y-3 sm:space-y-4 text-left mb-6 sm:mb-8">
                    <p class="text-slate-700 text-sm sm:text-base leading-relaxed">
                        FileSurf is far more than a file editor. Think of it as your personal workspace where an AI assistant can help you with almost anything:
                    </p>

                    <ul class="space-y-2 sm:space-y-3 text-slate-600 text-sm sm:text-base">
                        <li class="flex items-start gap-2 sm:gap-3">
                            <span class="text-orange-500 mt-1">📁</span>
                            <span><strong>File Management & Analysis</strong> — Upload, organize, and analyze any files</span>
                        </li>
                        <li class="flex items-start gap-2 sm:gap-3">
                            <span class="text-orange-500 mt-1">💻</span>
                            <span><strong>Write & Run Code</strong> — Create scripts, apps, and automate tasks</span>
                        </li>
                        <li class="flex items-start gap-2 sm:gap-3">
                            <span class="text-orange-500 mt-1">🎙️</span>
                            <span><strong>Voice Input</strong> — Speak naturally instead of typing with voice dictation</span>
                        </li>
                        <li class="flex items-start gap-2 sm:gap-3">
                            <span class="text-orange-500 mt-1">🎬</span>
                            <span><strong>YouTube Transcripts</strong> — Fetch and analyze video transcripts instantly</span>
                        </li>
                        <li class="flex items-start gap-2 sm:gap-3">
                            <span class="text-orange-500 mt-1">🎨</span>
                            <span><strong>Create UI & Prototypes</strong> — Generate interfaces and prototypes <em class="text-slate-400">(coming soon)</em></span>
                        </li>
                        <li class="flex items-start gap-2 sm:gap-3">
                            <span class="text-orange-500 mt-1">🧠</span>
                            <span><strong>Remember Preferences</strong> — Tell me to remember things and I will</span>
                        </li>
                    </ul>

                </div>
            </div>

            <div class="flex flex-col sm:flex-row gap-3 p-4 sm:p-6 border-t border-slate-100 bg-slate-50 rounded-b-2xl">
                <button id="tour-continue-btn" class="flex-1 px-4 sm:px-6 py-3 bg-gradient-to-r from-orange-500 to-orange-600 text-white font-semibold rounded-xl shadow-lg shadow-orange-200/50 hover:shadow-orange-300/60 transition-all duration-200 hover:scale-[1.02] text-sm sm:text-base">
                    Take a Quick Tour
                </button>
                <button id="tour-skip-btn" class="flex-1 px-4 sm:px-6 py-3 bg-slate-100 text-slate-700 font-medium rounded-xl hover:bg-slate-200 transition-colors duration-200 text-sm sm:text-base">
                    Skip for Now
                </button>
            </div>
        `;

        backdrop.appendChild(modal);
        document.body.appendChild(backdrop);

        // Add event listeners
        modal.querySelector('#tour-continue-btn').addEventListener('click', () => {
            backdrop.remove();
            onContinue();
        });

        modal.querySelector('#tour-skip-btn').addEventListener('click', () => {
            backdrop.remove();
            onSkip();
        });

        return backdrop;
    }

    function runTour() {
        if (!shouldShowTour()) return;
        if (hasCompletedTour) return;
        hasCompletedTour = true;

        // Show welcome modal first
        createWelcomeModal(
            // On continue - run the tour
            () => {
                runTourSteps();
            },
            // On skip - just mark as completed
            () => {
                setCompletedTourFlag();
            }
        );
    }

    function runTourSteps() {
        const actions = getPrimaryActions();
        const overlay = addOverlay();
        const steps = [];

        // Step 1: Chat input - the main way to interact
        steps.push(() => {
            const cleanup = highlightElement(actions.chatInput);
            const tip = createTooltip(`
                <strong>💬 Chat with your AI assistant</strong><br/>
                <p class="mt-1">This is your main way to interact. Ask questions, give tasks, or have a conversation. The AI can help you with almost anything — from analyzing files to writing code to fetching YouTube transcripts.</p>
                <p class="mt-2 text-slate-500 text-xs">Tip: Press Enter to send, Shift+Enter for a new line</p>
            `);
            positionTooltip(tip, actions.chatInput, 'top');
            tip.addEventListener('click', () => overlay.dispatchEvent(new Event('click')));
            return () => { cleanup(); tip.remove(); };
        });

        // Step 2: Voice input button
        steps.push(() => {
            const cleanup = highlightElement(elements.voiceButton);
            const tip = createTooltip(`
                <strong>🎙️ Voice input mode</strong><br/>
                <p class="mt-1">Click the microphone to dictate your messages instead of typing. Perfect for hands-free operation or when you want to speak naturally.</p>
                <p class="mt-2 text-slate-500 text-xs">Note: Requires browser speech recognition support (Chrome, Edge, Safari)</p>
            `);
            positionTooltip(tip, elements.voiceButton, 'top');
            tip.addEventListener('click', () => overlay.dispatchEvent(new Event('click')));
            return () => { cleanup(); tip.remove(); };
        });

        // Step 3: Upload button
        steps.push(() => {
            const cleanup = highlightElement(actions.uploadButton);
            const tip = createTooltip(`
                <strong>📎 Upload files instantly</strong><br/>
                <p class="mt-1">Click here to upload documents, images, code, or any files you want to work with. The AI can read, analyze, edit, and create files for you.</p>
            `);
            positionTooltip(tip, actions.uploadButton, 'top');
            tip.addEventListener('click', () => overlay.dispatchEvent(new Event('click')));
            return () => { cleanup(); tip.remove(); };
        });

        // Step 4: Files tab / File Explorer
        steps.push(() => {
            const isDesktop = window.innerWidth >= 1024;

            if (isDesktop) {
                // On larger screens, highlight the file panel directly since it's always visible
                const filePanel = rootEl.querySelector('#file-panel');
                const cleanup = highlightElement(filePanel);
                const tip = createTooltip(`
                    <strong>📂 Your file workspace</strong><br/>
                    <p class="mt-1">Your file explorer is always visible here. Browse, download, preview, and manage all your uploaded files and anything the AI creates for you.</p>
                `);
                positionTooltip(tip, filePanel, 'left');
                tip.addEventListener('click', () => overlay.dispatchEvent(new Event('click')));
                return () => { cleanup(); tip.remove(); };
            } else {
                // On smaller screens, show the Files tab
                switchToTab('file');
                const cleanup = highlightElement(actions.filesTab);
                const tip = createTooltip(`
                    <strong>📂 Your file workspace</strong><br/>
                    <p class="mt-1">The Files tab shows all your uploaded files and anything the AI creates for you. You can browse, download, preview, and manage your files here.</p>
                `);
                positionTooltip(tip, actions.filesTab, 'bottom');
                tip.addEventListener('click', () => overlay.dispatchEvent(new Event('click')));
                return () => { cleanup(); tip.remove(); switchToTab('chat'); };
            }
        });

        // Step 5: Status indicator
        steps.push(() => {
            const cleanup = highlightElement(actions.statusText);
            const tip = createTooltip(`
                <strong>🟢 Connection status</strong><br/>
                <p class="mt-1">This shows your connection status and lets you know when the AI is thinking or working on a task.</p>
            `);
            positionTooltip(tip, actions.statusText, 'bottom');
            tip.addEventListener('click', () => overlay.dispatchEvent(new Event('click')));
            return () => { cleanup(); tip.remove(); };
        });

        // Final step: You're ready!
        steps.push(() => {
            const wrapper = document.createElement('div');
            wrapper.className = 'fixed inset-0 z-40 flex items-center justify-center pointer-events-none';

            const tip = document.createElement('div');
            tip.className = 'max-w-md px-6 py-5 rounded-2xl bg-white shadow-[0_0_40px_rgba(249,115,22,0.3)] border-2 border-orange-500 text-slate-800 pointer-events-auto cursor-pointer';
            tip.innerHTML = `
                <div class="text-center">
                    <div class="text-3xl mb-3">🎉</div>
                    <h3 class="text-xl font-bold text-slate-900 mb-2">You're all set!</h3>
                    <p class="text-slate-600 mb-3">Start by typing a message or uploading a file. Here are some things you can try:</p>
                    <div class="text-left space-y-2 text-sm text-slate-500 bg-slate-50 rounded-lg p-3">
                        <p>• "Summarize this document for me"</p>
                        <p>• "Get the transcript from [YouTube URL]"</p>
                        <p>• "Write a Python script that..."</p>
                        <p>• "Remember that I prefer dark mode"</p>
                    </div>
                    <p class="mt-4 text-xs text-orange-600 font-medium">Click anywhere to start</p>
                </div>
            `;

            tip.addEventListener('click', () => {
                overlay.dispatchEvent(new Event('click'));
            }, { once: true });

            wrapper.appendChild(tip);
            document.body.appendChild(wrapper);

            return () => { wrapper.remove(); };
        });

        let currentStep = 0;
        let currentCleanup = null;

        function nextStep() {
            if (currentCleanup) {
                currentCleanup();
            }
            if (currentStep >= steps.length) {
                removeOverlay();
                setCompletedTourFlag();
                return;
            }
            currentCleanup = steps[currentStep++]();
        }

        overlay.addEventListener('click', nextStep);
        window.addEventListener('resize', () => {
            if (currentCleanup) currentCleanup();
            currentStep = Math.max(0, currentStep - 1);
            nextStep();
        });
        nextStep();
    }

    function ensureFileExplorer() {
        console.log('[file-chat] ensureFileExplorer called, fileExplorer exists:', !!fileExplorer);
        if (fileExplorer) return;
        if (typeof window !== 'undefined' && window.FileExplorer) {
            console.log('[file-chat] Creating new FileExplorer instance');
            fileExplorer = new window.FileExplorer();
        } else {
            console.error('[file-chat] window.FileExplorer not available!');
        }
    }

    function setFileExplorerSession() {
        console.log('[file-chat] setFileExplorerSession called, sessionId:', sessionId);
        if (!sessionId) return;
        ensureFileExplorer();
        console.log('[file-chat] fileExplorer instance:', fileExplorer ? 'exists' : 'null');
        // Rely on server-managed session; no need to read cookie in JS.
        const userId = null;
        if (fileExplorer && typeof fileExplorer.setSession === 'function') {
            fileExplorer.setSession(sessionId, userId);
            console.log('[file-chat] fileExplorer session set with sessionId:', sessionId);

            // Check if Files tab is visible
            // On desktop (>= 1024px), both panels are always visible
            // On mobile, check if files tab is the active one
            const isDesktop = window.innerWidth >= 1024;
            let shouldLoadExplorer = false;

            if (isDesktop) {
                // On desktop, file explorer is always visible
                shouldLoadExplorer = true;
                console.log('[file-chat] Desktop mode: loading file explorer (always visible)');
            } else if (tabManager) {
                // On mobile, check active tab
                const activeTab = tabManager.getActiveTab();
                console.log('[file-chat] Mobile mode, current active tab:', activeTab ? activeTab.id : 'none');
                if (activeTab && activeTab.id === 'file') {
                    shouldLoadExplorer = true;
                    console.log('[file-chat] Files tab is active, loading directory...');
                } else {
                    console.log('[file-chat] Files tab is NOT active, skipping directory load');
                }
            }

            if (shouldLoadExplorer) {
                console.log('[file-chat] Loading file explorer directory');
                fileExplorer.loadDirectory();
                // Start auto-refresh for file explorer
                if (typeof fileExplorer.startAutoRefresh === 'function') {
                    fileExplorer.startAutoRefresh();
                }
            }
        }
    }



    function scheduleReconnect() {
        // Clear any existing reconnect timeout
        if (reconnectTimeoutId) {
            clearTimeout(reconnectTimeoutId);
        }

        reconnectAttempts++;

        // Calculate delay with exponential backoff: delay = baseDelay * 2^attempts
        // Add jitter (±25%) to prevent thundering herd
        const exponentialDelay = Math.min(
            BASE_RECONNECT_DELAY * Math.pow(2, reconnectAttempts - 1),
            MAX_RECONNECT_DELAY
        );
        const jitter = exponentialDelay * 0.25 * (Math.random() * 2 - 1); // ±25%
        const delay = Math.round(exponentialDelay + jitter);

        console.log(`[file-chat] Scheduling reconnect attempt ${reconnectAttempts}/${MAX_RECONNECT_ATTEMPTS} in ${delay}ms`);
        updateStatus(false, `Reconnecting in ${Math.round(delay / 1000)}s... (${reconnectAttempts}/${MAX_RECONNECT_ATTEMPTS})`);

        reconnectTimeoutId = setTimeout(() => {
            connectWebSocket();
        }, delay);
    }

    async function connectWebSocket() {
        console.log('[file-chat] connectWebSocket called, attempt:', reconnectAttempts + 1);

        // Don't reconnect if manually disconnected or after auth failure
        if (isManuallyDisconnected) {
            console.log('[file-chat] Manual disconnect - not reconnecting');
            return;
        }

        if (isAuthFailure) {
            console.log('[file-chat] Auth failure detected - not reconnecting');
            return;
        }

        // Check if we've exceeded max reconnect attempts
        if (reconnectAttempts >= MAX_RECONNECT_ATTEMPTS) {
            console.error('[file-chat] Max reconnect attempts reached');
            updateStatus(false, 'Connection failed. Click to retry.');
            // Add a reconnect button to the status
            if (elements.statusIndicator) {
                elements.statusIndicator.style.cursor = 'pointer';
                elements.statusIndicator.onclick = () => {
                    console.log('[file-chat] Manual reconnect triggered');
                    reconnectAttempts = 0; // Reset counter
                    elements.statusIndicator.onclick = null;
                    elements.statusIndicator.style.cursor = 'default';
                    connectWebSocket();
                };
            }
            return;
        }

        try {
            // Check sessionStorage for existing session ID
            const storedSessionId = sessionStorage.getItem('filesurf_sessionId');

            if (storedSessionId) {
                // Reuse existing session
                sessionId = storedSessionId;
                console.log('[file-chat] Reusing session from sessionStorage:', sessionId);
            } else {
                // Generate new session
                console.log('[file-chat] Fetching session from /session/generate');
                const response = await fetch('/session/generate');
                if (!response.ok) {
                    // Check if it's an authentication error
                    if (isAuthRequired(response)) {
                        console.log('[file-chat] Authentication required - stopping reconnection');
                        isAuthFailure = true;
                        // Clear any pending reconnection attempts
                        if (reconnectTimeoutId) {
                            clearTimeout(reconnectTimeoutId);
                            reconnectTimeoutId = null;
                        }
                        await handleAuthError(response);
                        return;
                    }
                    throw new Error('Failed to generate session');
                }
                const sessionData = await response.json();
                sessionId = sessionData.sessionId;
                console.log('[file-chat] Session generated:', sessionId);

                // Store in sessionStorage for reuse on reconnect
                sessionStorage.setItem('filesurf_sessionId', sessionId);
            }

            // Server sets HttpOnly cookie; no client write needed.

            console.log('[file-chat] Calling setFileExplorerSession');
            setFileExplorerSession();

            const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
            const wsUrl = protocol + '//' + window.location.host + '/app/ws/' + encodeURIComponent(sessionId);

            debug('Connecting to WebSocket', wsUrl);
            ws = new WebSocket(wsUrl);
        } catch (error) {
            console.error('Failed to generate session:', error);
            scheduleReconnect();
            return;
        }

        ws.onopen = () => {
            console.info('WebSocket connected');
            updateStatus(true, 'Connected');
            // Reset reconnection counter on successful connection
            reconnectAttempts = 0;
            if (reconnectTimeoutId) {
                clearTimeout(reconnectTimeoutId);
                reconnectTimeoutId = null;
            }
        };

        ws.onmessage = (event) => {
            debug('Message from server', event.data);
            try {
                const message = JSON.parse(event.data);
                const messageType = message.messageType;
                const content = message.content;

                switch (messageType) {
                    case 'STATUS':
                        if (content && content.startsWith('SESSION_ID:')) {
                            updateStatus(true, 'Connected');
                        } else if (content && (/(Klawed is|working|processing|Tool|thinking)/i.test(content))) {
                            // Show typing indicator (3-dot bubble) for AI processing
                            showAiWorkingIndicator();
                            // Don't update connection status - it stays "Connected"
                        } else {
                            updateStatus(true, 'Connected');
                            // Don't remove typing indicator here - wait for END_AI_TURN
                        }
                        break;
                    case 'TEXT':
                        if (content) {
                            // Ensure content is a string - if it's an object, stringify it
                            const textContent = typeof content === 'string' ? content :
                                (typeof content === 'object' ? JSON.stringify(content, null, 2) : String(content));
                            handleStreamComplete(textContent);
                        }
                        // Don't remove typing indicator here - wait for END_AI_TURN
                        clearToolActivity(); // Clear tool activity tracking for next interaction
                        // Reset API call time when we get a text response
                        lastApiCallTime = null;
                        // Keep status as "Connected" - typing indicator will be removed by END_AI_TURN
                        break;
                    case 'ERROR':
                        // Check if this is an invalid session error
                        if (content && (content.includes('Invalid session') || content.includes('No session ID'))) {
                            console.warn('[file-chat] Invalid session detected, clearing sessionStorage');
                            sessionStorage.removeItem('filesurf_sessionId');
                            // Don't show error to user, just reconnect with new session
                            ws.close();
                            return;
                        }
                        addSystemMessage('✗ ' + (content || 'Error occurred'), 'error');
                        // Don't remove typing indicator here - wait for END_AI_TURN
                        clearToolActivity(); // Clear tool activity tracking
                        // Reset API call time when we get an error
                        lastApiCallTime = null;
                        // Keep status as "Connected" - typing indicator will be removed by END_AI_TURN
                        break;
                    case 'API_CALL':
                        // Show typing indicator (3-dot bubble) when AI is processing
                        showAiWorkingIndicator();
                        // Connection status stays "Connected" - no need to change it

                        // Add a system message for API calls (optional info)
                        if (content && typeof content === 'object') {
                            // Only add system message if we haven't recently added one
                            // to avoid spamming the chat with multiple "AI is processing" messages
                            const now = Date.now();
                            if (!lastApiCallTime || (now - lastApiCallTime) > 5000) { // 5 second cooldown
                                let systemMessage = '🤖 AI is thinking';
                                if (content.model) {
                                    systemMessage += ' using ' + content.model;
                                }
                                addSystemMessage(systemMessage, 'info');
                                lastApiCallTime = now;
                            }
                        } else if (typeof content === 'string') {
                            // Only add system message if we haven't recently added one
                            const now = Date.now();
                            if (!lastApiCallTime || (now - lastApiCallTime) > 5000) {
                                addSystemMessage('🤖 ' + content, 'info');
                                lastApiCallTime = now;
                            }
                        } else {
                            // Only add system message if we haven't recently added one
                            const now = Date.now();
                            if (!lastApiCallTime || (now - lastApiCallTime) > 5000) {
                                addSystemMessage('🤖 AI is thinking', 'info');
                                lastApiCallTime = now;
                            }
                        }
                        break;
                    case 'TOOL':
                    case 'TOOL_RESULT':
                        // Display tool activity in a collapsible card
                        debug('Processing ' + messageType + ' message:', content);

                        if (messageType === 'TOOL') {
                            // Extract tool info
                            let toolName = null;
                            let toolId = null;
                            let toolInput = null;

                            if (content && typeof content === 'object') {
                                toolName = content.toolName;
                                toolId = content.toolId;
                                // Backend sends 'toolParameters', also check 'toolInput' and 'input'
                                toolInput = content.toolParameters || content.toolInput || content.input;
                            } else if (typeof content === 'string') {
                                try {
                                    const toolData = JSON.parse(content);
                                    toolName = toolData.toolName;
                                    toolId = toolData.toolId;
                                    toolInput = toolData.toolParameters || toolData.toolInput || toolData.input;
                                } catch (e) {
                                    // Not JSON
                                }
                            }

                            if (toolName && toolId) {
                                addToolActivity(toolName, toolId, toolInput);
                                // Don't update connection status - it stays "Connected"
                                // The typing indicator and tool activity card show the AI is working
                            } else {
                                // Fallback to old behavior
                                showAiWorkingIndicator();
                                // Don't update connection status
                            }
                        } else {
                            // TOOL_RESULT
                            let toolName = null;
                            let toolId = null;
                            let isError = false;
                            let result = null;

                            if (content && typeof content === 'object') {
                                toolName = content.toolName;
                                toolId = content.toolId;
                                isError = content.isError || false;
                                // Backend sends 'toolOutput', also check 'result' and 'output'
                                result = content.toolOutput || content.result || content.output;
                            } else if (typeof content === 'string') {
                                try {
                                    const toolData = JSON.parse(content);
                                    toolName = toolData.toolName;
                                    toolId = toolData.toolId;
                                    isError = toolData.isError || false;
                                    result = toolData.toolOutput || toolData.result || toolData.output;
                                } catch (e) {
                                    // Not JSON
                                }
                            }

                            if (toolName && toolId) {
                                completeToolActivity(toolName, toolId, isError, result);
                            }
                            // Don't update connection status - typing indicator will be removed by END_AI_TURN
                        }
                        break;
                    case 'END_AI_TURN':
                        // AI turn completed - remove typing indicator
                        debug('Received END_AI_TURN - AI turn completed');
                        hideAiWorkingIndicator();
                        // Reset API call time
                        lastApiCallTime = null;
                        // Connection status stays "Connected"
                        break;
                    default:
                        if (typeof content === 'string') {
                            handleStreamComplete(content);
                            // Don't remove typing indicator here - wait for END_AI_TURN
                        }
                }
            } catch (error) {
                debug('Failed to parse message as JSON', error);
                if (event.data.startsWith('SESSION_ID:')) {
                    const parts = event.data.split('|');
                    sessionId = parts[0].replace('SESSION_ID:', '');
                    updateStatus(true, 'Connected');
                    setFileExplorerSession();
                } else if (event.data.startsWith('CHUNK:')) {
                    const chunkContent = event.data.substring(6);
                    handleStreamChunk(chunkContent);
                } else if (event.data.startsWith('DONE:')) {
                    const doneContent = event.data.substring(5);
                    try {
                        handleStreamComplete(doneContent);
                    } catch (streamError) {
                        console.error('Error in handleStreamComplete:', streamError);
                        addMessage(doneContent, false);
                    }
                    // Don't remove typing indicator here - wait for END_AI_TURN
                } else if (event.data.startsWith('ERROR:')) {
                    const errorMessage = event.data.substring(6);
                    addSystemMessage('✗ ' + errorMessage, 'error');
                    // Don't remove typing indicator here - wait for END_AI_TURN
                } else {
                    // Check if it's a tool-related message in legacy format
                    if (event.data.includes('[TOOL') || event.data.includes('[TOOL RESULT') ||
                        event.data.startsWith('[TOOL') || event.data.startsWith('[TOOL RESULT')) {
                        debug('Filtered out tool-related legacy message:', event.data);
                        // Don't update connection status - typing indicator shows AI is working
                    } else {
                        addMessage(event.data, false);
                        // Don't remove typing indicator here - wait for END_AI_TURN
                    }
                }
            }
        };

        ws.onerror = (error) => {
            console.error('WebSocket error:', error);
            hideAiWorkingIndicator();
            updateStatus(false, 'Connection error');
        };

        ws.onclose = (event) => {
            console.info('WebSocket closed', event.code, event.reason);
            hideAiWorkingIndicator();
            isConnected = false;

            // Don't reconnect if this was a manual disconnect
            if (isManuallyDisconnected) {
                updateStatus(false, 'Disconnected');
                return;
            }

            // Use exponential backoff for automatic reconnection
            updateStatus(false, 'Disconnected');
            scheduleReconnect();
        };
    }

    function sendMessage(message) {
        if (!isConnected || !ws) {
            console.error('sendMessage: WebSocket not connected');
            return;
        }
        if (!message || !message.trim()) {
            return;
        }

        // Reset first AI response flag for new conversation turn
        hasReceivedFirstAiResponse = false;

        addMessage(message, true);
        showAiWorkingIndicator();
        ws.send(message);
        if (elements.messageInput) {
            elements.messageInput.value = '';
            // Reset textarea height after sending
            elements.messageInput.style.height = 'auto';
        }
        currentStreamMessageId = null;
        currentStreamContent = '';
    }

    // --- Voice input (Web Speech API) ---
    let recognition = null;
    let isListening = false;
    let stopListenTimeout = null;

    function supportsSpeech() {
        return typeof window !== 'undefined' && ('SpeechRecognition' in window || 'webkitSpeechRecognition' in window);
    }

    function requiresHTTPS() {
        // Web Speech API requires HTTPS except for localhost
        const isLocalhost = window.location.hostname === 'localhost' ||
                           window.location.hostname === '127.0.0.1' ||
                           window.location.hostname === '[::1]';
        const isHTTPS = window.location.protocol === 'https:';
        return !isLocalhost && !isHTTPS;
    }

    function getRecognition() {
        if (recognition) return recognition;
        const SpeechRecognition = window.SpeechRecognition || window.webkitSpeechRecognition;
        if (!SpeechRecognition) return null;
        recognition = new SpeechRecognition();
        recognition.lang = 'en-US';
        recognition.interimResults = true;
        recognition.continuous = false;
        recognition.maxAlternatives = 1;
        recognition.onstart = () => {
            isListening = true;
            updateVoiceUI();
            addSystemMessage('🎙️ Listening... speak your message', 'info');
        };
        recognition.onerror = (event) => {
            console.error('Speech recognition error:', event.error);
            let errorMsg = '✗ Voice error: ' + event.error;

            // Provide helpful context for common errors
            if (event.error === 'network') {
                if (requiresHTTPS()) {
                    errorMsg = '✗ Voice input requires HTTPS. Please access via https:// or localhost';
                } else if (!navigator.onLine) {
                    errorMsg = '✗ No internet connection. Check your network and try again.';
                } else {
                    errorMsg = '✗ Cannot reach speech service. Check your internet connection and try again.';
                }
            } else if (event.error === 'not-allowed') {
                errorMsg = '✗ Microphone access denied. Check browser permissions.';
            } else if (event.error === 'no-speech') {
                errorMsg = '✗ No speech detected. Click the microphone to try again.';
            } else if (event.error === 'aborted') {
                // Don't show error for user-initiated aborts
                stopListening();
                return;
            } else if (event.error === 'audio-capture') {
                errorMsg = '✗ Microphone not available. Check your audio settings.';
            } else if (event.error === 'service-not-allowed') {
                errorMsg = '✗ Speech service unavailable. Try again later.';
            }

            addSystemMessage(errorMsg, 'error');
            stopListening();
        };
        recognition.onend = () => {
            if (isListening) {
                // If it ended unexpectedly, stop
                stopListening();
            }
        };
        recognition.onresult = (event) => {
            if (!elements.messageInput) return;
            let transcript = '';
            for (let i = event.resultIndex; i < event.results.length; i++) {
                transcript += event.results[i][0].transcript;
                if (event.results[i].isFinal) {
                    // Commit final result
                    elements.messageInput.value = transcript.trim();
                    elements.messageInput.dispatchEvent(new Event('input'));
                    stopListening(true);
                    // auto-send if we already have content
                    if (elements.messageInput.value.trim()) {
                        elements.messageForm.dispatchEvent(new Event('submit'));
                    }
                } else {
                    // Show interim text in the input
                    elements.messageInput.value = transcript;
                    elements.messageInput.dispatchEvent(new Event('input'));
                }
            }
        };
        return recognition;
    }

    function updateVoiceUI() {
        if (!elements.voiceButton) return;
        elements.voiceButton.dataset.state = isListening ? 'listening' : 'idle';
        elements.voiceButton.classList.toggle('ring-2', isListening);
        elements.voiceButton.classList.toggle('ring-orange-500', isListening);
        elements.voiceButton.classList.toggle('animate-pulse', isListening);
        elements.voiceButton.title = isListening ? 'Click to stop listening' : 'Dictate with voice';
        elements.voiceButton.setAttribute('aria-pressed', isListening ? 'true' : 'false');
    }

    function stopListening(silent = false) {
        if (!recognition) return;
        try { recognition.stop(); } catch (e) { /* ignore */ }
        isListening = false;
        updateVoiceUI();
        if (!silent) {
            addSystemMessage('🎙️ Stopped listening', 'info');
        }
        if (stopListenTimeout) {
            clearTimeout(stopListenTimeout);
            stopListenTimeout = null;
        }
    }

    function startListening() {
        // Check HTTPS requirement
        if (requiresHTTPS()) {
            addSystemMessage('✗ Voice input requires HTTPS or localhost access', 'error');
            return;
        }

        // Check network connectivity
        if (!navigator.onLine) {
            addSystemMessage('✗ No internet connection. Voice input requires network access.', 'error');
            return;
        }

        const rec = getRecognition();
        if (!rec) {
            addSystemMessage('✗ Voice input not supported in this browser', 'error');
            return;
        }
        try {
            rec.start();
            // safety stop after 30s
            stopListenTimeout = setTimeout(() => stopListening(), 30000);
        } catch (err) {
            console.error('Failed to start recognition', err);
            addSystemMessage('✗ Could not start voice input: ' + err.message, 'error');
        }
    }

    if (elements.voiceButton) {
        // Voice input disabled - requires network/cloud service
        elements.voiceButton.style.display = 'none';
        
        // Legacy code kept for reference (disabled)
        /*
        // Only hide on non-HTTPS (except localhost)
        if (requiresHTTPS()) {
            elements.voiceButton.style.display = 'none';
        } else if (!supportsSpeech()) {
            elements.voiceButton.disabled = true;
            elements.voiceButton.title = 'Voice input not supported in this browser';
        } else {
            elements.voiceButton.addEventListener('click', () => {
                if (isListening) {
                    stopListening();
                } else {
                    startListening();
                }
            });
        }
        */
    }

    if (elements.messageForm && elements.messageInput) {
        elements.messageForm.addEventListener('submit', (e) => {
            e.preventDefault();
            if (isListening) stopListening(true);
            sendMessage(elements.messageInput.value);
        });

        elements.messageInput.addEventListener('keydown', (e) => {
            // Only submit on Enter for desktop (lg breakpoint: 1024px and above)
            // On mobile, Enter creates a new line
            const isDesktop = window.innerWidth >= 1024;
            
            if (e.key === 'Enter' && !e.shiftKey && isDesktop) {
                e.preventDefault();
                elements.messageForm.dispatchEvent(new Event('submit'));
            }
            // On mobile or with Shift+Enter, textarea handles new line automatically
        });

        // Auto-resize textarea as user types
        elements.messageInput.addEventListener('input', () => {
            elements.messageInput.style.height = 'auto';
            elements.messageInput.style.height = Math.min(elements.messageInput.scrollHeight, 120) + 'px';
        });
    }

    rootEl.querySelectorAll('[data-quick-prompt]').forEach((button) => {
        button.addEventListener('click', () => {
            if (isConnected && elements.messageForm && elements.messageInput) {
                elements.messageInput.value = (button.textContent || '').trim();
                elements.messageForm.dispatchEvent(new Event('submit'));
            }
        });
    });

    // Set up chat upload functionality (scoped to chat components only)
    if (elements.chatUploadButton && elements.chatFileInput) {
        // Wire chat upload button to chat file input
        elements.chatUploadButton.addEventListener('click', () => {
            elements.chatFileInput.click();
        });

        // Set up change handler for chat file input
        elements.chatFileInput.addEventListener('change', async (e) => {
            const files = Array.from(e.target.files || []);
            if (files.length === 0) return;

            if (!sessionId) {
                addSystemMessage('✗ No session available. Please reconnect.', 'error');
                return;
            }

            const fileNames = files.map(f => f.name).join(', ');
            addSystemMessage('📤 Uploading ' + files.length + ' file(s): ' + fileNames + '...', 'info');

            try {
                // Use StandardUploader for progress tracking
                const result = await StandardUploader.upload(files, sessionId, '/', {
                    useProgressUI: true
                });

                addSystemMessage('✓ Successfully uploaded ' + result.count + ' file(s)', 'success');

                if (isConnected && result.files && result.files.length > 0) {
                    const uploadMessage = "I've uploaded the following files: " + result.files.join(', ') + '. Please analyze them.';
                    sendMessage(uploadMessage);
                }
            } catch (error) {
                console.error('Upload error:', error);

                // Check if it's an auth error
                if (error.message && error.message.includes('401')) {
                    // Redirect to login
                    window.location.href = '/auth/login?redirect=' + encodeURIComponent(window.location.pathname);
                    return;
                }

                addSystemMessage('✗ Upload error: ' + error.message, 'error');
            }

            elements.chatFileInput.value = '';
        });
    }

    seedPlaceholderMessages();
    runTour();
    connectWebSocket();
}

const autoRoot = document.querySelector('[data-chat-root]');
if (autoRoot) {
    init(autoRoot);
}
