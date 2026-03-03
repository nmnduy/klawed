// Popup script for Klawed Browser Controller

document.addEventListener('DOMContentLoaded', async () => {
  const dot         = document.getElementById('dot');
  const statusLabel = document.getElementById('statusLabel');
  const connectBtn  = document.getElementById('connectBtn');
  const hint        = document.getElementById('hint');
  const logEl       = document.getElementById('log');

  const actionBtns = [
    document.getElementById('btnPageInfo'),
    document.getElementById('btnListTabs'),
    document.getElementById('btnScreenshot'),
    document.getElementById('btnGetText'),
  ];

  function log(msg, type = 'dim') {
    const line = document.createElement('div');
    line.className = `log-line ${type}`;
    line.textContent = `[${new Date().toLocaleTimeString()}] ${msg}`;
    logEl.appendChild(line);
    logEl.scrollTop = logEl.scrollHeight;
    // Cap log at 100 entries
    while (logEl.children.length > 100) logEl.removeChild(logEl.firstChild);
  }

  function setConnected(connected) {
    dot.className = `dot ${connected ? 'connected' : 'disconnected'}`;
    statusLabel.textContent = connected ? 'Connected' : 'Disconnected';
    connectBtn.textContent  = connected ? 'Disconnect' : 'Connect';
    connectBtn.className    = `btn ${connected ? 'btn-red' : 'btn-green'}`;
    hint.textContent = connected
      ? 'Native messaging active. klawed can now control this browser.'
      : 'Click Connect to link this browser to the Klawed native host.';
    actionBtns.forEach(b => b.disabled = !connected);
  }

  async function send(action, extra = {}) {
    return chrome.runtime.sendMessage({ action, ...extra });
  }

  // Check initial status
  try {
    const r = await send('getStatus');
    setConnected(r.connected);
    if (r.connected) log('Already connected to native host.', 'ok');
  } catch (e) {
    setConnected(false);
  }

  // Connect / Disconnect
  connectBtn.addEventListener('click', async () => {
    const wasConnected = statusLabel.textContent === 'Connected';
    try {
      if (wasConnected) {
        await send('disconnect');
        log('Disconnected.', 'dim');
        setConnected(false);
      } else {
        log('Connecting to native host...', 'dim');
        const r = await send('connect');
        if (r.connected) {
          log('Connected!', 'ok');
          setConnected(true);
        } else {
          log('Connection failed. Is the native host installed?', 'err');
        }
      }
    } catch (e) {
      log(`Error: ${e.message}`, 'err');
    }
  });

  // Page Info
  document.getElementById('btnPageInfo').addEventListener('click', async () => {
    try {
      const r = await send('execute', { command: 'getPageInfo', params: {} });
      if (r.success) {
        log(`URL: ${r.url}`, 'ok');
        log(`Title: ${r.title}`, 'ok');
      } else {
        log(`Error: ${r.error}`, 'err');
      }
    } catch (e) { log(`Error: ${e.message}`, 'err'); }
  });

  // List Tabs
  document.getElementById('btnListTabs').addEventListener('click', async () => {
    try {
      const r = await send('listTabs');
      if (r.success) {
        log(`${r.tabs.length} tab(s) open:`, 'ok');
        r.tabs.forEach(t => log(`  ${t.active ? '●' : '○'} ${(t.title || 'Untitled').slice(0, 50)}`, 'dim'));
      } else {
        log(`Error: ${r.error}`, 'err');
      }
    } catch (e) { log(`Error: ${e.message}`, 'err'); }
  });

  // Screenshot
  document.getElementById('btnScreenshot').addEventListener('click', async () => {
    try {
      const r = await send('execute', { command: 'screenshot', params: {} });
      if (r.success && r.dataUrl) {
        log('Screenshot captured (data URL available).', 'ok');
      } else {
        log(`Error: ${r.error}`, 'err');
      }
    } catch (e) { log(`Error: ${e.message}`, 'err'); }
  });

  // Get Text
  document.getElementById('btnGetText').addEventListener('click', async () => {
    try {
      const r = await send('execute', { command: 'getReadableText', params: {} });
      if (r.success) {
        const preview = (r.text || '').slice(0, 200);
        log(`Text (${(r.text || '').length} chars): ${preview}...`, 'ok');
      } else {
        log(`Error: ${r.error}`, 'err');
      }
    } catch (e) { log(`Error: ${e.message}`, 'err'); }
  });
});
