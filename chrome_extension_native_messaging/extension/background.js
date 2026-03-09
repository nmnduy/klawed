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
          const result = await executeCommand(message.command, message.params || {});
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
      const [tab] = await chrome.tabs.query({ active: true, currentWindow: true });
      if (!tab) throw new Error('No active tab');
      await chrome.tabs.update(tab.id, { url: params.url });
      // Wait for the page to load
      await waitForTabLoad(tab.id);
      return { success: true, url: params.url };
    }

    case 'navigateTab': {
      const tabId = params.tabId;
      await chrome.tabs.update(tabId, { url: params.url });
      await waitForTabLoad(tabId);
      return { success: true, tabId, url: params.url };
    }

    case 'goBack': {
      const [tab] = await chrome.tabs.query({ active: true, currentWindow: true });
      await chrome.tabs.goBack(tab.id);
      return { success: true };
    }

    case 'goForward': {
      const [tab] = await chrome.tabs.query({ active: true, currentWindow: true });
      await chrome.tabs.goForward(tab.id);
      return { success: true };
    }

    case 'reload': {
      const [tab] = await chrome.tabs.query({ active: true, currentWindow: true });
      await chrome.tabs.reload(tab.id);
      await waitForTabLoad(tab.id);
      return { success: true };
    }

    // ── Tab Management ───────────────────────────────────────────────────────
    case 'listTabs': {
      const tabs = await chrome.tabs.query({});
      return {
        tabs: tabs.map(t => ({
          id: t.id, url: t.url, title: t.title, active: t.active,
          windowId: t.windowId, index: t.index,
        })),
      };
    }

    case 'getActiveTab': {
      const [tab] = await chrome.tabs.query({ active: true, currentWindow: true });
      if (!tab) throw new Error('No active tab');
      return { id: tab.id, url: tab.url, title: tab.title };
    }

    case 'newTab': {
      const tab = await chrome.tabs.create({ url: params.url || 'about:blank' });
      if (params.url) await waitForTabLoad(tab.id);
      return { tabId: tab.id, url: tab.url };
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
      const result = await execInActiveTab(() => ({
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
      const result = await execInActiveTab(() => {
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
      const result = await execInActiveTab(() => {
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
      const result = await execInActiveTab((selector) => {
        const el = document.querySelector(selector);
        if (!el) return { success: false, error: 'Element not found: ' + selector };
        el.click();
        return { success: true, selector };
      }, [params.selector]);
      return result[0].result;
    }

    case 'type': {
      const result = await execInActiveTab((selector, text, clearFirst) => {
        const el = document.querySelector(selector);
        if (!el) return { success: false, error: 'Element not found: ' + selector };
        el.focus();
        if (clearFirst) el.value = '';
        el.value = text;
        el.dispatchEvent(new Event('input', { bubbles: true }));
        el.dispatchEvent(new Event('change', { bubbles: true }));
        return { success: true, selector, textLength: text.length };
      }, [params.selector, params.text, params.clearFirst !== false]);
      return result[0].result;
    }

    case 'getText': {
      const result = await execInActiveTab((selector) => {
        const el = selector ? document.querySelector(selector) : document.body;
        return el ? el.innerText : null;
      }, [params.selector || null]);
      const text = result[0].result || '';
      const maxLength = params.maxLength || 8000;
      return { text: text.length > maxLength ? text.slice(0, maxLength) + `\n[truncated at ${maxLength} chars]` : text };
    }

    case 'getHtml': {
      const result = await execInActiveTab((selector) => {
        const el = selector ? document.querySelector(selector) : document.body;
        return el ? el.innerHTML : null;
      }, [params.selector || null]);
      return { html: result[0].result };
    }

    case 'getAttribute': {
      const result = await execInActiveTab((selector, attr) => {
        const el = document.querySelector(selector);
        return el ? el.getAttribute(attr) : null;
      }, [params.selector, params.attribute]);
      return { value: result[0].result };
    }

    case 'scroll': {
      const result = await execInActiveTab((x, y) => {
        window.scrollTo(x, y);
        return { scrollX: window.scrollX, scrollY: window.scrollY };
      }, [params.x || 0, params.y || 0]);
      return result[0].result;
    }

    case 'scrollBy': {
      const result = await execInActiveTab((dx, dy) => {
        window.scrollBy(dx, dy);
        return { scrollX: window.scrollX, scrollY: window.scrollY };
      }, [params.dx || 0, params.dy || 0]);
      return result[0].result;
    }

    case 'scrollToElement': {
      const result = await execInActiveTab((selector) => {
        const el = document.querySelector(selector);
        if (!el) return { success: false, error: 'Element not found: ' + selector };
        el.scrollIntoView({ behavior: 'smooth', block: 'center' });
        return { success: true };
      }, [params.selector]);
      return result[0].result;
    }

    case 'evaluate': {
      const result = await execInActiveTab((code) => {
        try {
          // eslint-disable-next-line no-eval
          const val = eval(code);
          return { result: typeof val === 'object' ? JSON.stringify(val) : String(val) };
        } catch (e) {
          return { error: e.message };
        }
      }, [params.code]);
      return result[0].result;
    }

    case 'waitForElement': {
      const result = await execInActiveTab((selector, timeout) => {
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
      const result = await execInActiveTab((selector, limit) => {
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
      const result = await execInActiveTab((limit) => {
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
      const result = await execInActiveTab(() =>
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

    case 'fillForm': {
      const result = await execInActiveTab((data) => {
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
      const result = await execInActiveTab((selector) => {
        const form = selector ? document.querySelector(selector) : document.querySelector('form');
        if (form) { form.submit(); return { success: true }; }
        return { success: false, error: 'Form not found' };
      }, [params.selector || null]);
      return result[0].result;
    }

    case 'pressKey': {
      const result = await execInActiveTab((selector, key) => {
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
      const [tab] = await chrome.tabs.query({ active: true, currentWindow: true });
      if (!tab) throw new Error('No active tab');
      const dataUrl = await chrome.tabs.captureVisibleTab(tab.windowId, { format: 'png' });
      return { dataUrl, format: 'png' };
    }

    // ── System ───────────────────────────────────────────────────────────────
    case 'ping':
      return { pong: true, timestamp: Date.now() };

    case 'getInfo':
      return {
        name: 'Klawed Browser Controller',
        version: '2.0.0',
        hostType: 'go',
        commands: [
          'navigate', 'navigateTab', 'goBack', 'goForward', 'reload',
          'listTabs', 'getActiveTab', 'newTab', 'closeTab', 'switchTab',
          'getPageInfo', 'getPageSource', 'getReadableText',
          'click', 'type', 'getText', 'getHtml', 'getAttribute',
          'scroll', 'scrollBy', 'scrollToElement', 'evaluate',
          'waitForElement', 'findElements', 'getLinks', 'getForms',
          'fillForm', 'submitForm', 'pressKey',
          'screenshot', 'ping', 'getInfo',
        ],
      };

    default:
      throw new Error(`Unknown command: ${command}`);
  }
}

// ─── Helpers ─────────────────────────────────────────────────────────────────

// Shorthand for chrome.scripting.executeScript on the active tab
async function execInActiveTab(func, args = []) {
  const [tab] = await chrome.tabs.query({ active: true, currentWindow: true });
  if (!tab) throw new Error('No active tab');
  return chrome.scripting.executeScript({ target: { tabId: tab.id }, func, args });
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
