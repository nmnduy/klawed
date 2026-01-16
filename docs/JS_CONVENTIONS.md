# JavaScript Conventions

**All JS files use the classic script pattern (not ES modules) unless they need imports.**

## Rules

1. Load with `<script src="..." defer></script>` — **NO** `type="module"`
2. **NO** `export` or `import` statements
3. Self-initialize on DOMContentLoaded
4. Expose public API on `window.Filesurf*` namespace

## Exception: ES Modules

Only use `type="module"` when the file needs to `import` from other modules (e.g., `fileChat.js`, `fileExplorer.js`).

| Pattern | Script Tag | `export`/`import`? |
|---------|-----------|-------------------|
| Classic | `<script src="..." defer>` | ❌ NO |
| Module | `<script type="module" src="...">` | ✅ YES |

## Template Structure for Classic Scripts

```javascript
// myModule.js
(function() {
    'use strict';

    class MyModule { /* ... */ }

    let instance = null;

    function init() {
        if (!instance) {
            instance = new MyModule();
        }
        instance.init();
    }

    // Auto-init when DOM is ready
    if (document.readyState === 'loading') {
        document.addEventListener('DOMContentLoaded', init);
    } else {
        init();
    }

    // Public API (optional)
    window.FilesurfMyModule = {
        open: () => instance?.open(),
        close: () => instance?.close()
    };
})();
```

## Common Mistake

Using `export` in a file loaded without `type="module"` causes a silent failure:

```javascript
// ❌ BAD - causes "Unexpected token 'export'" error
export function init() { }

// ✅ GOOD - works with classic <script defer>
function init() { }
```

## Module Files (Exceptions)

The following files are loaded as ES modules (`type="module"`) or imported by other modules, and MAY use `import`/`export`:

- `fileChat.js` - Main chat module (loaded as module)
- `fileExplorer.js` - File explorer module (loaded as module)
- `authUtils.js` - Imported by fileChat.js
- `darkMode.js` - Imported by fileChat.js
- `tabManager.js` - Imported by fileChat.js
- `loginWaitlist.js` - Loaded as module in login/waitlist pages

All other JS files should follow the classic script pattern.

## Validation

Run `npm run build` or `npm run build:dev` to validate JS files against these conventions.
The build will warn if a classic script contains `export`/`import` statements.
