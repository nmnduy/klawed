#!/usr/bin/env node

/**
 * JS Hash Generator
 *
 * This script generates content-based hashes for all JS files and:
 * 1. Copies each JS file to [name].[hash].js
 * 2. Creates a js-version.properties file with all hashed filenames
 * 3. This allows templates to reference the hashed JS files for cache busting
 */

import { createHash } from 'crypto';
import { readFileSync, writeFileSync, copyFileSync, existsSync, mkdirSync, readdirSync } from 'fs';
import { join, dirname, basename, extname } from 'path';
import { fileURLToPath } from 'url';

const __filename = fileURLToPath(import.meta.url);
const __dirname = dirname(__filename);
const projectRoot = join(__dirname, '..');

const JS_SOURCE_DIR = join(projectRoot, 'src/main/resources/META-INF/resources/js');
const JS_DIST_DIR = join(projectRoot, 'src/main/resources/META-INF/resources/dist');
const VERSION_FILE = join(projectRoot, 'src/main/resources/js-version.properties');

function generateHash(filePath) {
    const content = readFileSync(filePath);
    const hash = createHash('md5').update(content).digest('hex');
    return hash.substring(0, 8); // Use first 8 characters
}

function main() {
    // Skip hashing in development mode
    const isDev = process.env.NODE_ENV !== 'production';

    if (isDev) {
        console.log('🔧 Development mode: Skipping JS hashing');

        // Ensure dist directory exists
        if (!existsSync(JS_DIST_DIR)) {
            mkdirSync(JS_DIST_DIR, { recursive: true });
        }

        // Get all JS files in dev mode
        if (!existsSync(JS_SOURCE_DIR)) {
            console.error(`❌ Error: JS directory not found at ${JS_SOURCE_DIR}`);
            process.exit(1);
        }

        const jsFiles = readdirSync(JS_SOURCE_DIR)
            .filter(f => f.endsWith('.js') && !f.match(/\.[a-f0-9]{8}\.js$/)); // Exclude already hashed files

        // Copy JS files to dist directory without hashing
        jsFiles.forEach(filename => {
            const sourcePath = join(JS_SOURCE_DIR, filename);
            const destPath = join(JS_DIST_DIR, filename);
            copyFileSync(sourcePath, destPath);
        });

        console.log(`   Copied ${jsFiles.length} JS files to dist`);

        // Create a simple properties file pointing to non-hashed files
        let devPropertiesContent = `# JS version file for development (no cache busting)\n`;
        devPropertiesContent += `js.generated=${new Date().toISOString()}\n`;

        jsFiles.forEach(filename => {
            const baseName = basename(filename, '.js');
            devPropertiesContent += `js.${baseName}=${filename}\n`;
        });

        writeFileSync(VERSION_FILE, devPropertiesContent);
        console.log(`✅ Created dev version file: ${VERSION_FILE}`);
        console.log(`   JS files: ${jsFiles.join(', ')} (no hashes)`);
        console.log('');
        console.log('💡 Tip: Run "bun run build" for production build with cache busting');
        return;
    }

    console.log('🔨 Generating JS hashes for cache busting (production mode)...');

    // Check if JS directory exists
    if (!existsSync(JS_SOURCE_DIR)) {
        console.error(`❌ Error: JS directory not found at ${JS_SOURCE_DIR}`);
        process.exit(1);
    }

    // Ensure dist directory exists
    if (!existsSync(JS_DIST_DIR)) {
        mkdirSync(JS_DIST_DIR, { recursive: true });
    }

    // Get all JS files
    const jsFiles = readdirSync(JS_SOURCE_DIR)
        .filter(f => f.endsWith('.js') && !f.match(/\.[a-f0-9]{8}\.js$/)); // Exclude already hashed files

    if (jsFiles.length === 0) {
        console.warn('⚠️  No JS files found in', JS_SOURCE_DIR);
        return;
    }

    console.log(`   Found ${jsFiles.length} JS files to hash`);

    // First pass: Generate hashes and create mapping
    const hashedFiles = {};
    jsFiles.forEach(filename => {
        const sourcePath = join(JS_SOURCE_DIR, filename);
        const hash = generateHash(sourcePath);
        const baseName = basename(filename, '.js');
        const hashedFilename = `${baseName}.${hash}.js`;

        // Store mapping
        hashedFiles[baseName] = hashedFilename;
    });

    // Second pass: Copy files and update imports
    jsFiles.forEach(filename => {
        const sourcePath = join(JS_SOURCE_DIR, filename);
        const baseName = basename(filename, '.js');
        const hashedFilename = hashedFiles[baseName];
        const hashedFilePath = join(JS_DIST_DIR, hashedFilename);

        // Read the file content
        let content = readFileSync(sourcePath, 'utf8');

        // Update all import paths to use hashed filenames
        // Match patterns like: './moduleName.js' or '/dist/moduleName.js'
        Object.entries(hashedFiles).forEach(([name, hashedName]) => {
            if (name !== baseName) { // Don't replace self-reference
                // Replace relative imports: './name.js' or './name'
                content = content.replace(
                    new RegExp(`(['"])\\./${name}(\\.js)?\\1`, 'g'),
                    `$1./${hashedName}$1`
                );
                // Replace absolute dist imports: '/dist/name.js' or '/dist/name'
                content = content.replace(
                    new RegExp(`(['"'])/dist/${name}(\\.js)?\\1`, 'g'),
                    `$1/dist/${hashedName}$1`
                );
            }
        });

        // Write the modified content to dist directory
        writeFileSync(hashedFilePath, content);

        console.log(`   ✓ ${filename} → dist/${hashedFilename}`);
    });

    // Create properties file
    let propertiesContent = `# JS version file for cache busting (auto-generated)\n`;
    propertiesContent += `js.generated=${new Date().toISOString()}\n`;

    Object.entries(hashedFiles).forEach(([baseName, hashedFilename]) => {
        propertiesContent += `js.${baseName}=${hashedFilename}\n`;
    });

    // Ensure directory exists
    const versionDir = dirname(VERSION_FILE);
    if (!existsSync(versionDir)) {
        mkdirSync(versionDir, { recursive: true });
    }

    writeFileSync(VERSION_FILE, propertiesContent);
    console.log(`✅ Created version file: ${VERSION_FILE}`);
    console.log('');
    console.log('✨ Cache busting setup complete!');
}

main();
