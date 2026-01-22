# FileSurf v2 Frontend Documentation

This document describes the frontend architecture, design system, CSS tokens, and development guidelines for FileSurf v2.

---

## Table of Contents

1. [Overview](#overview)
2. [Design System](#design-system)
   - [Design Tokens](#design-tokens)
   - [Component Patterns](#component-patterns)
   - [Dark Mode](#dark-mode)
3. [CSS Build Pipeline](#css-build-pipeline)
   - [Build Commands](#build-commands)
   - [File Organization](#file-organization)
   - [Dynamic CSS Classes](#dynamic-css-classes)
4. [Development Guidelines](#development-guidelines)
   - [General Structure](#general-structure)
   - [Separation of Concerns](#separation-of-concerns)
   - [Styling Guidelines](#styling-guidelines)
   - [JavaScript Guidelines](#javascript-guidelines)
   - [Forms & Validation](#forms--validation)
   - [Accessibility](#accessibility)
   - [Mobile-First & Responsive](#mobile-first--responsive)
5. [Integration with Quarkus](#integration-with-quarkus)
6. [Checklist for New Components](#checklist-for-new-components)

---

## Overview

The FileSurf v2 frontend is built using:
- **Tailwind CSS 3.4.0** - Utility-first CSS framework
- **CSS Custom Properties** - For design tokens and theming
- **Vite** - For building and bundling CSS
- **Qute Templates** - Server-side rendering
- **Vanilla JavaScript** - Component-based architecture with ES modules

### Prerequisites
- Node.js v22.15.1
- npm v10.9.2 (or bun)

---

## Design System

### Design Tokens

#### Color System

##### Monochrome (Cool Gray)
```css
cool-gray-900: #121213
cool-gray-800: #212122
cool-gray-700: #383031
cool-gray-600: #404044
cool-gray-500: #4C4C51
cool-gray-400: #757D82
cool-gray-300: #91979B
cool-gray-200: #BEC9CE
cool-gray-100: #DCE0E3
cool-gray-50:  #F6F7F8
cool-gray-10:  #FCFDFE
```

##### Brand Colors

**Navy**
- Primary brand color for professional UI elements
- Range: navy-900 to navy-10

**Blue**
- Primary action and interactive elements
- Range: blue-900 (#001B4D) to blue-10 (#F7FBFF)
- Key: blue-500 (#005EDF) - Primary action color

**Teal**
- Secondary brand color
- Range: teal-900 (#003B33) to teal-10 (#F7FFFD)

**Purple**
- Accent and visited links
- Range: purple-900 (#2D004D) to purple-10 (#FBF7FF)

**Red**
- Error states and destructive actions
- Range: red-900 (#4D0000) to red-10 (#FFF7F7)

**Yellow**
- Warning states
- Range: yellow-900 (#4D4D00) to yellow-10 (#FFFFF7)

**Green**
- Success states
- Range: green-900 (#004D00) to green-10 (#F7FFF7)

##### Semantic Colors

**Layout Colors (Light Theme)**
```css
layout-page-background: #F6F7F8
layout-main-container: #FFFFFF
layout-emphasis-high: #121213
layout-emphasis-medium: #383031
layout-emphasis-low: #4C4C51
layout-disabled: #757D82
layout-content-high: #121213
layout-content-medium: #383031
layout-content-low: #4C4C51
layout-border: #BEC9CE
layout-selected: #005EDF
```

**Interactive Colors**
```css
semantic-link-unvisited: #005EDF (blue-500)
semantic-link-hover: #004DBF (blue-600)
semantic-link-pressed: #003C9E (blue-700)
semantic-link-visited: #9500DF (purple-500)
semantic-info: #005EDF (blue-500)
semantic-success: #00DF00 (green-500)
semantic-warning: #DFDF00 (yellow-500)
semantic-error: #DF0000 (red-500)
```

#### Typography Scale

##### Display
- `display-xl`: 64px / 80px line-height / 700 weight
- `display-l`: 48px / 60px / 700
- `display-m`: 40px / 52px / 600
- `display-s`: 32px / 44px / 600

##### Headlines
- `h1-headline-xl`: 32px / 44px / 600
- `h2-headline-l`: 28px / 40px / 600
- `h3-headline-m`: 24px / 36px / 600
- `h4-headline-s`: 20px / 28px / 600
- `h5-headline-xs`: 18px / 28px / 600

##### Body
- `body-xl`: 18px / 28px / 400 (bold: 600)
- `body-l`: 16px / 24px / 400 (bold: 600)
- `body-m`: 15px / 24px / 400 (bold: 600)
- `body-s`: 14px / 20px / 400 (bold: 600)
- `body-xs`: 13px / 18px / 400 (bold: 600)

##### Caption
- `caption-m`: 12px / 16px / 400 (bold: 600)
- `caption-s`: 11px / 16px / 400 (bold: 600)

#### Spacing System

Consistent spacing scale based on 4px increments:

```css
xxs: 4px
xs:  8px
sm:  12px
md:  16px
lg:  20px
xl:  24px
2xl: 32px
3xl: 40px
4xl: 48px
5xl: 64px
6xl: 80px
7xl: 96px
8xl: 128px
```

#### Border Radius

```css
none: 0
xs:   4px
sm:   6px
md:   8px
lg:   12px
xl:   16px
2xl:  20px
3xl:  24px
full: 9999px
```

#### Shadows

```css
xs:   0px 1px 2px rgba(0, 0, 0, 0.05)
sm:   0px 1px 3px rgba(0, 0, 0, 0.1), 0px 1px 2px rgba(0, 0, 0, 0.06)
md:   0px 4px 6px -1px rgba(0, 0, 0, 0.1), 0px 2px 4px -1px rgba(0, 0, 0, 0.06)
lg:   0px 10px 15px -3px rgba(0, 0, 0, 0.1), 0px 4px 6px -2px rgba(0, 0, 0, 0.05)
xl:   0px 20px 25px -5px rgba(0, 0, 0, 0.1), 0px 10px 10px -5px rgba(0, 0, 0, 0.04)
2xl:  0px 25px 50px -12px rgba(0, 0, 0, 0.25)
inner: inset 0px 2px 4px rgba(0, 0, 0, 0.06)
```

#### Animations

Available animations:
- `animate-fade-in` - Fade in effect
- `animate-fade-out` - Fade out effect
- `animate-slide-in-from-top` - Slide in from top
- `animate-slide-in-from-bottom` - Slide in from bottom
- `animate-slide-in-from-left` - Slide in from left
- `animate-slide-in-from-right` - Slide in from right
- `animate-gradient` - Animated gradient background
- `animate-accordion-down` - Accordion expand
- `animate-accordion-up` - Accordion collapse

### Component Patterns

#### Buttons

```html
<!-- Primary Button -->
<button class="btn btn-primary btn-md">Primary</button>

<!-- Secondary Button -->
<button class="btn btn-secondary btn-md">Secondary</button>

<!-- Destructive Button -->
<button class="btn btn-destructive btn-md">Delete</button>

<!-- Ghost Button -->
<button class="btn btn-ghost btn-md">Ghost</button>

<!-- Sizes -->
<button class="btn btn-primary btn-sm">Small</button>
<button class="btn btn-primary btn-md">Medium</button>
<button class="btn btn-primary btn-lg">Large</button>
```

#### Cards

```html
<div class="card">
  <div class="card-header">
    <h3 class="card-title">Card Title</h3>
    <p class="card-description">Card description text</p>
  </div>
  <div class="card-content">
    Card content goes here
  </div>
  <div class="card-footer">
    Card footer content
  </div>
</div>
```

#### Inputs

```html
<input type="text" class="input" placeholder="Enter text...">
```

#### Badges

```html
<span class="badge badge-default">Default</span>
<span class="badge badge-secondary">Secondary</span>
<span class="badge badge-destructive">Error</span>
<span class="badge badge-outline">Outline</span>
```

#### Alerts

```html
<div class="alert alert-info">Information message</div>
<div class="alert alert-success">Success message</div>
<div class="alert alert-warning">Warning message</div>
<div class="alert alert-error">Error message</div>
```

### Dark Mode

We use **class-based dark mode** (`.dark` on `<html>`). See `docs/DARK_MODE.md` for full details.

The design system includes dark mode tokens. To enable dark mode, add the `dark` class to the `<html>` element:

```html
<html class="dark">
```

**Pattern preference order**:
1. **Semantic tokens** (auto-switch): `bg-background`, `text-foreground`, `bg-card`
2. **Layout tokens**: `bg-layout-surface`, `text-layout-content-high`
3. **Explicit dark variants**: `bg-white dark:bg-slate-900` (use sparingly, only when semantic tokens don't fit)

Never mix approaches on the same element:
```html
<!-- ❌ WRONG: Mixing token and hardcoded -->
<div class="bg-background dark:bg-slate-900">

<!-- ✅ CORRECT: Consistent approach -->
<div class="bg-background">
<div class="bg-white dark:bg-slate-900">
```

---

## CSS Build Pipeline

### Build Commands

```bash
# Build once (production - with cache busting)
npm run build
# OR
make css

# Build once (development - no cache busting, faster)
npm run build:dev
# OR
make css-dev

# Build and watch for changes
npm run dev
# OR
npm run watch
```

The compiled CSS will be output to `src/main/resources/META-INF/resources/assets/main.css` (or hashed filename in production).

### File Organization

```
src/main/resources/css/
├── index.css                    # Main entry point (imports dynamic-*.css files)
├── class-reference.html         # Class names for Tailwind scanner (required!)
├── dynamic-file-icons.css       # File icon color classes
├── dynamic-toasts.css           # Toast notification classes
├── dynamic-latex.css            # LaTeX compilation feedback classes
└── (add more dynamic-*.css files as needed)
```

**File purposes:**
- **`index.css`**: Imports dynamic CSS files, Tailwind directives, base layer, global styles
- **`class-reference.html`**: Contains all dynamic class names so Tailwind includes them
- **`dynamic-*.css`**: Modular files for classes constructed dynamically in JavaScript

**Build pipeline:**
1. `postcss-import` - resolves `@import` statements
2. `tailwindcss` - processes Tailwind directives and scans for classes
3. `autoprefixer` - adds vendor prefixes
4. `cssnano` - minifies (production only)

### Dynamic CSS Classes

**The Problem**: Tailwind's JIT compiler only includes classes it finds in scanned files. Classes constructed dynamically in JavaScript (template strings, concatenation, ternaries) won't be detected.

```javascript
// ❌ PROBLEM: Tailwind can't detect these class names
const icon = `<svg class="text-${color}-500">...</svg>`;
const toast = type === 'error' ? 'bg-red-50' : 'bg-green-50';
```

**The Solution**: Define semantic classes in `dynamic-*.css` files and reference them in `class-reference.html`.

**Adding new dynamic classes (step-by-step):**

1. **Create or edit a `dynamic-[feature].css` file:**
   ```css
   /* src/main/resources/css/dynamic-myfeature.css */
   @layer components {
     .dynamic-myfeature-active {
       @apply bg-green-500 text-white;
     }
     .dynamic-myfeature-inactive {
       @apply bg-gray-200 text-gray-600;
     }
   }
   ```

2. **Import it in `index.css`** (before `@tailwind` directives):
   ```css
   @import './dynamic-myfeature.css';
   ```

3. **Add class names to `class-reference.html`:**
   ```html
   <!-- MyFeature (dynamic-myfeature.css) -->
   <div class="dynamic-myfeature-active"></div>
   <div class="dynamic-myfeature-inactive"></div>
   ```

4. **Use in JavaScript:**
   ```javascript
   const className = isActive ? 'dynamic-myfeature-active' : 'dynamic-myfeature-inactive';
   ```

5. **Run build** - validation will catch any missing references:
   ```bash
   npm run build
   # ❌ MISSING from class-reference.html: dynamic-myfeature-active
   ```

**Why this approach?**
- **Fail-fast**: Build fails immediately if you forget to add a class reference
- **Self-documenting**: CSS files show what each class does, reference file shows what exists
- **No safelist maintenance**: Tailwind finds classes naturally by scanning the reference file
- **Semantic names**: `dynamic-icon-pdf` is clearer than `text-red-500` scattered in JS

---

## Development Guidelines

### General Structure
- Keep Qute templates focused on markup and data binding; avoid business logic in templates.
- Break pages into reusable fragments/partials for repeated UI (headers, footers, lists, form fields).
- Prefer component-like structure: one template fragment + one JS module per interactive widget/area.

### Separation of Concerns
- No inline JavaScript in Qute templates; place behavior in dedicated JS files and import them in the template.
- Use data-* attributes or IDs/classes as stable hooks for JS; avoid coupling JS to presentational Tailwind classes.
- Keep template logic minimal: conditionals/loops only for rendering states, not for calculations.

### Styling Guidelines

#### Design Token System
We use a **unified color system** based on CSS HSL variables. All colors should reference these tokens.

**Primary approach**: Use Tailwind's semantic color utilities that map to CSS variables:
```html
<!-- ✅ CORRECT: Uses semantic tokens -->
<div class="bg-background text-foreground border-border">
<div class="bg-card text-card-foreground">
<button class="bg-primary text-primary-foreground">

<!-- ❌ WRONG: Hardcoded colors bypass theming -->
<div class="bg-white text-black">
<div class="bg-slate-800 text-slate-100">
```

**For colors not in the semantic palette**, use the `layout-*` and `semantic-*` tokens defined in `tailwind.config.js`:
```html
<div class="bg-layout-surface text-layout-content-high">
<span class="text-semantic-error">
```

#### Custom CSS Classes Policy

**In templates**: Use Tailwind utility classes directly. Do not create new CSS component classes.

**In `index.css`**: Custom classes are permitted ONLY for:
1. **`@layer base`** - Global resets and element defaults (html, body, h1-h6, a, etc.)
2. **Stateful UI patterns** - Classes that represent dynamic states controlled by JavaScript (e.g., `.status-indicator--connected`, `.status-indicator--error`)
3. **Complex animations** - Keyframe animations and their associated classes that cannot be expressed as Tailwind utilities

**In `dynamic-*.css` files**: For classes that are **dynamically generated in JavaScript**:
1. **File type icons** - Classes like `.dynamic-icon-pdf`, `.dynamic-icon-text`
2. **Toast notifications** - Classes like `.dynamic-toast-success`, `.dynamic-toast-error`
3. **Any dynamically constructed class** - String concatenation, ternaries, template literals

**Naming conventions for permitted custom classes**:
- State classes: `[component]--[state]` (e.g., `status-indicator--working`)
- Animation classes: Descriptive kebab-case (e.g., `float-animation`, `three-dot-loader`)
- Dynamic classes: `dynamic-[category]-[name]` (e.g., `dynamic-icon-pdf`, `dynamic-toast-success`)

**What is NOT permitted**:
- Generic component classes (`.btn`, `.card`, `.input-field`) - these are already provided by the design system
- Layout helper classes (`.flex-center`, `.grid-2col`)
- Styling shortcuts that duplicate Tailwind utilities

#### `@apply` Usage

`@apply` is permitted in:
1. **`@layer base`** - for global element styling in `index.css`
2. **`@layer components`** - for dynamic classes in `dynamic-*.css` files

```css
/* ✅ CORRECT: Base layer element defaults (index.css) */
@layer base {
  body {
    @apply bg-background text-foreground;
  }
  h1 {
    @apply text-h1-headline-xl;
  }
}

/* ✅ CORRECT: Dynamic classes (dynamic-*.css) */
@layer components {
  .dynamic-icon-pdf {
    @apply text-red-500;
  }
}

/* ❌ WRONG: Component class outside of dynamic-* pattern */
.btn-primary {
  @apply bg-primary text-white px-4 py-2 rounded-lg;
}
```

#### Arbitrary Values

Arbitrary Tailwind values (e.g., `bg-[#ff0000]`, `w-[137px]`) are permitted for:
- **Gradients**: Complex gradient definitions
- **Box shadows**: Multi-layer shadows with specific values
- **Animations**: Custom timing or keyframe references
- **One-off spacing**: When design requires non-token values

**Rules**:
- If an arbitrary value is used **more than twice**, extract it to `tailwind.config.js`
- Never use arbitrary values for colors that should respond to dark mode
- Prefer CSS variables in arbitrary values: `bg-[hsl(var(--primary))]`

#### Inline Styles

Inline `<style>` blocks in templates are permitted ONLY for:
- Browser-specific pseudo-element styling (e.g., scrollbar customization)
- Styles that must use CSS features not available in Tailwind

Document why the inline style is necessary with a comment.

#### `!important` Usage

**Avoid `!important`**. If you find yourself needing it:
1. First, check if specificity can be resolved by reordering classes
2. If truly necessary (e.g., overriding third-party styles), document why
3. Consider if the architecture needs refactoring

### JavaScript Guidelines

See `docs/JS_CONVENTIONS.md` for full details. Key points:

- One module per component/area; export an `init(rootEl)` that wires events within that root.
- Use event delegation for lists/tables to avoid many listeners.
- Keep selectors near the top; avoid "magic strings"—centralize them.
- **IMPORTANT**: When selecting elements for JavaScript, use `data-*` attributes (e.g., `data-testid`, `data-role`) instead of CSS classes. Do NOT rely on Tailwind utility classes for JavaScript selectors.
- Handle failure states: show user-friendly messages; log details to console only as needed.
- Debounce/throttle rapid actions (search, typeahead) where appropriate.
- Avoid global state; if needed, namespaced objects only.

### Forms & Validation
- Validate on submit; optionally validate on blur for key fields. Show inline errors near fields plus a summary if needed.
- Preserve user input on errors; highlight invalid fields with consistent styles.
- Use native inputs where possible; for custom controls, mirror native behavior/ARIA.

### Accessibility
- Use semantic HTML and proper labels/aria attributes, especially for form controls and custom widgets.
- Maintain keyboard support (tab order, enter/space activation) for interactive elements.
- Provide focus management after dynamic updates (e.g., focus the first invalid field on validation errors).

### Mobile-First & Responsive
- Design for mobile first (~375–414px). Default to single-column, stacked flow with generous `px-4` gutters and vertical rhythm (`space-y-*`).
- Introduce multi-column or side panels only at `md:` and above. On small screens, stack panels or hide behind toggles.
- Keep touch targets ≥44px tall with `gap` for hit separation; ensure focus states are visible.
- **Mobile text inputs must be at least 16px font size** to prevent iOS Safari from zooming in when the input is focused.
- Apply typography/spacing via design tokens (`text-*`, `space-*`, `rounded-*`, `shadow-*`); avoid arbitrary values unless documented.
- Manage scrolling inside panels (e.g., chat list, explorer content) rather than the page root; keep tab order linear on mobile.

### Data Flow and State
- Prefer server-rendered initial state from Qute; use progressive enhancement for interactions.
- Keep client state localized to the component; if shared, define a clear contract (events or a small state module).
- Derive UI from state; avoid mutating DOM in many places—update via a single render/update function per component.

### Performance
- Lazy-bind expensive handlers and avoid unnecessary reflows; batch DOM reads/writes where possible.
- Keep dependencies minimal; prefer vanilla JS.

### Testing & Robustness
- Prefer deterministic hooks for tests (`data-testid` or data-role) over styling classes.
- Handle null/undefined data in templates defensively; default to safe values.
- Log unexpected states during development; strip noisy logging before release if applicable.

### Templates for UI Shells
- Use semantic landmarks (`header`, `main`, `nav`, `section`, `form`).
- Keep repeated UI in partials (headers, footers, rows, banners, empty/error/loading blocks) under `templates/partials`.
- On interactive areas (chat, explorer, forms), prefer server-rendered initial state; JS progressively enhances via `init(rootEl)`.
- Use `data-*`/`id` hooks for behavior; do not bind JS to Tailwind classes.
- Handle empty/error/loading states explicitly (`@if list.isEmpty`).

---

## Integration with Quarkus

The CSS build process integrates seamlessly with Quarkus:

1. CSS is compiled to `META-INF/resources/assets/main.css` (or hashed filename)
2. Quarkus automatically serves files from `META-INF/resources/`
3. Reference in templates: `<link rel="stylesheet" href="{cssPath}">`

### Usage in Qute Templates

```html
<!-- Include CSS (uses {cssPath} for cache busting) -->
<link rel="stylesheet" href="{cssPath}">

<!-- Using Tailwind Classes -->
<div class="bg-layout-page-background">
  <h1 class="text-h1-headline-xl text-blue-500">Hello World</h1>
  <p class="text-body-m text-layout-content-medium">
    Body text with medium emphasis
  </p>
  <button class="btn btn-primary btn-md">Click Me</button>
</div>

<!-- Using Component Classes -->
<div class="card">
  <div class="card-header">
    <h2 class="card-title">File #12345</h2>
  </div>
  <div class="card-content">
    <p class="text-body-m">File details...</p>
  </div>
</div>
```

### Maven Integration

To build CSS as part of Maven build, add to `pom.xml`:

```xml
<plugin>
  <groupId>com.github.eirslett</groupId>
  <artifactId>frontend-maven-plugin</artifactId>
  <version>1.12.1</version>
  <executions>
    <execution>
      <id>npm install</id>
      <goals>
        <goal>npm</goal>
      </goals>
    </execution>
    <execution>
      <id>npm build</id>
      <goals>
        <goal>npm</goal>
      </goals>
      <configuration>
        <arguments>run build</arguments>
      </configuration>
    </execution>
  </executions>
</plugin>
```

---

## Checklist for New Components

- [ ] Template uses partials/fragments for repeated UI.
- [ ] No inline JS; JS module exposes `init(rootEl)`.
- [ ] Uses data-* hooks, not styling classes, for behavior.
- [ ] Tailwind classes use semantic tokens; no hardcoded colors for themed elements.
- [ ] **Dynamic classes added to `dynamic-*.css` AND `class-reference.html`.**
- [ ] Arbitrary values documented or extracted if used more than twice.
- [ ] No `!important` without documented justification.
- [ ] Empty/error/loading states covered.
- [ ] Accessible (semantic, labels, aria, keyboard).
- [ ] Validation UX consistent; errors surfaced inline.
- [ ] Tests target stable hooks (`data-testid`), not presentation.
- [ ] Dark mode tested and working.
- [ ] `npm run build` passes without errors.

---

## CSS Variables

All design tokens are available as CSS custom properties:

```css
:root {
  --spacing-xs: 8px;
  --font-family-sans: system-ui, sans-serif;
  --transition-base: 200ms;
  --z-modal: 1050;
  /* ... and many more */
}
```

---

## Best Practices

1. **Use semantic tokens** - Prefer `bg-layout-page-background` over `bg-gray-50`
2. **Use component classes** - Use `.btn` classes instead of composing utilities
3. **Stay consistent** - Use the spacing scale for all margins and paddings
4. **Typography hierarchy** - Use the typography scale for consistent text sizing
5. **Color meaning** - Use semantic colors for their intended purpose (error = red, success = green)
6. **Dark mode first** - Use semantic tokens that automatically adapt to dark mode
7. **Mobile first** - Design for small screens first, enhance for larger viewports
8. **Accessibility** - Test with keyboard navigation and screen readers
9. **Performance** - Keep JavaScript minimal and bundle sizes small
10. **Documentation** - Document complex patterns and edge cases

---

## Customization

To customize the design system:

1. Edit `tailwind.config.js` to modify or add design tokens
2. Edit `src/main/resources/css/index.css` to add custom CSS
3. Run `npm run build` to rebuild the CSS
4. Refresh your browser to see changes

For development with hot reload:
```bash
npm run watch
```

---

## File Structure

```
css/
├── src/main/resources/
│   ├── css/
│   │   ├── index.css          # Main CSS file with design tokens
│   │   ├── class-reference.html # Tailwind class scanner reference
│   │   ├── dynamic-*.css      # Dynamic class definitions
│   │   └── main.html          # Build entry point
│   └── META-INF/resources/
│       └── assets/
│           └── main.css       # Compiled CSS output
├── package.json               # npm dependencies
├── postcss.config.js          # PostCSS configuration
├── tailwind.config.js         # Tailwind + design tokens config
└── vite.config.js             # Vite build configuration
```

---

## Related Documentation

- `docs/DARK_MODE.md` - Dark mode implementation details
- `docs/CSS_CACHE_BUSTING.md` - CSS cache busting and build process
- `docs/JS_CONVENTIONS.md` - JavaScript coding conventions
- `docs/JS_CACHE_BUSTING.md` - JavaScript cache busting
- `docs/JS_VALIDATION.md` - JavaScript validation
- `docs/LUCIDE_ICONS.md` - Icon system documentation
