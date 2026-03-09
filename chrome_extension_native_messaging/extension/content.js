// Content script for Klawed Browser Controller
// Injected into every page at document_end.
//
// The heavy lifting (DOM manipulation, JS evaluation) is done via
// chrome.scripting.executeScript() from background.js — which runs
// in the page's main world and has full DOM access.
//
// This content script's role is minimal:
//   1. Forward native-host status broadcasts to the page (optional)
//   2. Serve as the required content_scripts entry so the extension
//      loads cleanly under Manifest V3.

(function () {
  'use strict';

  // Listen for status broadcasts from the background service worker
  chrome.runtime.onMessage.addListener((message) => {
    if (message.type === 'nativeStatus') {
      // Could dispatch a custom DOM event for in-page listeners if needed.
      // For now, just silently accept the message so no "Unchecked runtime.lastError" fires.
    }
  });
})();
