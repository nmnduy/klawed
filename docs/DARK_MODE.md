# Dark Mode Implementation

## Overview
FileSurf v2 now includes a comprehensive dark mode implementation with automatic system preference detection and persistent user preference storage.

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

#### Using Dark Mode Classes in Templates
Tailwind's `dark:` variant is available for all utilities:

```html
<div class="bg-white dark:bg-slate-800 text-slate-900 dark:text-slate-100">
  Content that adapts to theme
</div>
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

## Design Guidelines

### Color Tokens
Always use design tokens instead of hard-coded colors:

✅ **Good:**
```html
<div class="bg-background text-foreground border-border">
```

❌ **Bad:**
```html
<div class="bg-white text-black border-gray-200">
```

### Testing Dark Mode
When adding new components:
1. Build CSS: `npm run build`
2. Test in both light and dark modes
3. Ensure proper contrast ratios (WCAG AA minimum)
4. Check animations and transitions work smoothly

### Common Patterns

#### Card Component
```html
<div class="bg-white dark:bg-slate-800 border border-slate-200 dark:border-slate-700 rounded-lg shadow-sm">
  <h2 class="text-slate-900 dark:text-slate-100">Title</h2>
  <p class="text-slate-600 dark:text-slate-400">Description</p>
</div>
```

#### Button Component
```html
<button class="bg-primary hover:bg-primary/90 text-primary-foreground">
  Click me
</button>
```

#### Input Component
```html
<input class="bg-white dark:bg-slate-900 border-slate-200 dark:border-slate-700 text-slate-900 dark:text-slate-100" />
```

## Browser Support
- Chrome/Edge: Full support
- Firefox: Full support
- Safari: Full support (includes `prefers-color-scheme` detection)
- Mobile browsers: Full support

## Storage
Theme preference is stored in `localStorage` with key: `filesurf-theme`
Possible values: `'light'` or `'dark'`

## Future Enhancements
- [ ] Add system theme sync option in settings
- [ ] Add theme transition animations
- [ ] Add more theme variants (high contrast, etc.)
- [ ] Add custom color scheme picker

## Troubleshooting

### Theme not persisting
Check browser localStorage permissions and ensure the domain has storage access.

### Flash of wrong theme
Verify the inline script in `<head>` is executing before CSS loads.

### Colors not updating
Ensure you're using design tokens (HSL variables) instead of hard-coded colors.

### Dark mode not activating
1. Check browser console for errors
2. Verify `darkMode.js` is loading correctly
3. Check if `.dark` class is being applied to `<html>` element
