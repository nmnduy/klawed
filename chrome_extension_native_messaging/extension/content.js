// Content script for Klawed Browser Controller
// Injected into every page at document_end.
//
// Role: minimal. Listens for status broadcasts from the background
// service worker so the extension loads cleanly under Manifest V3.

(function () {
  'use strict';

  chrome.runtime.onMessage.addListener((message) => {
    if (message.type === 'nativeStatus') {
      // Silently consume status broadcasts
    }
  });
})();
