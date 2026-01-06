// Invoice Chat module
import { TabManager } from './tabManager.js';

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
        statusTime: rootEl.querySelector('[data-status-time]'),
        chatContainer: rootEl.querySelector('[data-chat-container]'),
        chatMessages: rootEl.querySelector('[data-chat-messages]'),
        chatEmpty: rootEl.querySelector('[data-chat-empty]'),
        messageForm: rootEl.querySelector('[data-message-form]'),
        messageInput: rootEl.querySelector('[data-message-input]'),
        sendButton: rootEl.querySelector('[data-send-button]'),
        uploadButton: rootEl.querySelector('[data-upload-button]'),
        fileInput: rootEl.querySelector('[data-file-input]')
    };

    const messageParent = elements.chatMessages || elements.chatContainer || rootEl;
    const scrollContainer = elements.chatContainer || elements.chatMessages || rootEl;

    // Initialize tab manager
    let tabManager = null;
    try {
        tabManager = new TabManager(rootEl);
        
        // Listen for tab switches to load file explorer when Files tab is activated
        rootEl.addEventListener('tab-switched', (event) => {
            const { tabId } = event.detail;
            if (tabId === 'file') {
                // Ensure file explorer is initialized
                ensureFileExplorer();
                // Set session if available
                if (sessionId && fileExplorer && typeof fileExplorer.setSession === 'function') {
                    fileExplorer.setSession(sessionId, null);
                }
                // Load the file explorer when Files tab is activated
                if (fileExplorer) {
                    fileExplorer.loadDirectory();
                    // Start auto-refresh for file explorer
                    if (typeof fileExplorer.startAutoRefresh === 'function') {
                        fileExplorer.startAutoRefresh();
                    }
                }

            } else if (fileExplorer && typeof fileExplorer.stopAutoRefresh === 'function') {
                // Stop auto-refresh when switching away from Files tab
                fileExplorer.stopAutoRefresh();
            }
        });
    } catch (error) {
        console.warn('[invoice-chat] Failed to initialize tab manager:', error);
    }

    // Check if Files or Template tab is already active on page load
    if (tabManager) {
        const activeTab = tabManager.getActiveTab();
        if (activeTab && activeTab.id === 'file') {
            // Files tab is active, initialize and load file explorer
            ensureFileExplorer();
            if (sessionId && fileExplorer && typeof fileExplorer.setSession === 'function') {
                fileExplorer.setSession(sessionId, null);
            }
            if (fileExplorer) {
                fileExplorer.loadDirectory();
                // Start auto-refresh for file explorer
                if (typeof fileExplorer.startAutoRefresh === 'function') {
                    fileExplorer.startAutoRefresh();
                }
            }

        }
    }

    let ws = null;
    let isConnected = false;
    let sessionId = null;
    let fileExplorer = null;
    let currentStreamMessageId = null;
    let currentStreamContent = '';
    let typingIndicatorEl = rootEl.querySelector('[data-typing-indicator]') || null;
    let hasSeededPlaceholders = false;
    let lastApiCallTime = null;

    // --- Guided Tour State ---
    let hasCompletedTour = false;


    function updateStatusTime() {
        if (!elements.statusTime) return;
        const now = new Date();
        elements.statusTime.textContent = now.toLocaleTimeString('en-US', { hour: '2-digit', minute: '2-digit' });
    }
    updateStatusTime();
    setInterval(updateStatusTime, 1000);

    function setDisabledState(disabled) {
        if (elements.messageInput) elements.messageInput.disabled = disabled;
        if (elements.sendButton) elements.sendButton.disabled = disabled;
        if (elements.uploadButton) elements.uploadButton.disabled = disabled;
    }

    function updateStatus(connected, message) {
        isConnected = connected;
        if (connected) {
            elements.statusIndicator && (elements.statusIndicator.className = 'status-indicator status-indicator--connected');
            elements.statusPulse && (elements.statusPulse.className = 'status-pulse status-pulse--connected');
            if (elements.statusText) {
                elements.statusText.textContent = message || 'Connected';
                elements.statusText.className = 'font-mono text-sm font-semibold tracking-tight status-text status-text--connected';
            }
            setDisabledState(false);
        } else {
            elements.statusIndicator && (elements.statusIndicator.className = 'status-indicator status-indicator--error');
            elements.statusPulse && (elements.statusPulse.className = 'status-pulse status-pulse--error');
            if (elements.statusText) {
                elements.statusText.textContent = message || 'Disconnected';
                elements.statusText.className = 'font-mono text-sm font-semibold tracking-tight status-text status-text--error';
            }
            setDisabledState(true);
        }
    }

    function showKlawedStatus(statusMessage) {
        elements.statusIndicator && (elements.statusIndicator.className = 'status-indicator status-indicator--working');
        elements.statusPulse && (elements.statusPulse.className = 'status-pulse status-pulse--working');
        if (elements.statusText) {
            // Create three-dot loader HTML
            const threeDotLoader = '<span class="three-dot-loader"><span class="dot"></span><span class="dot"></span><span class="dot"></span></span>';
            
            // If the message is about processing, show it with three dots
            if (statusMessage.includes('processing') || statusMessage.includes('working') || statusMessage.includes('Klawed is')) {
                elements.statusText.innerHTML = 'AI is thinking' + threeDotLoader;
            } else {
                elements.statusText.textContent = statusMessage;
            }
            elements.statusText.className = 'font-mono text-sm font-semibold tracking-tight status-text status-text--working';
        }

        if (statusMessage && (statusMessage.includes('completed processing') || statusMessage.includes('timeout reached'))) {
            addSystemMessage('• ' + statusMessage, 'info');
        }
    }



    function hideEmptyState() {
        if (elements.chatEmpty) {
            elements.chatEmpty.classList.add('hidden');
        }
        const demoSeed = rootEl.querySelector('[data-chat-demo-seed]');
        if (demoSeed) {
            demoSeed.remove();
        }
    }

    function scrollToBottom() {
        if (scrollContainer && typeof scrollContainer.scrollTo === 'function') {
            scrollContainer.scrollTo({ top: scrollContainer.scrollHeight, behavior: 'smooth' });
        }
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
            bubble.className = 'w-full max-w-[90%] sm:max-w-2xl px-4 sm:px-5 py-3 rounded-2xl border shadow-sm ' + (isUser ? 
                'bg-[color-mix(in_srgb,_hsl(var(--primary))_14%,_hsl(var(--card))_86%)] border-[color-mix(in_srgb,_hsl(var(--primary))_26%,_hsl(var(--border))_74%)] border-l-[color-mix(in_srgb,_hsl(var(--primary))_38%,_hsl(var(--border))_62%)] shadow-[0_6px_16px_color-mix(in_srgb,_hsl(var(--primary))_12%,_transparent)]' : 
                'bg-[color-mix(in_srgb,_hsl(var(--card))_98%,_hsl(var(--muted))_2%)] border-[hsl(var(--border))] border-l-[color-mix(in_srgb,_hsl(var(--primary))_18%,_hsl(var(--border))_82%)] shadow-[0_4px_12px_color-mix(in_srgb,_hsl(var(--primary))_6%,_transparent)]');

            const textDiv = document.createElement('div');
            textDiv.className = 'whitespace-pre-wrap break-words font-sans text-body-m leading-relaxed text-slate-800';
            textDiv.textContent = String(content);

            const timestamp = document.createElement('div');
            timestamp.className = 'text-caption-s mt-2 text-[hsl(var(--muted-foreground))] opacity-80 ' + (isUser ? 'opacity-70' : 'opacity-80');
            timestamp.textContent = new Date().toLocaleTimeString('en-US', { hour: '2-digit', minute: '2-digit' });

            bubble.appendChild(textDiv);
            bubble.appendChild(timestamp);
            messageDiv.appendChild(bubble);

            if (elements.chatEmpty && elements.chatEmpty.parentElement) {
                hideEmptyState();
            }

            messageParent.appendChild(messageDiv);
            scrollToBottom();
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
            scrollToBottom();
        }
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
        if (currentStreamMessageId) {
            updateMessage(currentStreamMessageId, content ?? '');
            currentStreamMessageId = null;
            currentStreamContent = '';
        } else {
            addMessage(content ?? '', false);
        }
    }

    function addSystemMessage(content, type = 'info') {
        const messageDiv = document.createElement('div');
        messageDiv.className = 'text-center animate-fade-in';

        const span = document.createElement('span');
        const colors = {
            info: 'bg-cyan-100 text-cyan-700 dark:bg-cyan-500/20 dark:text-cyan-300 dark:border-cyan-500/30',
            success: 'bg-emerald-100 text-emerald-700 dark:bg-emerald-500/20 dark:text-emerald-300 dark:border-emerald-500/30',
            error: 'bg-coral-100 text-coral-700 dark:bg-coral-500/20 dark:text-coral-300 dark:border-coral-500/30',
            warning: 'bg-amber-100 text-amber-700 dark:bg-amber-500/20 dark:text-amber-300 dark:border-amber-500/30'
        };
        span.className = 'inline-flex items-center gap-2 px-4 py-2 rounded-full text-caption-m-bold border border-transparent shadow-sm ' + (colors[type] || colors.info);
        span.textContent = content;

        messageDiv.appendChild(span);
        messageParent.appendChild(messageDiv);
        scrollToBottom();
    }

    function ensureTypingIndicator() {
        if (typingIndicatorEl) return typingIndicatorEl;

        const wrapper = document.createElement('div');
        wrapper.className = 'flex justify-start animate-fade-in';
        wrapper.setAttribute('data-typing-indicator', '');

        const bubble = document.createElement('div');
        bubble.className = 'w-full max-w-[90%] sm:max-w-2xl px-4 sm:px-5 py-3 rounded-2xl border shadow-sm bg-[color-mix(in_srgb,_hsl(var(--muted))_90%,_hsl(var(--primary))_10%)] border-[color-mix(in_srgb,_hsl(var(--primary))_16%,_hsl(var(--border))_84%)] border-l-[color-mix(in_srgb,_hsl(var(--primary))_24%,_hsl(var(--border))_76%)]';

        const dots = document.createElement('div');
        dots.className = 'flex gap-1.5';
        for (let i = 0; i < 3; i++) {
            const dot = document.createElement('div');
            dot.className = 'w-2 h-2 rounded-full bg-[color-mix(in_srgb,_hsl(var(--primary))_30%,_hsl(var(--muted))_70%)]';
            // Add animation delay for each dot
            dot.style.animation = `bounce 1.4s infinite ${i * 0.16}s`;
            dots.appendChild(dot);
        }

        bubble.appendChild(dots);
        wrapper.appendChild(bubble);
        typingIndicatorEl = wrapper;
        return typingIndicatorEl;
    }

    function addTypingIndicator() {
        removeTypingIndicator();
        const indicator = ensureTypingIndicator();
        messageParent.appendChild(indicator);
        scrollToBottom();
    }

    function removeTypingIndicator() {
        if (typingIndicatorEl && typingIndicatorEl.parentElement) {
            typingIndicatorEl.remove();
        }
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
            sendButton: elements.sendButton,
            filesTab: rootEl.querySelector('#file-tab'),
            chatTab: rootEl.querySelector('#chat-tab'),
            uploadButton: elements.uploadButton,
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
        // Enhanced highlight box - more prominent without background dimming
        el.classList.add('ring-4', 'ring-amber-500', 'ring-offset-4', 'shadow-2xl', 'shadow-amber-500/30');
        return () => {
            el.style.zIndex = prevZ;
            el.style.position = prevPos;
            el.classList.remove('ring-4', 'ring-amber-500', 'ring-offset-4', 'shadow-2xl', 'shadow-amber-500/30');
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
        tooltip.className = 'fixed z-40 max-w-sm px-4 py-3 rounded-2xl bg-white shadow-2xl border border-slate-200 text-slate-800 text-sm leading-relaxed space-y-2';
        tooltip.innerHTML = text + '<div class="text-right text-xs text-slate-500">Click to continue</div>';
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

    function runTour() {
        if (!shouldShowTour()) return;
        if (hasCompletedTour) return;
        hasCompletedTour = true;

        const actions = getPrimaryActions();
        const overlay = addOverlay();
        const steps = [];

        steps.push(() => {
            const cleanup = highlightElement(actions.chatInput);
            const tip = createTooltip('<strong>Start chatting</strong><br/>Type a question or task for the AI assistant here. Press Enter to send, Shift+Enter for a new line.');
            positionTooltip(tip, actions.chatInput, 'top');
            tip.addEventListener('click', () => overlay.dispatchEvent(new Event('click')));
            return () => { cleanup(); tip.remove(); };
        });

        steps.push(() => {
            const cleanup = highlightElement(actions.sendButton);
            const tip = createTooltip('<strong>Send your message</strong><br/>Click Send or press Enter to submit.');
            positionTooltip(tip, actions.sendButton, 'top');
            tip.addEventListener('click', () => overlay.dispatchEvent(new Event('click')));
            return () => { cleanup(); tip.remove(); };
        });

        steps.push(() => {
            // Show the Files tab first so users understand where uploads live
            switchToTab('file');
            const cleanup = highlightElement(actions.filesTab);
            const tip = createTooltip('<strong>Files tab</strong><br/>This is where you manage and upload files.');
            positionTooltip(tip, actions.filesTab, 'bottom');
            tip.addEventListener('click', () => overlay.dispatchEvent(new Event('click')));
            return () => { cleanup(); tip.remove(); };
        });

        steps.push(() => {
            // Ensure Files tab is visible so the upload button is in DOM/visible
            switchToTab('file');
            let cleanup = null;
            let tip = null;
            let cancelled = false;
            const run = () => {
                if (cancelled) return;
                cleanup = highlightElement(actions.uploadButton);
                tip = createTooltip('<strong>Upload files</strong><br/>Attach PDFs, docs, or data so the AI can analyze them.');
                positionTooltip(tip, actions.uploadButton, 'top');
                tip.addEventListener('click', () => overlay.dispatchEvent(new Event('click')));
            };
            // Use double rAF to allow layout/render after tab switch
            requestAnimationFrame(() => requestAnimationFrame(run));
            return () => { cancelled = true; if (cleanup) cleanup(); if (tip) tip.remove(); switchToTab('chat'); };
        });

        steps.push(() => {
            const cleanup = highlightElement(actions.statusText);
            const tip = createTooltip('<strong>Status</strong><br/>See when you are connected and when the AI is thinking.');
            positionTooltip(tip, actions.statusText, 'bottom');
            tip.addEventListener('click', () => overlay.dispatchEvent(new Event('click')));
            return () => { cleanup(); tip.remove(); };
        });

        // Finish message
        steps.push(() => {
            const tip = createTooltip('<strong>You are ready!</strong><br/>Start a conversation or upload a file to begin.');
            tip.addEventListener('click', () => {
                overlay.dispatchEvent(new Event('click'));
            }, { once: true });
            tip.style.position = 'fixed';
            tip.style.bottom = '32px';
            tip.style.right = '32px';
            return () => { tip.remove(); };
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
            // Re-run current step positioning
            currentStep = Math.max(0, currentStep - 1);
            nextStep();
        });
        nextStep();
    }

    function ensureFileExplorer() {
        if (fileExplorer) return;
        if (typeof window !== 'undefined' && window.FileExplorer) {
            fileExplorer = new window.FileExplorer();
        }
    }

    function setFileExplorerSession() {
        if (!sessionId) return;
        ensureFileExplorer();
        // Rely on server-managed session; no need to read cookie in JS.
        const userId = null;
        if (fileExplorer && typeof fileExplorer.setSession === 'function') {
            fileExplorer.setSession(sessionId, userId);
            
            // If Files tab is currently active, reload the directory
            if (tabManager) {
                const activeTab = tabManager.getActiveTab();
                if (activeTab && activeTab.id === 'file') {
                    fileExplorer.loadDirectory();
                }
            }
        }
    }



    async function connectWebSocket() {
        try {
            const response = await fetch('/session/generate');
            if (!response.ok) {
                throw new Error('Failed to generate session');
            }
            const sessionData = await response.json();
            sessionId = sessionData.sessionId;

            // Server sets HttpOnly cookie; no client write needed.

            setFileExplorerSession();

            const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
            const wsUrl = protocol + '//' + window.location.host + '/file-chat/ws/' + encodeURIComponent(sessionId);

            debug('Connecting to WebSocket', wsUrl);
            ws = new WebSocket(wsUrl);
        } catch (error) {
            console.error('Failed to generate session:', error);
            updateStatus(false, 'Failed to connect. Retrying in 3 seconds...');
            setTimeout(connectWebSocket, 3000);
            return;
        }

        ws.onopen = () => {
            console.info('WebSocket connected');
            updateStatus(true, 'Connected');
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
                        } else if (content && (content.includes('Klawed is') || content.includes('working') || content.includes('processing') || content.includes('Tool'))) {
                            // Show "AI is thinking" with three dots for processing messages
                            showKlawedStatus('AI is thinking');
                        } else {
                            updateStatus(true, 'Connected');
                        }
                        break;
                    case 'TEXT':
                        if (content) {
                            handleStreamComplete(content);
                        }
                        removeTypingIndicator();
                        // Reset API call time when we get a text response
                        lastApiCallTime = null;
                        // Update status to show we're connected (not thinking)
                        updateStatus(true, 'Connected');
                        break;
                    case 'ERROR':
                        addSystemMessage('✗ ' + (content || 'Error occurred'), 'error');
                        removeTypingIndicator();
                        // Reset API call time when we get an error
                        lastApiCallTime = null;
                        // Update status to show we're connected (not thinking)
                        updateStatus(true, 'Connected');
                        break;
                    case 'API_CALL':
                        // Show typing indicator when AI is processing
                        addTypingIndicator();
                        
                        if (content && typeof content === 'object') {
                            let apiCallMessage = 'AI is thinking';
                            if (content.model || content.provider || content.estimatedDurationMs) {
                                apiCallMessage = 'AI is thinking (';
                                let hasPrevious = false;
                                if (content.model) {
                                    apiCallMessage += 'model: ' + content.model;
                                    hasPrevious = true;
                                }
                                if (content.provider) {
                                    if (hasPrevious) apiCallMessage += ', ';
                                    apiCallMessage += 'provider: ' + content.provider;
                                    hasPrevious = true;
                                }
                                if (content.estimatedDurationMs) {
                                    if (hasPrevious) apiCallMessage += ', ';
                                    apiCallMessage += 'estimated: ' + content.estimatedDurationMs + 'ms';
                                }
                                apiCallMessage += ')';
                            }
                            showKlawedStatus(apiCallMessage);
                            
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
                            showKlawedStatus(content);
                            // Only add system message if we haven't recently added one
                            const now = Date.now();
                            if (!lastApiCallTime || (now - lastApiCallTime) > 5000) {
                                addSystemMessage('🤖 ' + content, 'info');
                                lastApiCallTime = now;
                            }
                        } else {
                            showKlawedStatus('AI is thinking');
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
                        // Filter out tool-related messages - don't show them to the user
                        debug('Filtered out ' + messageType + ' message:', content);
                        // Show a status update for tool usage
                        if (messageType === 'TOOL') {
                            let toolMessage = 'AI is thinking';
                            if (content && typeof content === 'object' && content.toolName) {
                                toolMessage = 'AI is thinking (using ' + content.toolName + ' tool)';
                            } else if (typeof content === 'string' && content.includes('toolName')) {
                                // Try to parse tool name from string content
                                try {
                                    const toolData = JSON.parse(content);
                                    if (toolData.toolName) {
                                        toolMessage = 'AI is thinking (using ' + toolData.toolName + ' tool)';
                                    }
                                } catch (e) {
                                    // Not JSON, use default message
                                }
                            }
                            showKlawedStatus(toolMessage);
                        } else {
                            showKlawedStatus('AI is thinking');
                        }
                        break;
                    default:
                        if (typeof content === 'string') {
                            handleStreamComplete(content);
                            removeTypingIndicator();
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
                    removeTypingIndicator();
                    // Reset API call time when we get a done response
                    lastApiCallTime = null;
                } else if (event.data.startsWith('ERROR:')) {
                    const errorMessage = event.data.substring(6);
                    addSystemMessage('✗ ' + errorMessage, 'error');
                    removeTypingIndicator();
                    // Reset API call time when we get an error
                    lastApiCallTime = null;
                } else {
                    // Check if it's a tool-related message in legacy format
                    if (event.data.includes('[TOOL') || event.data.includes('[TOOL RESULT') || 
                        event.data.startsWith('[TOOL') || event.data.startsWith('[TOOL RESULT')) {
                        debug('Filtered out tool-related legacy message:', event.data);
                        showKlawedStatus('AI is thinking');
                    } else {
                        addMessage(event.data, false);
                        removeTypingIndicator();
                    }
                }
            }
        };

        ws.onerror = (error) => {
            console.error('WebSocket error:', error);
            removeTypingIndicator();
            updateStatus(false, 'Connection error');
        };

        ws.onclose = () => {
            console.info('WebSocket closed');
            removeTypingIndicator();
            updateStatus(false, 'Disconnected');
            setTimeout(connectWebSocket, 3000);
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

        addMessage(message, true);
        addTypingIndicator();
        ws.send(message);
        if (elements.messageInput) {
            elements.messageInput.value = '';
            // Reset textarea height after sending
            elements.messageInput.style.height = 'auto';
        }
        currentStreamMessageId = null;
        currentStreamContent = '';
    }

    if (elements.messageForm && elements.messageInput) {
        elements.messageForm.addEventListener('submit', (e) => {
            e.preventDefault();
            sendMessage(elements.messageInput.value);
        });

        elements.messageInput.addEventListener('keydown', (e) => {
            if (e.key === 'Enter' && !e.shiftKey) {
                e.preventDefault();
                elements.messageForm.dispatchEvent(new Event('submit'));
            }
            // Shift+Enter will insert new line (textarea handles this automatically)
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
                elements.messageInput.value = button.textContent || '';
                elements.messageForm.dispatchEvent(new Event('submit'));
            }
        });
    });

    if (elements.uploadButton && elements.fileInput) {
        elements.uploadButton.addEventListener('click', () => {
            elements.fileInput.click();
        });

        elements.fileInput.addEventListener('change', async (e) => {
            const files = Array.from(e.target.files || []);
            if (files.length === 0) return;

            if (!sessionId) {
                addSystemMessage('✗ No session available. Please reconnect.', 'error');
                return;
            }

            const fileNames = files.map(f => f.name).join(', ');
            addSystemMessage('📤 Uploading ' + files.length + ' file(s): ' + fileNames + '...', 'info');

            const formData = new FormData();
            files.forEach(file => formData.append('files', file));

            try {
                const response = await fetch('/file-chat/upload', {
                    method: 'POST',
                    headers: {
                        'X-Session-ID': sessionId
                    },
                    body: formData
                });

                if (response.ok) {
                    const result = await response.json();
                    addSystemMessage('✓ Successfully uploaded ' + result.count + ' file(s)', 'success');

                    if (isConnected && result.files && result.files.length > 0) {
                        const uploadMessage = "I've uploaded the following files: " + result.files.join(', ') + '. Please analyze them.';
                        sendMessage(uploadMessage);
                    }
                } else {
                    const errorText = await response.text();
                    addSystemMessage('✗ Upload failed: ' + errorText, 'error');
                }
            } catch (error) {
                console.error('Upload error:', error);
                addSystemMessage('✗ Upload error: ' + error.message, 'error');
            }

            elements.fileInput.value = '';
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
