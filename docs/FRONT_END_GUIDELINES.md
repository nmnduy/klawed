# Front-End Guidelines

## General Structure
- Keep Qute templates focused on markup and data binding; avoid business logic in templates.
- Break pages into reusable fragments/partials for repeated UI (headers, footers, lists, form fields).
- Prefer component-like structure: one template fragment + one JS module per interactive widget/area.

## Separation of Concerns
- No inline JavaScript in Qute templates; place behavior in dedicated JS files and import them in the template.
- Use data-* attributes or IDs/classes as stable hooks for JS; avoid coupling JS to presentational Tailwind classes.
- Keep template logic minimal: conditionals/loops only for rendering states, not for calculations.

## Styling

### Design Token System
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

### Custom CSS Classes Policy

**In templates**: Use Tailwind utility classes directly. Do not create new CSS component classes.

**In `index.css`**: Custom classes are permitted ONLY for:
1. **`@layer base`** - Global resets and element defaults (html, body, h1-h6, a, etc.)
2. **Stateful UI patterns** - Classes that represent dynamic states controlled by JavaScript (e.g., `.status-indicator--connected`, `.status-indicator--error`)
3. **Complex animations** - Keyframe animations and their associated classes that cannot be expressed as Tailwind utilities

**Naming conventions for permitted custom classes**:
- State classes: `[component]--[state]` (e.g., `status-indicator--working`)
- Animation classes: Descriptive kebab-case (e.g., `float-animation`, `three-dot-loader`)

**What is NOT permitted**:
- Generic component classes (`.btn`, `.card`, `.input-field`)
- Layout helper classes (`.flex-center`, `.grid-2col`)
- Styling shortcuts that duplicate Tailwind utilities

### `@apply` Usage

`@apply` is permitted in **`@layer base` only** for global element styling:
```css
/* ✅ CORRECT: Base layer element defaults */
@layer base {
  body {
    @apply bg-background text-foreground;
  }
  h1 {
    @apply text-h1-headline-xl;
  }
}

/* ❌ WRONG: Component class using @apply */
.btn-primary {
  @apply bg-primary text-white px-4 py-2 rounded-lg;
}
```

### Arbitrary Values

Arbitrary Tailwind values (e.g., `bg-[#ff0000]`, `w-[137px]`) are permitted for:
- **Gradients**: Complex gradient definitions
- **Box shadows**: Multi-layer shadows with specific values
- **Animations**: Custom timing or keyframe references
- **One-off spacing**: When design requires non-token values

**Rules**:
- If an arbitrary value is used **more than twice**, extract it to `tailwind.config.js`
- Never use arbitrary values for colors that should respond to dark mode
- Prefer CSS variables in arbitrary values: `bg-[hsl(var(--primary))]`

### Inline Styles

Inline `<style>` blocks in templates are permitted ONLY for:
- Browser-specific pseudo-element styling (e.g., scrollbar customization)
- Styles that must use CSS features not available in Tailwind

Document why the inline style is necessary with a comment.

### `!important` Usage

**Avoid `!important`**. If you find yourself needing it:
1. First, check if specificity can be resolved by reordering classes
2. If truly necessary (e.g., overriding third-party styles), document why
3. Consider if the architecture needs refactoring

## Dark Mode

We use **class-based dark mode** (`.dark` on `<html>`). See `docs/DARK_MODE.md` for full details.

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

## Data Flow and State
- Prefer server-rendered initial state from Qute; use progressive enhancement for interactions.
- Keep client state localized to the component; if shared, define a clear contract (events or a small state module).
- Derive UI from state; avoid mutating DOM in many places—update via a single render/update function per component.

## Accessibility & Semantics
- Use semantic HTML and proper labels/aria attributes, especially for form controls and custom widgets.
- Maintain keyboard support (tab order, enter/space activation) for interactive elements.
- Provide focus management after dynamic updates (e.g., focus the first invalid field on validation errors).

## Templates
- Use clear naming for fragments/partials (e.g., `file-list.html`, `file-row.html`).
- Pass only required data into partials; document expected params at the top in comments.
- Handle empty states explicitly (`@if list.isEmpty`).
- Keep error/success banners as reusable partials; standardize their structure and classes.

## JavaScript
- One module per component/area; export an `init(rootEl)` that wires events within that root.
- Use event delegation for lists/tables to avoid many listeners.
- Keep selectors near the top; avoid "magic strings"—centralize them.
- **IMPORTANT**: When selecting elements for JavaScript, use `data-*` attributes (e.g., `data-testid`, `data-role`) instead of CSS classes. Do NOT rely on Tailwind utility classes for JavaScript selectors.
- Handle failure states: show user-friendly messages; log details to console only as needed.
- Debounce/throttle rapid actions (search, typeahead) where appropriate.
- Avoid global state; if needed, namespaced objects only.

## Forms & Validation
- Validate on submit; optionally validate on blur for key fields. Show inline errors near fields plus a summary if needed.
- Preserve user input on errors; highlight invalid fields with consistent styles.
- Use native inputs where possible; for custom controls, mirror native behavior/ARIA.

## Performance
- Lazy-bind expensive handlers and avoid unnecessary reflows; batch DOM reads/writes where possible.
- Keep dependencies minimal; prefer vanilla JS.

## Testing & Robustness
- Prefer deterministic hooks for tests (`data-testid` or data-role) over styling classes.
- Handle null/undefined data in templates defensively; default to safe values.
- Log unexpected states during development; strip noisy logging before release if applicable.

## Mobile-First & Responsive
- Design for mobile first (~375–414px). Default to single-column, stacked flow with generous `px-4` gutters and vertical rhythm (`space-y-*`).
- Introduce multi-column or side panels only at `md:` and above. On small screens, stack panels or hide behind toggles.
- Keep touch targets ≥44px tall with `gap` for hit separation; ensure focus states are visible.
- Apply typography/spacing via design tokens (`text-*`, `space-*`, `rounded-*`, `shadow-*`); avoid arbitrary values unless documented.
- Manage scrolling inside panels (e.g., chat list, explorer content) rather than the page root; keep tab order linear on mobile.

## Templates for UI Shells
- Use semantic landmarks (`header`, `main`, `nav`, `section`, `form`).
- Keep repeated UI in partials (headers, footers, rows, banners, empty/error/loading blocks) under `templates/partials`.
- On interactive areas (chat, explorer, forms), prefer server-rendered initial state; JS progressively enhances via `init(rootEl)`.
- Use `data-*`/`id` hooks for behavior; do not bind JS to Tailwind classes.
- Handle empty/error/loading states explicitly (`@if list.isEmpty`).

## Checklist for New Components/Pages
- [ ] Template uses partials/fragments for repeated UI.
- [ ] No inline JS; JS module exposes `init(rootEl)`.
- [ ] Uses data-* hooks, not styling classes, for behavior.
- [ ] Tailwind classes use semantic tokens; no hardcoded colors for themed elements.
- [ ] Arbitrary values documented or extracted if used more than twice.
- [ ] No `!important` without documented justification.
- [ ] Empty/error/loading states covered.
- [ ] Accessible (semantic, labels, aria, keyboard).
- [ ] Validation UX consistent; errors surfaced inline.
- [ ] Tests target stable hooks (`data-testid`), not presentation.
- [ ] Dark mode tested and working.
