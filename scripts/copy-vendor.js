#!/usr/bin/env node

/**
 * Vendor File Copier
 * 
 * This script copies vendor files from node_modules to the vendor directory
 */

import { copyFileSync, existsSync, mkdirSync } from 'fs';
import { join, dirname } from 'path';
import { fileURLToPath } from 'url';

const __filename = fileURLToPath(import.meta.url);
const __dirname = dirname(__filename);
const projectRoot = join(__dirname, '..');

const VENDOR_FILES = [
  // Add vendor files here as needed
  // Example:
  // {
  //   source: join(projectRoot, 'node_modules/package-name/file.js'),
  //   dest: join(projectRoot, 'src/main/resources/META-INF/resources/js/vendor/file.js')
  // }
];

function main() {
  console.log('📦 Copying vendor files...');
  
  let copiedCount = 0;
  
  VENDOR_FILES.forEach(({ source, dest }) => {
    if (!existsSync(source)) {
      console.error(`❌ Source file not found: ${source}`);
      return;
    }
    
    // Ensure destination directory exists
    const destDir = dirname(dest);
    if (!existsSync(destDir)) {
      mkdirSync(destDir, { recursive: true });
    }
    
    copyFileSync(source, dest);
    console.log(`   ✓ ${source.split('/').pop()} → ${dest.replace(projectRoot, '')}`);
    copiedCount++;
  });
  
  console.log(`✅ Copied ${copiedCount} vendor file(s)`);
}

main();