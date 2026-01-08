# CSS Cache Busting

This project implements content-based cache busting for CSS files to prevent browser caching issues.

## How It Works

1. **Build Process**: When you run `npm run build` or `make css`, the build process:
   - Compiles Tailwind CSS to `main.css`
   - Generates an MD5 hash of the CSS content
   - Creates a hashed copy: `main.[hash].css` (e.g., `main.b017b445.css`)
   - Creates `css-version.properties` with the hashed filename
   - Outputs all files to `dist/` directory

2. **Java Backend**: The `CssVersionProvider` class:
   - Reads `css-version.properties` on startup
   - Provides the hashed CSS filename to templates
   - Falls back to `main.css` if version file not found (dev mode)

3. **Templates**: Use `{cssPath}` instead of hardcoded paths:
   ```html
   <link rel="stylesheet" href="{cssPath}">
   ```
   
   This automatically renders as:
   ```html
   <link rel="stylesheet" href="/dist/main.b017b445.css">
   ```

## Benefits

- **Automatic cache invalidation**: When CSS changes, the hash changes, forcing browsers to download the new version
- **No manual versioning**: Hash is computed automatically from content
- **Production-ready**: Works seamlessly in both dev and production
- **Git-friendly**: The `css-version.properties` file is tracked in git, so deployments use the correct CSS version

## Files

- `scripts/hash-css.js` - Generates CSS hash and version file
- `src/main/java/com/filesurf/util/CssVersionProvider.java` - Provides CSS version to app
- `src/main/java/com/filesurf/template/GlobalTemplateData.java` - Makes CSS path available to templates
- `src/main/resources/css-version.properties` - Generated version file (tracked in git)

## Development Workflow

### During Development (Hot Reload)
```bash
# Terminal 1: Watch CSS changes
npm run watch

# Terminal 2: Run Quarkus dev mode
mvn quarkus:dev
```
In dev mode, templates use `main.css` (no hash) for faster development.

### Building for Production
```bash
# Build CSS with cache busting
make css

# Or just:
npm run build

# Build full production JAR
make build-dist
```

## Troubleshooting

### Templates showing "main.css" instead of hashed version

Make sure you've run `npm run build` to generate the `css-version.properties` file:
```bash
npm run build
```

### CSS changes not showing in browser

1. Clear browser cache (Cmd+Shift+R or Ctrl+Shift+R)
2. Rebuild CSS: `make css`
3. Restart Quarkus if running: `mvn quarkus:dev`

### Hash not updating after CSS changes

The hash is only generated during build, not during dev watch mode. Run:
```bash
npm run build
```

## Implementation Details

### Why MD5?

We use MD5 (8 characters) for the hash because:
- It's fast to compute
- Collision risk is negligible for CSS files
- Results in short, URL-friendly filenames
- Not used for security, just cache busting

### Template Global Variables

The `@TemplateGlobal` annotation in `GlobalTemplateData` makes these available in all templates:
- `{cssPath}` - Full path: `/dist/main.[hash].css`
- `{cssFilename}` - Filename only: `main.[hash].css`
- `{cssHash}` - Hash only: `b017b445`

### Git Tracking

- ✅ **Tracked**: `css-version.properties` (needed for deployments)
- ❌ **Ignored**: `src/main/resources/META-INF/resources/dist/*` (build artifacts)

The `.gitignore` has an entry to ignore the dist folder:
```gitignore
# CSS/JS Build Output (generated)
src/main/resources/META-INF/resources/dist/
```
