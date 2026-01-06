# FileSurf v2 Design System

This document describes the design system and CSS tokens used in the FileSurf v2 application.

## Overview

The design system is built using:
- **Tailwind CSS 3.4.0** - Utility-first CSS framework
- **CSS Custom Properties** - For design tokens and theming
- **Vite** - For building and bundling CSS

## Setup

### Prerequisites
- Node.js v22.15.1
- npm v10.9.2

### Installation
```bash
npm install
```

### Build CSS
```bash
npm run build        # Build once
npm run watch        # Build and watch for changes
```

The compiled CSS will be output to `src/main/resources/META-INF/resources/assets/main.css`.

## Design Tokens

### Color System

#### Monochrome (Cool Gray)
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

#### Brand Colors

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

#### Semantic Colors

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

### Typography Scale

#### Display
- `display-xl`: 64px / 80px line-height / 700 weight
- `display-l`: 48px / 60px / 700
- `display-m`: 40px / 52px / 600
- `display-s`: 32px / 44px / 600

#### Headlines
- `h1-headline-xl`: 32px / 44px / 600
- `h2-headline-l`: 28px / 40px / 600
- `h3-headline-m`: 24px / 36px / 600
- `h4-headline-s`: 20px / 28px / 600
- `h5-headline-xs`: 18px / 28px / 600

#### Body
- `body-xl`: 18px / 28px / 400 (bold: 600)
- `body-l`: 16px / 24px / 400 (bold: 600)
- `body-m`: 15px / 24px / 400 (bold: 600)
- `body-s`: 14px / 20px / 400 (bold: 600)
- `body-xs`: 13px / 18px / 400 (bold: 600)

#### Caption
- `caption-m`: 12px / 16px / 400 (bold: 600)
- `caption-s`: 11px / 16px / 400 (bold: 600)

### Spacing System

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

### Border Radius

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

### Shadows

```css
xs:   0px 1px 2px rgba(0, 0, 0, 0.05)
sm:   0px 1px 3px rgba(0, 0, 0, 0.1), 0px 1px 2px rgba(0, 0, 0, 0.06)
md:   0px 4px 6px -1px rgba(0, 0, 0, 0.1), 0px 2px 4px -1px rgba(0, 0, 0, 0.06)
lg:   0px 10px 15px -3px rgba(0, 0, 0, 0.1), 0px 4px 6px -2px rgba(0, 0, 0, 0.05)
xl:   0px 20px 25px -5px rgba(0, 0, 0, 0.1), 0px 10px 10px -5px rgba(0, 0, 0, 0.04)
2xl:  0px 25px 50px -12px rgba(0, 0, 0, 0.25)
inner: inset 0px 2px 4px rgba(0, 0, 0, 0.06)
```

## Component Patterns

### Buttons

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

### Cards

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

### Inputs

```html
<input type="text" class="input" placeholder="Enter text...">
```

### Badges

```html
<span class="badge badge-default">Default</span>
<span class="badge badge-secondary">Secondary</span>
<span class="badge badge-destructive">Error</span>
<span class="badge badge-outline">Outline</span>
```

### Alerts

```html
<div class="alert alert-info">Information message</div>
<div class="alert alert-success">Success message</div>
<div class="alert alert-warning">Warning message</div>
<div class="alert alert-error">Error message</div>
```

## Usage in Qute Templates

### Include CSS
```html
<link rel="stylesheet" href="/assets/main.css">
```

### Using Tailwind Classes
```html
<div class="bg-layout-page-background">
  <h1 class="text-h1-headline-xl text-blue-500">Hello World</h1>
  <p class="text-body-m text-layout-content-medium">
    Body text with medium emphasis
  </p>
  <button class="btn btn-primary btn-md">Click Me</button>
</div>
```

### Using Component Classes
```html
<div class="card">
  <div class="card-header">
    <h2 class="card-title">File #12345</h2>
  </div>
  <div class="card-content">
    <p class="text-body-m">File details...</p>
  </div>
</div>
```

## Dark Mode Support

The design system includes dark mode tokens. To enable dark mode, add the `dark` class to the `<html>` element:

```html
<html class="dark">
```

## Animations

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

## Best Practices

1. **Use semantic tokens** - Prefer `bg-layout-page-background` over `bg-gray-50`
2. **Use component classes** - Use `.btn` classes instead of composing utilities
3. **Stay consistent** - Use the spacing scale for all margins and paddings
4. **Typography hierarchy** - Use the typography scale for consistent text sizing
5. **Color meaning** - Use semantic colors for their intended purpose (error = red, success = green)

## File Structure

```
css/
├── src/main/resources/
│   ├── css/
│   │   ├── index.css          # Main CSS file with design tokens
│   │   └── main.html          # Build entry point
│   └── META-INF/resources/
│       └── assets/
│           └── main.css       # Compiled CSS output
├── package.json               # npm dependencies
├── postcss.config.js          # PostCSS configuration
├── tailwind.config.js         # Tailwind + design tokens config
└── vite.config.js             # Vite build configuration
```

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

## Integration with Quarkus

The CSS build process integrates seamlessly with Quarkus:

1. CSS is compiled to `META-INF/resources/assets/main.css`
2. Quarkus automatically serves files from `META-INF/resources/`
3. Reference in templates: `<link rel="stylesheet" href="/assets/main.css">`

## Maven Integration

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