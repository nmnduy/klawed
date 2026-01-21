#!/usr/bin/env node
/**
 * Validates that all dynamic-*.css classes are referenced in class-reference.html
 * Run: bun scripts/validate-dynamic-css.js
 *
 * This ensures Tailwind will detect and include all dynamic classes.
 */

import { readFileSync, readdirSync } from 'fs';
import { join, dirname } from 'path';
import { fileURLToPath } from 'url';

const __dirname = dirname(fileURLToPath(import.meta.url));
const projectRoot = join(__dirname, '..');
const cssDir = join(projectRoot, 'src/main/resources/css');

// Extract class names from CSS files
function extractClassesFromCSS(cssContent) {
  const classRegex = /^\s*\.(dynamic-[a-z0-9-]+)\s*\{/gm;
  const classes = [];
  let match;
  while ((match = classRegex.exec(cssContent)) !== null) {
    classes.push(match[1]);
  }
  return classes;
}

// Extract class names from HTML reference file
function extractClassesFromHTML(htmlContent) {
  const classRegex = /class="([^"]+)"/g;
  const classes = new Set();
  let match;
  while ((match = classRegex.exec(htmlContent)) !== null) {
    match[1].split(/\s+/).forEach(c => {
      if (c.startsWith('dynamic-')) classes.add(c);
    });
  }
  return classes;
}

// Get all dynamic-*.css files
const cssFiles = readdirSync(cssDir).filter(f => f.startsWith('dynamic-') && f.endsWith('.css'));

// Extract all class names from CSS files
const cssClasses = new Map(); // class -> file
for (const file of cssFiles) {
  const content = readFileSync(join(cssDir, file), 'utf-8');
  const classes = extractClassesFromCSS(content);
  classes.forEach(c => cssClasses.set(c, file));
}

// Extract classes from reference file
const refPath = join(cssDir, 'class-reference.html');
let refClasses;
try {
  const refContent = readFileSync(refPath, 'utf-8');
  refClasses = extractClassesFromHTML(refContent);
} catch (e) {
  console.error('❌ Missing class-reference.html file');
  console.error('   Create src/main/resources/css/class-reference.html');
  process.exit(1);
}

// Find discrepancies
const missingFromRef = [...cssClasses.keys()].filter(c => !refClasses.has(c));
const extraInRef = [...refClasses].filter(c => !cssClasses.has(c));

// Report
console.log(`Found ${cssClasses.size} classes in ${cssFiles.length} dynamic-*.css files`);
console.log(`Found ${refClasses.size} classes in class-reference.html\n`);

let hasErrors = false;

if (missingFromRef.length > 0) {
  console.error('❌ MISSING from class-reference.html (will not be in CSS output):');
  missingFromRef.forEach(c => console.error(`   - ${c} (from ${cssClasses.get(c)})`));
  console.error('\nAdd these to src/main/resources/css/class-reference.html');
  hasErrors = true;
}

if (extraInRef.length > 0) {
  console.warn('\n⚠️  In class-reference.html but not defined in CSS (can be removed):');
  extraInRef.forEach(c => console.warn(`   - ${c}`));
}

if (hasErrors) {
  process.exit(1);
}

console.log('✅ All dynamic classes are properly referenced');
