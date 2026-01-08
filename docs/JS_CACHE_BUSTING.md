# JS Cache Busting

## Overview
JavaScript files are automatically hashed during **production** builds to prevent browser caching issues. This ensures users always get the latest JS code after deployments.

## How It Works

### Build Modes

**Development Mode** (`npm run build:dev`):
- Generates standard JS filenames: `fileChat.js`, `fileExplorer.js`, etc.
- No content hashing (faster iteration)
- Properties file points to non-hashed files

**Production Mode** (`npm run build`):
- Generates hashed JS filenames: `fileChat.9bc4af5d.js`, `fileExplorer.540f49d4.js`, etc.
- Hash changes whenever JS content changes
- Forces browser cache invalidation
- Properties file maps base names to hashed filenames

### Technical Implementation

1. **Build Script** (`scripts/hash-js.js`):
   - Scans `src/main/resources/META-INF/resources/js/` directory
   - Generates MD5 hash (first 8 chars) for each JS file
   - Copies each file with hash: `[name].[hash].js`
   - Creates `js-version.properties` with mappings

2. **Java Provider** (`JsVersionProvider.java`):
   - Reads `js-version.properties` at startup
   - Provides methods to get hashed filenames
   - Caches mappings in memory

3. **Template Globals** (`GlobalTemplateData.java`):
   - Exposes `jsPath(baseName)` function to templates
   - Example: `{jsPath('fileChat')}` → `/js/fileChat.9bc4af5d.js`

4. **Template Usage** (`fileChat.html`):
   ```html
   <script src="{jsPath('fileExplorer')}" defer></script>
   <script type="module" src="{jsPath('fileChat')}"></script>
   ```

## Files Generated

### Development Build
```properties
# js-version.properties
js.generated=2026-01-08T07:13:31.179Z
js.authUtils=authUtils.js
js.darkMode=darkMode.js
js.fileChat=fileChat.js
js.fileExplorer=fileExplorer.js
js.tabManager=tabManager.js
```

### Production Build
```properties
# js-version.properties
js.generated=2026-01-08T07:13:40.125Z
js.authUtils=authUtils.29971ffe.js
js.darkMode=darkMode.9833955d.js
js.fileChat=fileChat.9bc4af5d.js
js.fileExplorer=fileExplorer.540f49d4.js
js.tabManager=tabManager.e0dfd2f6.js
```

## Usage in Templates

### Basic Usage
```html
<!-- Use jsPath() to get the full path with hash -->
<script src="{jsPath('fileChat')}"></script>
<script src="{jsPath('fileExplorer')}" defer></script>
<script type="module" src="{jsPath('darkMode')}"></script>
```

### Getting Just the Filename
```html
<!-- If you need just the filename without /js/ prefix -->
<script>
  const filename = '{jsFilename('fileChat')}'; // fileChat.9bc4af5d.js
</script>
```

## Build Commands

```bash
# Development build (no hashing, faster)
npm run build:dev

# Production build (with hashing)
npm run build

# Build everything (CSS + JS + Quarkus JAR)
make build-dist
```

## Adding New JS Files

When you add a new JS file to `src/main/resources/META-INF/resources/js/`:

1. The file will be automatically detected during build
2. It will be hashed in production mode
3. Use `{jsPath('yourFileName')}` in templates (without .js extension)

Example:
- File: `myNewScript.js`
- Template: `<script src="{jsPath('myNewScript')}"></script>`
- Result (prod): `/js/myNewScript.a1b2c3d4.js`
- Result (dev): `/js/myNewScript.js`

## Cache Invalidation Strategy

- **Hash-based**: Hash changes only when JS content changes
- **Browser behavior**: Browser sees new filename, fetches fresh file
- **No server config needed**: No Cache-Control headers required
- **Graceful degradation**: Falls back to non-hashed filenames if properties file missing

## Integration with Quarkus

The system integrates seamlessly with Quarkus:

1. **Build Phase**: `npm run build` generates hashed files and properties
2. **Package Phase**: `mvn package` includes hashed files in JAR
3. **Runtime**: `JsVersionProvider` loads mappings at startup
4. **Templates**: Qute templates resolve `{jsPath()}` to hashed paths

## Troubleshooting

### JS files not hashing
- Ensure `NODE_ENV=production` is set
- Check that files exist in `src/main/resources/META-INF/resources/js/`
- Run `npm run build` (not `build:dev`)

### Template errors with jsPath
- Verify `JsVersionProvider` is loaded (check startup logs)
- Ensure `GlobalTemplateData` has both CSS and JS providers injected
- Check that the base name matches the file name (without .js)

### Old JS files being served
- Clear browser cache (hard refresh: Cmd+Shift+R / Ctrl+Shift+F5)
- Verify new hash was generated: check `js-version.properties`
- Ensure `JsVersionProvider` logged the new mappings at startup

## Related Files

- `scripts/hash-js.js` - Build script for hashing
- `src/main/java/com/filesurf/util/JsVersionProvider.java` - Java provider
- `src/main/java/com/filesurf/template/GlobalTemplateData.java` - Template globals
- `src/main/resources/js-version.properties` - Generated mappings
- `docs/CSS_CACHE_BUSTING.md` - Similar system for CSS
