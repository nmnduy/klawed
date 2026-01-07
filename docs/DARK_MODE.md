# Dark Mode Implementation

## Overview
FileSurf v2 uses a comprehensive dark mode implementation with automatic system preference detection and persistent user preference storage.

## Features
- ✅ Toggle button in the header (sun/moon icons)
- ✅ Automatic system theme detection (respects `prefers-color-scheme`)
- ✅ Persistent theme preference (localStorage)
- ✅ No flash of incorrect theme on page load
- ✅ Smooth transitions between themes
- ✅ Full coverage across all pages (chat, login, file explorer)
- ✅ Proper dark mode variants for all UI components

## Architecture

### 1. Design Tokens (`src/main/resources/css/index.css`)
The application uses HSL color tokens for dynamic theming:

```css
:root {
  /* Light theme tokens */
  --background: 222 47% 98%;
  --foreground: 222 47% 11%;
  --primary: 25 95% 53%;
  /* ... other tokens */
}

.dark {
  /* Dark theme tokens */
  --background: 222 84% 5%;
  --foreground: 222 47% 98%;
  --primary: 25 95% 62%;
  /* ... other tokens */
}
```

### 2. Tailwind Configuration (`tailwind.config.js`)
Dark mode is configured to use class-based targeting:

```javascript
export default {
  darkMode: ["class"],
  // ... rest of config
}
```

This allows us to toggle dark mode by adding/removing the `.dark` class on the `<html>` element.

### 3. Dark Mode Manager (`src/main/resources/META-INF/resources/js/darkMode.js`)
A singleton utility that handles:
- Reading theme from localStorage
- Detecting system preferences
- Applying theme to DOM
- Persisting user selection
- Listening to system theme changes

**Key methods:**
- `darkMode.toggle()` - Toggle between light/dark
- `darkMode.setTheme(theme)` - Set specific theme ('light' or 'dark')
- `darkMode.getTheme()` - Get current theme
- `darkMode.isDark()` - Check if dark mode is active
- `darkMode.addListener(callback)` - Listen for theme changes

### 4. Inline Theme Initialization
Both `fileChat.html` and `login.html` include an inline script in the `<head>` that:
1. Reads theme from localStorage
2. Falls back to system preference
3. Immediately applies the `.dark` class if needed

This prevents the "flash of incorrect theme" (FOIT) issue.

### 5. UI Toggle Button
The header includes a beautiful animated toggle button with:
- Sun icon (visible in dark mode)
- Moon icon (visible in light mode)
- Smooth rotation animations on hover
- Accessible ARIA labels

## Usage

### For Users
1. Click the moon/sun icon in the header to toggle dark mode
2. Your preference is automatically saved
3. The app will remember your choice on future visits

### For Developers

#### Color System Hierarchy

We use a **unified color system** with clear precedence:

| Approach | When to Use | Example |
|----------|-------------|---------|
| **Semantic tokens** | Default choice for all themed elements | `bg-background`, `text-foreground`, `bg-card` |
| **Layout tokens** | Semantic colors for specific UI contexts | `bg-layout-surface`, `text-layout-content-high` |
| **Explicit dark variants** | Only when semantic tokens don't provide the needed contrast | `bg-white dark:bg-slate-900` |

#### ✅ Correct Patterns

**Using semantic tokens (preferred)**:
```html
<!-- Background and text automatically adapt to theme -->
<div class="bg-background text-foreground">
  Content that adapts to theme
</div>

<!-- Card surfaces -->
<div class="bg-card text-card-foreground border-border rounded-lg">
  Card content
</div>

<!-- Primary actions -->
<button class="bg-primary text-primary-foreground hover:bg-primary/90">
  Click me
</button>

<!-- Muted/secondary content -->
<p class="text-muted-foreground">Secondary text</p>
```

**Using layout tokens**:
```html
<div class="bg-layout-surface text-layout-content-high">
  <span class="text-layout-content-medium">Subtitle</span>
</div>
```

**Using explicit dark variants (when necessary)**:
```html
<!-- When you need specific color control beyond tokens -->
<div class="bg-slate-50 dark:bg-slate-800 border-slate-200 dark:border-slate-700">
  <h2 class="text-slate-900 dark:text-slate-100">Title</h2>
  <p class="text-slate-600 dark:text-slate-400">Description</p>
</div>
```

#### ❌ Incorrect Patterns

```html
<!-- WRONG: Hardcoded colors that don't respond to theme -->
<div class="bg-white text-black">

<!-- WRONG: Mixing semantic tokens with explicit overrides -->
<div class="bg-background dark:bg-slate-900">

<!-- WRONG: Using arbitrary color values for themed elements -->
<div class="bg-[#ffffff] dark:bg-[#1e293b]">
```

#### Listening to Theme Changes
```javascript
import { darkMode } from './darkMode.js';

darkMode.addListener((theme) => {
  console.log('Theme changed to:', theme);
  // Update any theme-dependent logic
});
```

#### Programmatically Changing Theme
```javascript
import { darkMode } from './darkMode.js';

// Toggle
darkMode.toggle();

// Set specific theme
darkMode.setTheme('dark');
darkMode.setTheme('light');

// Check current theme
if (darkMode.isDark()) {
  // Dark mode is active
}
```

## Blessed Patterns

Copy-paste these patterns for consistent UI components.

### Card Component
```html
<div class="bg-card text-card-foreground border border-border rounded-lg shadow-sm p-4">
  <h2 class="text-h4-headline-s mb-2">Card Title</h2>
  <p class="text-muted-foreground">Card description goes here.</p>
</div>
```

### Primary Button
```html
<button class="inline-flex items-center justify-center rounded-lg bg-primary text-primary-foreground hover:bg-primary/90 h-10 px-4 py-2 text-body-s-bold transition-colors focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-ring focus-visible:ring-offset-2 disabled:pointer-events-none disabled:opacity-50">
  Button Text
</button>
```

### Secondary/Outline Button
```html
<button class="inline-flex items-center justify-center rounded-lg border-2 border-border bg-background text-foreground hover:bg-muted h-10 px-4 py-2 text-body-s-bold transition-colors focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-ring focus-visible:ring-offset-2 disabled:pointer-events-none disabled:opacity-50">
  Button Text
</button>
```

### Text Input
```html
<input 
  type="text"
  class="flex h-10 w-full rounded-lg border border-input bg-background text-foreground px-3 py-2 text-body-m placeholder:text-muted-foreground focus-visible:outline-none focus-visible:border-primary focus-visible:ring-2 focus-visible:ring-primary/20 disabled:cursor-not-allowed disabled:opacity-50"
  placeholder="Enter text..."
/>
```

### Alert/Banner - Info
```html
<div class="rounded-lg border border-semantic-info/30 bg-semantic-info-bg p-4">
  <p class="text-body-s text-semantic-info">Information message here.</p>
</div>
```

### Alert/Banner - Error
```html
<div class="rounded-lg border border-semantic-error/30 bg-semantic-error-bg p-4">
  <p class="text-body-s text-semantic-error">Error message here.</p>
</div>
```

### Empty State
```html
<div class="flex flex-col items-center justify-center py-12 px-4 text-center">
  <svg class="w-12 h-12 text-muted-foreground/50 mb-4" fill="none" stroke="currentColor" viewBox="0 0 24 24">
    <!-- icon path -->
  </svg>
  <h3 class="text-h5-headline-xs text-foreground mb-1">No items found</h3>
  <p class="text-body-s text-muted-foreground">Try adjusting your search or filters.</p>
</div>
```

### Loading State
```html
<div class="flex items-center justify-center py-8">
  <div class="inline-flex items-center gap-2 px-4 py-2 bg-muted rounded-lg">
    <svg class="w-4 h-4 animate-spin text-muted-foreground" fill="none" stroke="currentColor" viewBox="0 0 24 24">
      <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M4 4v5h.582m15.356 2A8.001 8.001 0 004.582 9m0 0H9m11 11v-5h-.581m0 0a8.003 8.003 0 01-15.357-2m15.357 2H15" />
    </svg>
    <span class="text-body-s text-muted-foreground">Loading...</span>
  </div>
</div>
```

## Browser Support
- Chrome/Edge: Full support
- Firefox: Full support
- Safari: Full support (includes `prefers-color-scheme` detection)
- Mobile browsers: Full support

## Storage
Theme preference is stored in `localStorage` with key: `filesurf-theme`
Possible values: `'light'` or `'dark'`

## Troubleshooting

### Theme not persisting
Check browser localStorage permissions and ensure the domain has storage access.

### Flash of wrong theme
Verify the inline script in `<head>` is executing before CSS loads.

### Colors not updating
Ensure you're using semantic tokens (CSS variables) instead of hardcoded colors.

### Dark mode not activating
1. Check browser console for errors
2. Verify `darkMode.js` is loading correctly
3. Check if `.dark` class is being applied to `<html>` element

### Inconsistent colors between light/dark
Check if you're mixing color approaches. Use semantic tokens consistently, or use explicit `dark:` variants consistently—don't mix both on the same element.
