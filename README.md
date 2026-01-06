# FileSurf v2

File Management and Chat System built with Quarkus and Qute templates.

## Tech Stack

- **Backend**: JDK 21 + Quarkus 3.16.4
- **Templates**: Qute Template Engine
- **CSS**: Tailwind CSS 3.4.0 with Design System
- **Build Tools**: Maven + Vite
- **Node**: v22.15.1
- **npm**: v10.9.2

## Quick Start

### Using Make (Recommended)

```bash
# First time setup - install dependencies and build CSS
make setup

# Start development (automatically builds CSS if needed)
make dev

# In another terminal, watch CSS changes (optional)
make watch
```

### Manual Setup

#### Backend (Quarkus)

```bash
# Development mode (hot reload)
./mvnw quarkus:dev

# Package application
./mvnw package

# Run packaged application
java -jar target/quarkus-app/quarkus-run.jar
```

#### Frontend (CSS)

```bash
# Install dependencies
npm install

# Build CSS once
npm run build

# Watch mode (auto-rebuild on changes)
npm run watch
```

**Important**: After building CSS for the first time while Quarkus is running, press `s` in the Quarkus console to trigger a reload and load the new assets.

## Project Structure

```
css/
├── src/
│   ├── main/
│   │   ├── java/
│   │   │   └── com/filesurf/
│   │   │       └── FileResource.java
│   │   └── resources/
│   │       ├── css/
│   │       │   └── index.css              # Source CSS with Tailwind
│   │       ├── templates/
│   │       │   └── fileChat.html          # Qute templates
│   │       └── META-INF/resources/
│   │           └── assets/
│   │               └── main.css           # Compiled CSS (auto-generated)
│   └── test/
├── pom.xml                                 # Maven configuration
├── package.json                            # npm configuration
├── tailwind.config.js                      # Tailwind + design tokens
├── vite.config.js                          # Vite build config
└── DESIGN_SYSTEM.md                        # Design system documentation
```

## Development Workflow

1. **Start Quarkus in dev mode**:
   ```bash
   ./mvnw quarkus:dev
   ```

2. **In another terminal, watch CSS changes**:
   ```bash
   npm run watch
   ```

3. **Make changes**:
   - Edit Qute templates in `src/main/resources/templates/`
   - Edit CSS in `src/main/resources/css/index.css`
   - Edit design tokens in `tailwind.config.js`

4. **See changes**:
   - Quarkus auto-reloads Java and template changes
   - Vite auto-rebuilds CSS on file changes
   - Refresh browser to see updates

## Design System

See [DESIGN_SYSTEM.md](docs/DESIGN_SYSTEM.md) for complete documentation.

### Quick Examples

#### Using Design Tokens
```html
<div class="bg-layout-page-background p-lg">
  <h1 class="text-h1-headline-xl text-blue-500">Hello World</h1>
  <p class="text-body-m text-layout-content-medium">Body text</p>
</div>
```

#### Using Components
```html
<button class="btn btn-primary btn-md">Primary Button</button>

<div class="card">
  <div class="card-header">
    <h3 class="card-title">Card Title</h3>
  </div>
  <div class="card-content">Content</div>
</div>

<div class="alert alert-success">Success message</div>
```

## Available Scripts

### Make Commands
- `make help` - Show all available commands
- `make setup` - Install dependencies and build CSS (first time)
- `make dev` - Start Quarkus in dev mode (builds CSS if needed)
- `make watch` - Watch and rebuild CSS on changes
- `make dev-parallel` - Start both Quarkus and CSS watch together
- `make build` - Build CSS for production
- `make clean` - Clean all build artifacts
- `make test` - Run tests
- `make package` - Build CSS and package application

### Maven
- `./mvnw quarkus:dev` - Run in development mode
- `./mvnw package` - Package application
- `./mvnw test` - Run tests
- `./mvnw clean` - Clean build artifacts

### npm
- `npm install` - Install dependencies
- `npm run build` - Build CSS for production
- `npm run watch` - Build CSS and watch for changes
- `npm run dev` - Start Vite dev server (alternative)

## API Endpoints

- `GET /file-chat` - Main file chat page

## Configuration

### Application Properties
Edit `src/main/resources/application.properties`:

```properties
quarkus.http.port=8080
quarkus.qute.content-types.html=text/html
```

### Design Tokens
Edit `tailwind.config.js` to customize:
- Colors
- Typography
- Spacing
- Borders
- Shadows
- Animations

## Building for Production

```bash
# Build CSS
npm run build

# Package application
./mvnw package -Pnative  # For native image
# or
./mvnw package           # For JVM

# Run
java -jar target/quarkus-app/quarkus-run.jar
```

## Git Commit Messages

Follow project conventions from KLAWED.md:
- One line, all lowercase
- Keep under 20 words if body is needed

Examples:
```
add tailwind css with design tokens

update file template with new design system

fix button styles and spacing
```

## Testing

```bash
# Run all tests
./mvnw test

# Run specific test
./mvnw test -Dtest=FileResourceTest
```

## Documentation

- [DESIGN_SYSTEM.md](docs/DESIGN_SYSTEM.md) - Complete design system documentation
- [FRONT_END_GUIDELINES.md](docs/FRONT_END_GUIDELINES.md) - Frontend development guidelines
- [KLAWED.md](KLAWED.md) - Project conventions and guidelines

## Support

For issues or questions:
1. Check the design system documentation
2. Review Tailwind CSS documentation: https://tailwindcss.com
3. Review Quarkus Qute documentation: https://quarkus.io/guides/qute
