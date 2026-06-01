// Background service worker for Klawed Browser Controller
// Handles native messaging and browser control commands

const NATIVE_HOST_NAME = 'com.klawed.browser_controller';

let nativePort = null;
let isConnected = false;
let pendingResolvers = new Map();

// ─── Native Messaging ────────────────────────────────────────────────────────

async function connectNativeHost() {
  if (nativePort) return true;

  try {
    nativePort = chrome.runtime.connectNative(NATIVE_HOST_NAME);

    // Check immediately if Chrome rejected the connection
    const lastErr = chrome.runtime.lastError;
    if (lastErr) {
      console.error('connectNative failed immediately:', lastErr.message);
      nativePort = null;
      isConnected = false;
      return false;
    }

    nativePort.onMessage.addListener(async (message) => {
      console.log('Received from native host:', message);

      // Command FROM the host (klawed → host → extension): execute it
      if (message.command && message.id) {
        try {
          // Enforce windowId for all commands except window-agnostic system commands.
          // This eliminates any chance of multi-agent collision — every agent must
          // explicitly declare which window it's operating on.
          const windowAgnostic = new Set(['newWindow', 'closeWindow', 'listWindows', 'ping', 'getInfo']);
          if (!windowAgnostic.has(message.command)) {
            const params = message.params || {};
            if (params.windowId == null) {
              throw new Error(
                `windowId is REQUIRED for '${message.command}'. ` +
                `Commands that target a page/DOM/tab MUST specify a Chrome window ID ` +
                `to prevent multi-agent collisions. Obtain a windowId from newWindow first.`
              );
            }
          }

          // For page-interacting commands, enable dialog monitoring on the
          // target tab so alerts/confirms/prompts are auto-dismissed and reported.
          // System commands (ping, getInfo, newWindow, listWindows, listTabs, etc.) are excluded.
          const pageCommands = new Set([
            'navigate', 'navigateTab', 'goBack', 'goForward', 'reload',
            'getPageInfo', 'getPageSource', 'getReadableText',
            'click', 'type', 'getText', 'getHtml', 'getAttribute',
            'scroll', 'scrollBy', 'scrollToElement', 'evaluate',
            'waitForElement', 'findElements', 'getLinks', 'getForms',
            'fillForm', 'submitForm', 'pressKey', 'uploadFile', 'cdpSend',
            'screenshot',
          ]);
          let targetTab = null;
          if (pageCommands.has(message.command)) {
            try {
              targetTab = await getActiveTab(
                message.params?.windowId != null ? message.params.windowId : undefined
              );
              await ensureDialogMonitoring(targetTab.id).catch(() => {});
            } catch (e) {
              // No active tab is OK (e.g., first command after browser launch)
            }
          }

          const result = await executeCommand(message.command, message.params || {});

          // Attach any alerts that fired during (or since) the last command on this tab
          if (targetTab) {
            const alerts = drainAlerts(targetTab.id);
            if (alerts.length > 0) {
              result._alerts = alerts;
            }
          }

          nativePort.postMessage({ id: message.id, result });
        } catch (err) {
          nativePort.postMessage({ id: message.id, error: err.message });
        }
        return;
      }

      // Response to a command the extension previously sent
      if (message.id && pendingResolvers.has(message.id)) {
        const { resolve, reject } = pendingResolvers.get(message.id);
        pendingResolvers.delete(message.id);
        if (message.error) {
          reject(new Error(message.error));
        } else {
          resolve(message.result);
        }
        return;
      }

      // Status broadcast from host
      if (message.type === 'status') {
        isConnected = message.connected;
        broadcastToTabs({ type: 'nativeStatus', connected: isConnected });
      }
    });

    nativePort.onDisconnect.addListener(() => {
      const err = chrome.runtime.lastError;
      console.log('Native host disconnected:', err ? err.message : '(no error)');
      const wasIntentional = !err || err.message === 'Native host has exited.';
      isConnected = false;
      nativePort = null;
      broadcastToTabs({ type: 'nativeStatus', connected: false });

      // Only auto-reconnect for unexpected disconnects, not "host not found" errors
      // (which would spam reconnects forever).
      if (!wasIntentional && err && err.message.includes('not found')) {
        console.log('Native host not found — not retrying automatically.');
        return;
      }
      // Use chrome.alarms for reconnect so it survives service worker idle kills.
      chrome.alarms.create('reconnect', { delayInMinutes: 1/30 }); // ~2 seconds
    });

    isConnected = true;
    return true;
  } catch (error) {
    console.error('Failed to connect to native host:', error);
    isConnected = false;
    return false;
  }
}

// Send a command TO the host (extension-initiated, e.g. from popup)
// and wait for the response.
async function sendToNativeHost(command, params = {}) {
  if (!nativePort) {
    const connected = await connectNativeHost();
    if (!connected) throw new Error('Failed to connect to native host');
  }

  const id = generateId();
  return new Promise((resolve, reject) => {
    pendingResolvers.set(id, { resolve, reject });

    const timer = setTimeout(() => {
      if (pendingResolvers.has(id)) {
        pendingResolvers.delete(id);
        reject(new Error('Command timeout (30s)'));
      }
    }, 30000);

    // Wrap resolve/reject to clear timer
    pendingResolvers.set(id, {
      resolve: (val) => { clearTimeout(timer); resolve(val); },
      reject:  (err) => { clearTimeout(timer); reject(err); },
    });

    nativePort.postMessage({ id, command, params });
  });
}

function generateId() {
  return Date.now().toString(36) + Math.random().toString(36).slice(2);
}

async function broadcastToTabs(message) {
  const tabs = await chrome.tabs.query({});
  for (const tab of tabs) {
    try { chrome.tabs.sendMessage(tab.id, message).catch(() => {}); } catch (e) {}
  }
}

// ─── Command Execution ───────────────────────────────────────────────────────

// executeCommand dispatches a command name + params to the right Chrome API.
// This is called both when klawed sends commands (via native host) AND from
// the popup UI.
async function executeCommand(command, params) {
  switch (command) {
    // ── Navigation ──────────────────────────────────────────────────────────
    case 'navigate': {
      const tab = await getActiveTab(params.windowId);
      const originalUrl = tab.url;
      await chrome.tabs.update(tab.id, { url: params.url });
      // Wait for the page to load
      await waitForTabLoad(tab.id);
      // Verify navigation actually happened — beforeunload dialogs block it silently
      const updatedTab = await chrome.tabs.get(tab.id);
      if (updatedTab.url === originalUrl && originalUrl !== params.url) {
        return {
          success: false,
          error: `Navigation to "${params.url}" was blocked. The page likely displayed a confirmation dialog (e.g. "Are you sure you want to leave this page?"). The tab is still at "${originalUrl}". The dialog was auto-dismissed; check _alerts on this response for details. If navigation still failed, you may need to interact with the browser manually or use evaluate() to clear the form before navigating.`,
        };
      }
      return { success: true, url: updatedTab.url };
    }

    case 'navigateTab': {
      const tabId = params.tabId;
      const tab = await chrome.tabs.get(tabId);
      const originalUrl = tab.url;
      await chrome.tabs.update(tabId, { url: params.url });
      await waitForTabLoad(tabId);
      const updatedTab = await chrome.tabs.get(tabId);
      if (updatedTab.url === originalUrl && originalUrl !== params.url) {
        return {
          success: false,
          error: `Navigation to "${params.url}" was blocked. The page likely displayed a confirmation dialog (e.g. "Are you sure you want to leave this page?"). The tab is still at "${originalUrl}". The dialog was auto-dismissed; check _alerts on this response for details. If navigation still failed, you may need to clear the form before navigating.`,
        };
      }
      return { success: true, tabId, url: updatedTab.url };
    }

    case 'goBack': {
      const tab = await getActiveTab(params.windowId);
      await chrome.tabs.goBack(tab.id);
      return { success: true };
    }

    case 'goForward': {
      const tab = await getActiveTab(params.windowId);
      await chrome.tabs.goForward(tab.id);
      return { success: true };
    }

    case 'reload': {
      const tab = await getActiveTab(params.windowId);
      await chrome.tabs.reload(tab.id);
      await waitForTabLoad(tab.id);
      return { success: true };
    }

    // ── Window Management ────────────────────────────────────────────────────
    case 'newWindow': {
      const createParams = { url: params.url || 'about:blank' };
      if (params.incognito) createParams.incognito = true;
      if (params.focused !== false) createParams.focused = true;
      const win = await chrome.windows.create(createParams);
      // Wait for the tab to load if a URL was given
      if (params.url && win.tabs && win.tabs[0]) {
        await waitForTabLoad(win.tabs[0].id);
      }
      return { windowId: win.id, focused: win.focused, tabCount: win.tabs?.length || 1 };
    }

    case 'closeWindow': {
      await chrome.windows.remove(params.windowId);
      return { success: true };
    }

    case 'listWindows': {
      const windows = await chrome.windows.getAll({ populate: true });
      return {
        windows: windows.map(w => ({
          id: w.id,
          focused: w.focused,
          incognito: w.incognito,
          tabCount: w.tabs?.length || 0,
          tabs: (w.tabs || []).map(t => ({
            id: t.id, url: t.url, title: t.title, active: t.active,
          })),
        })),
      };
    }

    // ── Tab Management ───────────────────────────────────────────────────────
    case 'listTabs': {
      const query = (params.windowId != null) ? { windowId: params.windowId } : {};
      const tabs = await chrome.tabs.query(query);
      return {
        tabs: tabs.map(t => ({
          id: t.id, url: t.url, title: t.title, active: t.active,
          windowId: t.windowId, index: t.index,
        })),
      };
    }

    case 'getActiveTab': {
      const query = (params.windowId != null)
        ? { active: true, windowId: params.windowId }
        : { active: true, currentWindow: true };
      const [tab] = await chrome.tabs.query(query);
      if (!tab) throw new Error(params.windowId ? `No active tab in window ${params.windowId}` : 'No active tab');
      return { id: tab.id, url: tab.url, title: tab.title, windowId: tab.windowId };
    }

    case 'newTab': {
      const createParams = { url: params.url || 'about:blank' };
      if (params.windowId != null) createParams.windowId = params.windowId;
      const tab = await chrome.tabs.create(createParams);
      if (params.url) await waitForTabLoad(tab.id);
      return { tabId: tab.id, url: tab.url, windowId: tab.windowId };
    }

    case 'closeTab': {
      await chrome.tabs.remove(params.tabId);
      return { success: true };
    }

    case 'switchTab': {
      await chrome.tabs.update(params.tabId, { active: true });
      return { success: true };
    }

    // ── Page Info ────────────────────────────────────────────────────────────
    case 'getPageInfo': {
      const result = await execInWindowTab(params.windowId, () => ({
        url: window.location.href,
        title: document.title,
        readyState: document.readyState,
        scrollY: window.scrollY,
        scrollX: window.scrollX,
        documentHeight: document.documentElement.scrollHeight,
        viewportHeight: window.innerHeight,
      }));
      return result[0].result;
    }

    case 'getPageSource': {
      const result = await execInWindowTab(params.windowId, () => {
        const clone = document.documentElement.cloneNode(true);
        // Strip noise that LLMs don't need
        clone.querySelectorAll('script, style, noscript, svg, link[rel="stylesheet"]').forEach(el => el.remove());
        return clone.outerHTML;
      });
      const html = result[0].result || '';
      const maxLength = params.maxLength || 20000;
      return { html: html.length > maxLength ? html.slice(0, maxLength) + `\n<!-- truncated at ${maxLength} chars, full length ${html.length} -->` : html };
    }

    case 'getReadableText': {
      const result = await execInWindowTab(params.windowId, () => {
        const clone = document.documentElement.cloneNode(true);
        clone.querySelectorAll('script, style, noscript').forEach(el => el.remove());
        // Collapse excessive whitespace
        return clone.innerText.replace(/\n{3,}/g, '\n\n').trim();
      });
      const text = result[0].result || '';
      const maxLength = params.maxLength || 8000;
      return { text: text.length > maxLength ? text.slice(0, maxLength) + `\n[truncated at ${maxLength} chars, full length ${text.length}]` : text };
    }

    // ── DOM Interaction ──────────────────────────────────────────────────────
    case 'click': {
      const result = await execInWindowTab(params.windowId, (selector) => {
        const el = document.querySelector(selector);
        if (!el) return { success: false, error: 'Element not found: ' + selector };
        el.click();
        return { success: true, selector };
      }, [params.selector]);
      return result[0].result;
    }

    case 'type': {
      // Human-like typing: types character-by-character with randomized
      // delays (50–200ms per char, avg ~125ms = ~480 CPM) to avoid bot
      // detection. Most sites flag instantaneous form fills as automated.
      const result = await execInWindowTab(params.windowId, async (selector, text, clearFirst) => {
        const el = document.querySelector(selector);
        if (!el) return { success: false, error: 'Element not found: ' + selector };
        el.focus();
        if (clearFirst) {
          el.value = '';
          el.dispatchEvent(new Event('input', { bubbles: true }));
        }
        for (const char of text) {
          el.value += char;
          el.dispatchEvent(new Event('input', { bubbles: true }));
          // Random delay 50–200ms, averaging ~125ms (~480 CPM).
          // Slower for whitespace (humans pause between words).
          const delayMs = char === ' '
            ? 80 + Math.random() * 200
            : 50 + Math.random() * 150;
          await new Promise(r => setTimeout(r, delayMs));
        }
        el.dispatchEvent(new Event('change', { bubbles: true }));
        return { success: true, selector, textLength: text.length };
      }, [params.selector, params.text, params.clearFirst !== false]);
      return result[0].result;
    }

    case 'getText': {
      const result = await execInWindowTab(params.windowId, (selector) => {
        const el = selector ? document.querySelector(selector) : document.body;
        return el ? el.innerText : null;
      }, [params.selector || null]);
      const text = result[0].result || '';
      const maxLength = params.maxLength || 8000;
      return { text: text.length > maxLength ? text.slice(0, maxLength) + `\n[truncated at ${maxLength} chars]` : text };
    }

    case 'getHtml': {
      const result = await execInWindowTab(params.windowId, (selector) => {
        const el = selector ? document.querySelector(selector) : document.body;
        return el ? el.innerHTML : null;
      }, [params.selector || null]);
      return { html: result[0].result };
    }

    case 'getAttribute': {
      const result = await execInWindowTab(params.windowId, (selector, attr) => {
        const el = document.querySelector(selector);
        return el ? el.getAttribute(attr) : null;
      }, [params.selector, params.attribute]);
      return { value: result[0].result };
    }

    case 'scroll': {
      const result = await execInWindowTab(params.windowId, (x, y) => {
        window.scrollTo(x, y);
        return { scrollX: window.scrollX, scrollY: window.scrollY };
      }, [params.x || 0, params.y || 0]);
      return result[0].result;
    }

    case 'scrollBy': {
      const result = await execInWindowTab(params.windowId, (dx, dy) => {
        window.scrollBy(dx, dy);
        return { scrollX: window.scrollX, scrollY: window.scrollY };
      }, [params.dx || 0, params.dy || 0]);
      return result[0].result;
    }

    case 'scrollToElement': {
      const result = await execInWindowTab(params.windowId, (selector) => {
        const el = document.querySelector(selector);
        if (!el) return { success: false, error: 'Element not found: ' + selector };
        el.scrollIntoView({ behavior: 'smooth', block: 'center' });
        return { success: true };
      }, [params.selector]);
      return result[0].result;
    }

    case 'evaluate': {
      // Use Chrome DevTools Protocol (Runtime.evaluate) to bypass CSP.
      // This enables arbitrary JS execution on any page, including SPAs with
      // strict Content-Security-Policy headers. The tradeoff: Chrome shows a
      // "debugging this browser" warning bar while the debugger is attached.
      const tab = await getActiveTab(params.windowId);

      // Attach debugger (swallows "already attached" so repeated evaluate
      // calls are fast — the debugger stays attached across calls and is
      // auto-detached by Chrome on tab navigation).
      try {
        await chrome.debugger.attach({ tabId: tab.id }, '1.3');
      } catch (e) {
        if (!e.message.includes('already attached')) {
          throw new Error(`Cannot attach debugger: ${e.message}. Close DevTools or other debuggers on this tab.`);
        }
      }

      try {
        const evalResult = await chrome.debugger.sendCommand(
          { tabId: tab.id },
          'Runtime.evaluate',
          {
            expression: params.code,
            returnByValue: true,
            awaitPromise: true,
            timeout: params.timeout || 5000,
          }
        );

        if (evalResult.exceptionDetails) {
          const exc = evalResult.exceptionDetails;
          const errMsg = exc.exception?.description || exc.text || 'Evaluation failed';
          return { error: errMsg };
        }

        const value = evalResult.result?.value;
        if (value === undefined) return { result: undefined };
        if (value === null) return { result: null };
        return { result: value };
      } catch (e) {
        return { error: e.message };
      }
      // NOTE: We intentionally do NOT detach the debugger here.
      // Keeping it attached makes subsequent evaluate calls fast (no
      // re-attach overhead). Chrome auto-detaches on tab navigation.
      // The tradeoff is the persistent debugger warning bar in Chrome.
    }

    case 'cdpSend': {
      // Send an arbitrary CDP command via chrome.debugger.sendCommand.
      // This enables Input.dispatchKeyEvent (real keystrokes with isTrusted:true),
      // DOM manipulation, network interception, and any other CDP method.
      // Uses the same debugger attachment as evaluate.
      const tab = await getActiveTab(params.windowId);
      try {
        await chrome.debugger.attach({ tabId: tab.id }, '1.3');
      } catch (e) {
        if (!e.message.includes('already attached')) {
          throw new Error(`Cannot attach debugger: ${e.message}. Close DevTools or other debuggers on this tab.`);
        }
      }
      try {
        const result = await chrome.debugger.sendCommand(
          { tabId: tab.id },
          params.method,
          params.cdpParams || {}
        );
        return { result };
      } catch (e) {
        return { error: e.message };
      }
    }

    case 'waitForElement': {
      const result = await execInWindowTab(params.windowId, (selector, timeout) => {
        return new Promise((resolve) => {
          const el = document.querySelector(selector);
          if (el) { resolve({ found: true, selector }); return; }
          const observer = new MutationObserver(() => {
            const found = document.querySelector(selector);
            if (found) {
              observer.disconnect();
              clearTimeout(timer);
              resolve({ found: true, selector });
            }
          });
          observer.observe(document.body, { childList: true, subtree: true });
          const timer = setTimeout(() => {
            observer.disconnect();
            resolve({ found: false, selector, timeout });
          }, timeout || 10000);
        });
      }, [params.selector, params.timeout || 10000]);
      return result[0].result;
    }

    case 'findElements': {
      const result = await execInWindowTab(params.windowId, (selector, limit) => {
        const els = document.querySelectorAll(selector);
        return Array.from(els).slice(0, limit).map((el, idx) => {
          const rect = el.getBoundingClientRect();
          return {
            index: idx,
            tagName: el.tagName,
            id: el.id || null,
            className: el.className || null,
            text: el.textContent?.trim().substring(0, 100) || null,
            visible: rect.width > 0 && rect.height > 0,
          };
        });
      }, [params.selector, params.limit || 50]);
      const els = result[0].result || [];
      return { elements: els, count: els.length };
    }

    case 'getLinks': {
      const result = await execInWindowTab(params.windowId, (limit) => {
        const seen = new Set();
        return Array.from(document.links)
          .filter(l => { if (seen.has(l.href)) return false; seen.add(l.href); return true; })
          .slice(0, limit)
          .map(l => ({ href: l.href, text: l.textContent?.trim().substring(0, 80) || null }));
      }, [params.limit || 50]);
      const links = result[0].result || [];
      return { links, count: links.length };
    }

    case 'getForms': {
      const result = await execInWindowTab(params.windowId, () =>
        Array.from(document.forms).map((form, idx) => ({
          index: idx,
          id: form.id || null,
          action: form.action || null,
          method: form.method || 'get',
          inputs: Array.from(form.elements).map(el => ({
            name: el.name || null,
            type: el.type || el.tagName.toLowerCase(),
            id: el.id || null,
          })),
        }))
      );
      return { forms: result[0].result };
    }

    case 'uploadFile': {
      // Use CDP DOM.setFileInputFiles — the only programmatic way to
      // set files on <input type="file"> elements (browser security
      // prevents content scripts and page JS from doing this).
      const tab = await getActiveTab(params.windowId);

      // Attach debugger (reuses existing attachment if already attached via evaluate)
      try {
        await chrome.debugger.attach({ tabId: tab.id }, '1.3');
      } catch (e) {
        if (!e.message.includes('already attached')) {
          throw new Error(`Cannot attach debugger: ${e.message}`);
        }
      }

      try {
        // Step 1: Get the document root node
        const doc = await chrome.debugger.sendCommand(
          { tabId: tab.id }, 'DOM.getDocument', { depth: 0 }
        );

        // Step 2: Query for the file input element
        const nodeResult = await chrome.debugger.sendCommand(
          { tabId: tab.id }, 'DOM.querySelector',
          { nodeId: doc.root.nodeId, selector: params.selector }
        );

        if (!nodeResult || !nodeResult.nodeId) {
          return { success: false, error: `File input element not found: ${params.selector}` };
        }

        // Step 3: Set the files (absolute paths required)
        await chrome.debugger.sendCommand(
          { tabId: tab.id }, 'DOM.setFileInputFiles',
          { files: params.filePaths, nodeId: nodeResult.nodeId }
        );

        return { success: true, selector: params.selector, fileCount: params.filePaths.length };
      } catch (e) {
        return { success: false, error: e.message };
      }
      // NOTE: Debugger stays attached for fast subsequent evaluate/uploadFile calls.
    }

    case 'fillForm': {
      const result = await execInWindowTab(params.windowId, (data) => {
        const results = [];
        for (const [selector, value] of Object.entries(data)) {
          const el = document.querySelector(selector);
          if (el) {
            if (el.tagName === 'SELECT') el.value = value;
            else if (el.type === 'checkbox' || el.type === 'radio') el.checked = value;
            else el.value = value;
            el.dispatchEvent(new Event('input', { bubbles: true }));
            el.dispatchEvent(new Event('change', { bubbles: true }));
            results.push({ selector, success: true });
          } else {
            results.push({ selector, success: false, error: 'Not found' });
          }
        }
        return results;
      }, [params.data]);
      return { results: result[0].result };
    }

    case 'submitForm': {
      const result = await execInWindowTab(params.windowId, (selector) => {
        const form = selector ? document.querySelector(selector) : document.querySelector('form');
        if (form) { form.submit(); return { success: true }; }
        return { success: false, error: 'Form not found' };
      }, [params.selector || null]);
      return result[0].result;
    }

    case 'pressKey': {
      const result = await execInWindowTab(params.windowId, (selector, key) => {
        const el = selector ? document.querySelector(selector) : document.activeElement;
        if (!el) return { success: false, error: 'Element not found' };
        el.dispatchEvent(new KeyboardEvent('keydown', { key, bubbles: true }));
        el.dispatchEvent(new KeyboardEvent('keypress', { key, bubbles: true }));
        el.dispatchEvent(new KeyboardEvent('keyup', { key, bubbles: true }));
        return { success: true, key };
      }, [params.selector || null, params.key]);
      return result[0].result;
    }

    // ── Screenshot ───────────────────────────────────────────────────────────
    case 'screenshot': {
      let captureWindowId;
      if (params.windowId != null) {
        captureWindowId = params.windowId;
      } else {
        const [tab] = await chrome.tabs.query({ active: true, currentWindow: true });
        if (!tab) throw new Error('No active tab');
        captureWindowId = tab.windowId;
      }
      const dataUrl = await chrome.tabs.captureVisibleTab(captureWindowId, { format: 'png' });
      return { dataUrl, format: 'png' };
    }

    // ── System ───────────────────────────────────────────────────────────────
    case 'ping':
      return { pong: true, timestamp: Date.now() };

    case 'getInfo':
      return {
        name: 'Klawed Browser Controller',
        version: '2.4.0',
        hostType: 'go',
        commands: [
          'newWindow', 'closeWindow', 'listWindows',
          'navigate', 'navigateTab', 'goBack', 'goForward', 'reload',
          'listTabs', 'getActiveTab', 'newTab', 'closeTab', 'switchTab',
          'getPageInfo', 'getPageSource', 'getReadableText',
          'click', 'type', 'getText', 'getHtml', 'getAttribute',
          'scroll', 'scrollBy', 'scrollToElement', 'evaluate',
          'waitForElement', 'findElements', 'getLinks', 'getForms',
          'fillForm', 'submitForm', 'pressKey', 'uploadFile',
          'screenshot', 'ping', 'getInfo', 'cdpSend',
        ],
      };

    default:
      throw new Error(`Unknown command: ${command}`);
  }
}

// ─── Helpers ─────────────────────────────────────────────────────────────────

// Get the active tab, optionally scoped to a specific window.
// When windowId is undefined/null, falls back to currentWindow.
async function getActiveTab(windowId) {
  const query = (windowId != null)
    ? { active: true, windowId }
    : { active: true, currentWindow: true };
  const [tab] = await chrome.tabs.query(query);
  if (!tab) throw new Error(windowId ? `No active tab in window ${windowId}` : 'No active tab');
  return tab;
}

// Execute script in the active tab of a specific window (or current window if no windowId).
async function execInWindowTab(windowId, func, args = []) {
  const tab = await getActiveTab(windowId);
  return chrome.scripting.executeScript({ target: { tabId: tab.id }, func, args });
}

// Shorthand for chrome.scripting.executeScript on the active tab (legacy backward-compat).
async function execInActiveTab(func, args = []) {
  return execInWindowTab(null, func, args);
}

// Wait for a tab to finish loading (up to 15 seconds)
function waitForTabLoad(tabId) {
  return new Promise((resolve) => {
    const listener = (updatedTabId, info) => {
      if (updatedTabId === tabId && info.status === 'complete') {
        chrome.tabs.onUpdated.removeListener(listener);
        resolve();
      }
    };
    chrome.tabs.onUpdated.addListener(listener);
    // Safety timeout
    setTimeout(() => {
      chrome.tabs.onUpdated.removeListener(listener);
      resolve();
    }, 15000);
  });
}

// Detach debugger from all tabs (called on Disconnect to clear warning bar)
async function detachDebuggerFromAll() {
  const targets = await chrome.debugger.getTargets().catch(() => []);
  for (const target of targets) {
    if (target.attached && target.tabId) {
      await chrome.debugger.detach({ tabId: target.tabId }).catch(() => {});
    }
  }
}

// Log debugger detach events (e.g., user pressed Esc or clicked stop on the bar)
chrome.debugger.onDetach.addListener((source, reason) => {
  console.log('Debugger detached from tab', source.tabId, 'reason:', reason);
  dialogTabs.delete(source.tabId);
  tabAlerts.delete(source.tabId);
  // We don't need to clean up state since we attach on-demand; the next
  // evaluate call will simply re-attach if needed.
});

// ─── Alert / Dialog Monitoring ──────────────────────────────────────────────
//
// Browser dialogs (alert, confirm, prompt, beforeunload) block script
// execution on the tab. Without detection, commands silently hang and
// agents have no idea why. We use CDP to listen for Page.javascriptDialogOpening
// events, auto-dismiss dialogs, and report them on the next command.
//
// tabAlerts:  Map<tabId, Array<{type, message, url, timestamp}>>
//   Queued alerts waiting to be reported to the agent.
// dialogTabs: Set<tabId>
//   Tabs where debugger + dialog listener are active.

const tabAlerts = new Map();
const dialogTabs = new Set();

// Ensure CDP dialog monitoring is active for a tab.
// Attaches the debugger and enables Page domain for dialog events.
// Safe to call repeatedly — no-ops if already attached.
async function ensureDialogMonitoring(tabId) {
  if (dialogTabs.has(tabId)) return;
  try {
    await chrome.debugger.attach({ tabId }, '1.3');
  } catch (e) {
    // "already attached" is fine — evaluate() may have attached before us
    if (!e.message || !e.message.includes('already attached')) {
      console.warn('ensureDialogMonitoring: cannot attach debugger to tab', tabId, e.message);
      return;
    }
  }
  try {
    await chrome.debugger.sendCommand({ tabId }, 'Page.enable');
  } catch (e) {
    console.warn('ensureDialogMonitoring: Page.enable failed for tab', tabId, e.message);
  }
  dialogTabs.add(tabId);
  console.log('Dialog monitoring active on tab', tabId);
}

// CDP event listener for JavaScript dialogs.
// Auto-dismisses every dialog (accept=true) and queues the alert info
// so the next command response can surface it to the agent.
function dialogEventListener(source, method, params) {
  if (method !== 'Page.javascriptDialogOpening') return;
  const tabId = source.tabId;
  const alert = {
    type: params.type || 'alert', // 'alert', 'confirm', 'prompt', 'beforeunload'
    message: params.message || '',
    url: params.url || '',
    timestamp: Date.now(),
  };
  console.log('Dialog detected on tab', tabId, alert);
  // Store alert for later reporting
  if (!tabAlerts.has(tabId)) tabAlerts.set(tabId, []);
  tabAlerts.get(tabId).push(alert);
  // Auto-dismiss so the tab is unblocked
  chrome.debugger.sendCommand({ tabId }, 'Page.handleJavaScriptDialog', {
    accept: true,
    promptText: '', // empty string for prompt() defaults
  }).catch(err => console.warn('Failed to dismiss dialog on tab', tabId, err.message));
}

// Register the global CDP event listener (fires for all tabs with debugger attached)
chrome.debugger.onEvent.addListener(dialogEventListener);

// Drain and return all pending alerts for a tab, then clear the queue.
function drainAlerts(tabId) {
  const alerts = tabAlerts.get(tabId) || [];
  tabAlerts.delete(tabId);
  return alerts;
}

// ─── Message Listener (from popup / content scripts) ─────────────────────────

chrome.runtime.onMessage.addListener((request, sender, sendResponse) => {
  (async () => {
    try {
      let result;
      switch (request.action) {
        case 'connect':
          result = { connected: await connectNativeHost() };
          break;
        case 'disconnect':
          if (nativePort) { nativePort.disconnect(); nativePort = null; isConnected = false; }
          // Detach debugger from all tabs so the warning bar disappears
          await detachDebuggerFromAll();
          result = { connected: false };
          break;
        case 'getStatus':
          // If the service worker was killed and restarted, nativePort is gone.
          // Auto-reconnect transparently so the popup always comes up green.
          if (!nativePort) {
            await connectNativeHost().catch(() => {});
          }
          result = { connected: isConnected };
          break;
        case 'execute':
          result = await executeCommand(request.command, request.params || {});
          break;
        case 'sendToNative':
          result = await sendToNativeHost(request.command, request.params);
          break;
        // Legacy shorthand actions for popup
        case 'navigate':
          result = await executeCommand('navigate', { url: request.url });
          break;
        case 'newTab':
          result = await executeCommand('newTab', { url: request.url });
          break;
        case 'listTabs':
          result = await executeCommand('listTabs', {});
          break;
        case 'switchTab':
          result = await executeCommand('switchTab', { tabId: request.tabId });
          break;
        case 'closeTab':
          result = await executeCommand('closeTab', { tabId: request.tabId });
          break;
        case 'goBack':
          result = await executeCommand('goBack', {});
          break;
        case 'goForward':
          result = await executeCommand('goForward', {});
          break;
        case 'reload':
          result = await executeCommand('reload', {});
          break;
        default:
          throw new Error(`Unknown action: ${request.action}`);
      }
      sendResponse({ success: true, ...result });
    } catch (error) {
      console.error('Error handling message:', error);
      sendResponse({ success: false, error: error.message });
    }
  })();
  return true; // keep channel open for async
});

// ─── Startup ─────────────────────────────────────────────────────────────────

// Use alarms for reconnect so it survives service worker idle kills
chrome.alarms.onAlarm.addListener((alarm) => {
  if (alarm.name === 'reconnect') {
    console.log('Alarm fired: attempting reconnect...');
    connectNativeHost().catch(() => {});
  }
});

// Attempt to connect on startup; failure is normal if host isn't installed yet.
connectNativeHost().catch(() => {});
