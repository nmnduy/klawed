#!/usr/bin/env node

/**
 * CSS Hash Generator
 *
 * This script generates a content-based hash for the built CSS file and:
 * 1. Copies main.css to main.[hash].css
 * 2. Creates a css-version.properties file with the hashed filename
 * 3. This allows templates to reference the hashed CSS for cache busting
 */

import { createHash } from 'crypto';
import { readFileSync, writeFileSync, copyFileSync, existsSync, mkdirSync } from 'fs';
import { join, dirname } from 'path';
import { fileURLToPath } from 'url';

const __filename = fileURLToPath(import.meta.url);
const __dirname = dirname(__filename);
const projectRoot = join(__dirname, '..');

const CSS_SOURCE = join(projectRoot, 'src/main/resources/META-INF/resources/dist/main.css');
const DIST_DIR = join(projectRoot, 'src/main/resources/META-INF/resources/dist');
const VERSION_FILE = join(projectRoot, 'src/main/resources/css-version.properties');

function generateHash(filePath) {
    const content = readFileSync(filePath);
    const hash = createHash('md5').update(content).digest('hex');
    return hash.substring(0, 8); // Use first 8 characters
}

function main() {
    // Skip hashing in development mode
    const isDev = process.env.NODE_ENV !== 'production';

    if (isDev) {
        console.log('🔧 Development mode: Skipping CSS hashing');
        // Create a simple properties file pointing to main.css
        const devPropertiesContent = `# CSS version file for development (no cache busting)
css.filename=main.css
css.hash=dev
css.generated=${new Date().toISOString()}
`;
        writeFileSync(VERSION_FILE, devPropertiesContent);
        console.log(`✅ Created dev version file: ${VERSION_FILE}`);
        console.log(`   CSS filename: main.css (no hash)`);
        console.log('');
        console.log('💡 Tip: Run "bun run build" for production build with cache busting');
        return;
    }

    console.log('🔨 Generating CSS hash for cache busting (production mode)...');

    // Check if CSS file exists
    if (!existsSync(CSS_SOURCE)) {
        console.error(`❌ Error: CSS file not found at ${CSS_SOURCE}`);
        console.error('   Run "bun run build" first to generate the CSS file.');
        process.exit(1);
    }

    // Generate hash
    const hash = generateHash(CSS_SOURCE);
    const hashedFilename = `main.${hash}.css`;
    const hashedFilePath = join(DIST_DIR, hashedFilename);

    console.log(`   Hash: ${hash}`);
    console.log(`   Hashed filename: ${hashedFilename}`);

    // Copy CSS file with hash
    copyFileSync(CSS_SOURCE, hashedFilePath);
    console.log(`✅ Copied CSS to ${hashedFilename}`);

    // Create properties file
    const propertiesContent = `# CSS version file for cache busting (auto-generated)
css.filename=${hashedFilename}
css.hash=${hash}
css.generated=${new Date().toISOString()}
`;

    // Ensure directory exists
    const versionDir = dirname(VERSION_FILE);
    if (!existsSync(versionDir)) {
        mkdirSync(versionDir, { recursive: true });
    }

    writeFileSync(VERSION_FILE, propertiesContent);
    console.log(`✅ Created version file: ${VERSION_FILE}`);
    console.log(`   CSS filename: ${hashedFilename}`);
    console.log('');
    console.log('✨ Cache busting setup complete!');
}

main();
