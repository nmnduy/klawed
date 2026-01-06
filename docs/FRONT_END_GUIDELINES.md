**General structure**
- Keep Qute templates focused on markup and data binding; avoid business logic in templates.
- Break pages into reusable fragments/partials for repeated UI (headers, footers, lists, form fields).
- Prefer component-like structure: one template fragment + one JS module per interactive widget/area.

**Separation of concerns**
- No inline JavaScript in Qute templates; place behavior in dedicated JS files and import them in the template.
- Use data-* attributes or IDs/classes as stable hooks for JS; avoid coupling JS to presentational Tailwind classes.
- Keep template logic minimal: conditionals/loops only for rendering states, not for calculations.

**Styling**
- Use Tailwind with project design tokens (see `DESIGN_SYSTEM.md` and `src/main/resources/css/index.css`).
- **PROHIBITED**: No custom CSS classes in markup or CSS. Do not introduce or reuse helper classes (e.g., `.btn`, `.card`, `.chat-*`). Use Tailwind utility classes directly in templates.
- **PROHIBITED**: Do NOT use `@apply` in CSS to create component classes. If a new pattern is needed, compose it with Tailwind utilities in the template or extend the Tailwind config (tokens, variants), not via custom classes.
- Legacy classes that still exist are **deprecated**. When touching a view, replace them with Tailwind utility stacks and remove the custom class usage.
- Keep spacing/typography consistent with the design tokens; avoid ad-hoc arbitrary values unless documented.
- For complex patterns, use Tailwind's arbitrary values or extend the Tailwind config instead of creating custom classes.

**Data flow and state**
- Prefer server-rendered initial state from Qute; use progressive enhancement for interactions.
- Keep client state localized to the component; if shared, define a clear contract (events or a small state module).
- Derive UI from state; avoid mutating DOM in many places—update via a single render/update function per component.

**Accessibility & semantics**
- Use semantic HTML and proper labels/aria attributes, especially for form controls and custom widgets.
- Maintain keyboard support (tab order, enter/space activation) for interactive elements.
- Provide focus management after dynamic updates (e.g., focus the first invalid field on validation errors).

**Templates**
- Use clear naming for fragments/partials (e.g., `file-list.html`, `file-row.html`).
- Pass only required data into partials; document expected params at the top in comments.
- Handle empty states explicitly (`@if list.isEmpty`).
- Keep error/success banners as reusable partials; standardize their structure and classes.

**JavaScript**
- One module per component/area; export an `init(rootEl)` that wires events within that root.
- Use event delegation for lists/tables to avoid many listeners.
- Keep selectors near the top; avoid "magic strings"—centralize them.
- **IMPORTANT**: When selecting elements for JavaScript, use `data-*` attributes (e.g., `data-testid`, `data-role`) instead of CSS classes. Do NOT rely on Tailwind utility classes for JavaScript selectors.
- Handle failure states: show user-friendly messages; log details to console only as needed.
- Debounce/throttle rapid actions (search, typeahead) where appropriate.
- Avoid global state; if needed, namespaced objects only.

**Forms & validation**
- Validate on submit; optionally validate on blur for key fields. Show inline errors near fields plus a summary if needed.
- Preserve user input on errors; highlight invalid fields with consistent styles.
- Use native inputs where possible; for custom controls, mirror native behavior/ARIA.

**Performance**
- Lazy-bind expensive handlers and avoid unnecessary reflows; batch DOM reads/writes where possible.
- Keep dependencies minimal; prefer vanilla JS.

**Testing & robustness**
- Prefer deterministic hooks for tests (`data-testid` or data-role) over styling classes.
- Handle null/undefined data in templates defensively; default to safe values.
- Log unexpected states during development; strip noisy logging before release if applicable.

**Mobile-first & responsive**
- Design for mobile first (~375–414px). Default to single-column, stacked flow with generous `px-4` gutters and vertical rhythm (`space-y-*`).
- Introduce multi-column or side panels only at `md:` and above. On small screens, stack panels or hide behind toggles.
- Keep touch targets ≥44px tall with `gap` for hit separation; ensure focus states are visible.
- Apply typography/spacing via design tokens (`text-*`, `space-*`, `rounded-*`, `shadow-*`); avoid arbitrary values unless documented.
- Manage scrolling inside panels (e.g., chat list, explorer content) rather than the page root; keep tab order linear on mobile.

**Templates for UI shells**
- Use semantic landmarks (`header`, `main`, `nav`, `section`, `form`).
- Keep repeated UI in partials (headers, footers, rows, banners, empty/error/loading blocks) under `templates/partials`.
- On interactive areas (chat, explorer, forms), prefer server-rendered initial state; JS progressively enhances via `init(rootEl)`.
- Use `data-*`/`id` hooks for behavior; do not bind JS to Tailwind classes.
- Handle empty/error/loading states explicitly (`@if list.isEmpty`).

**Checklist for new components/pages**
- [ ] Template uses partials/fragments for repeated UI.
- [ ] No inline JS; JS module exposes `init(rootEl)`.
- [ ] Uses data-* hooks, not styling classes, for behavior.
- [ ] Tailwind classes follow design tokens; shared patterns extracted.
- [ ] Empty/error/loading states covered.
- [ ] Accessible (semantic, labels, aria, keyboard).
- [ ] Validation UX consistent; errors surfaced inline.
- [ ] Tests target stable hooks (`data-testid`), not presentation.