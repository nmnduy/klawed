#!/usr/bin/env node

/**
 * JS Convention Validator
 * 
 * Validates that JavaScript files follow the project conventions:
 * - Classic scripts (non-module) should NOT have export/import statements
 * - Module scripts MAY have export/import statements
 * 
 * See docs/JS_CONVENTIONS.md for full details.
 */

import { readFileSync, readdirSync, existsSync } from 'fs';
import { join, dirname } from 'path';
import { fileURLToPath } from 'url';

const __filename = fileURLToPath(import.meta.url);
const __dirname = dirname(__filename);
const projectRoot = join(__dirname, '..');

const JS_SOURCE_DIR = join(projectRoot, 'src/main/resources/META-INF/resources/js');

// Files that are loaded as ES modules (type="module") and MAY use import/export
// This includes files loaded directly as modules AND files imported by other modules
const MODULE_FILES = new Set([
    'fileChat.js',
    'fileExplorer.js',
    'authUtils.js',      // Imported by fileChat.js
    'darkMode.js',       // Imported by fileChat.js
    'tabManager.js',     // Imported by fileChat.js
    'loginWaitlist.js',  // Loaded as module in login.html, waitlist.html
    'markdownUtils.js'   // Imported by fileChat.js, parses markdown
]);

// Patterns that indicate ES module syntax
const EXPORT_PATTERN = /^\s*export\s+(default\s+)?(function|class|const|let|var|async|\{)/m;
const IMPORT_PATTERN = /^\s*import\s+/m;

function validateFile(filename, content) {
    const errors = [];
    
    // Skip module files - they can use import/export
    if (MODULE_FILES.has(filename)) {
        return errors;
    }
    
    // Check for export statements
    if (EXPORT_PATTERN.test(content)) {
        const match = content.match(EXPORT_PATTERN);
        const lineNum = content.substring(0, match.index).split('\n').length;
        errors.push({
            file: filename,
            line: lineNum,
            type: 'export',
            message: `Found 'export' statement in classic script. Remove 'export' or add file to MODULE_FILES list.`
        });
    }
    
    // Check for import statements (but not dynamic import())
    // We need to be careful not to match comments or strings
    const lines = content.split('\n');
    lines.forEach((line, index) => {
        const trimmed = line.trim();
        // Skip comments
        if (trimmed.startsWith('//') || trimmed.startsWith('*') || trimmed.startsWith('/*')) {
            return;
        }
        // Check for static import
        if (/^import\s+/.test(trimmed)) {
            errors.push({
                file: filename,
                line: index + 1,
                type: 'import',
                message: `Found 'import' statement in classic script. Remove import or add file to MODULE_FILES list.`
            });
        }
    });
    
    return errors;
}

function main() {
    console.log('🔍 Validating JS conventions...');
    
    if (!existsSync(JS_SOURCE_DIR)) {
        console.error(`❌ JS directory not found: ${JS_SOURCE_DIR}`);
        process.exit(1);
    }
    
    const jsFiles = readdirSync(JS_SOURCE_DIR)
        .filter(f => f.endsWith('.js') && !f.match(/\.[a-f0-9]{8}\.js$/));
    
    if (jsFiles.length === 0) {
        console.log('   No JS files found to validate.');
        return;
    }
    
    console.log(`   Checking ${jsFiles.length} JS files (${MODULE_FILES.size} module files excluded from export/import check)`);
    
    const allErrors = [];
    
    jsFiles.forEach(filename => {
        const filePath = join(JS_SOURCE_DIR, filename);
        const content = readFileSync(filePath, 'utf8');
        const errors = validateFile(filename, content);
        allErrors.push(...errors);
    });
    
    if (allErrors.length > 0) {
        console.log('');
        console.log('⚠️  JS Convention Warnings:');
        console.log('');
        
        allErrors.forEach(err => {
            console.log(`   ${err.file}:${err.line}`);
            console.log(`   └─ ${err.message}`);
            console.log('');
        });
        
        console.log(`   Found ${allErrors.length} issue(s). See docs/JS_CONVENTIONS.md for details.`);
        console.log('');
        
        // Exit with error code to fail the build
        // Change to process.exit(1) if you want to enforce strictly
        // For now, just warn
        // process.exit(1);
    } else {
        console.log('   ✅ All JS files follow conventions.');
    }
}

main();
