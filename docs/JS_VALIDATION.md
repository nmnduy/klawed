# JavaScript Validation

FileSurf v2 automatically validates all JavaScript files during the build process to catch syntax errors and enforce coding conventions.

## What Gets Validated

### 1. Syntax Validation
All JavaScript files are parsed using [meriyah](https://github.com/meriyah/meriyah), a fast and spec-compliant JavaScript parser. This catches:

- **Syntax errors**: Missing braces, brackets, parentheses
- **Unclosed blocks**: Functions, if statements, loops, etc.
- **Invalid JavaScript**: Any code that doesn't conform to ECMAScript standards

**Syntax errors will fail the build immediately.**

### 2. Convention Validation
- **Module files** (loaded with `<script type="module">`) MAY use `import`/`export`
- **Classic scripts** (loaded with `<script defer>`) should NOT use `import`/`export`

See [JS_CONVENTIONS.md](./JS_CONVENTIONS.md) for full details on conventions.

## When Validation Runs

Validation runs automatically during:
- `npm run build` (production build)
- `npm run build:dev` (development build)

## Running Validation Manually

### Check JavaScript Only
```bash
npm run check:js
```

### Check Using Shell Script
```bash
./scripts/check-js-syntax.sh
```

## Example Output

### Success
```
🔍 Validating JS conventions...
   Checking 14 JS files (8 module files excluded from export/import check)
   ✅ All JS files follow conventions.
```

### Syntax Error
```
🔍 Validating JS conventions...
   Checking 14 JS files (8 module files excluded from export/import check)

❌ JS Validation Errors:

   SYNTAX ERRORS (must be fixed):

   fileChat.js:1830:0
   └─ Syntax error: Unexpected token: 'end of source'

   Found 1 syntax error(s) and 0 convention warning(s).

   ❌ Build failed due to syntax errors. Please fix and try again.
```

### Convention Warning
```
🔍 Validating JS conventions...
   Checking 14 JS files (8 module files excluded from export/import check)

❌ JS Validation Errors:

   CONVENTION WARNINGS:

   myScript.js:42
   └─ Found 'export' statement in classic script. Remove 'export' or add file to MODULE_FILES list.

   Found 0 syntax error(s) and 1 convention warning(s).
```

## Configuration

### Adding Module Files
If you create a new ES module file, add it to the `MODULE_FILES` set in `scripts/validate-js-conventions.js`:

```javascript
const MODULE_FILES = new Set([
    'fileChat.js',
    'fileExplorer.js',
    'authUtils.js',
    'myNewModule.js',  // Add your module here
    // ...
]);
```

## Benefits

1. **Catch errors early**: Syntax errors are caught at build time, not runtime
2. **Prevent deployment issues**: Invalid JavaScript won't make it to production
3. **Consistent code**: Enforces coding conventions across the project
4. **Fast feedback**: Validation runs in < 1 second for the entire codebase

## Technical Details

- **Parser**: [meriyah](https://github.com/meriyah/meriyah) v7.0.0
- **Standards**: ECMAScript 2020+ with web compatibility
- **Location**: `scripts/validate-js-conventions.js`
- **Integration**: Runs in `prebuild` hook via npm scripts

## Related Documentation

- [JS_CONVENTIONS.md](./JS_CONVENTIONS.md) - JavaScript coding conventions
- [JS_CACHE_BUSTING.md](./JS_CACHE_BUSTING.md) - JavaScript file hashing for cache busting
