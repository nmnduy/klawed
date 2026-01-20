# Lucide Icons Reference and Troubleshooting

## Overview
FileSurf v2 uses [Lucide Icons](https://lucide.dev/) v0.562.0 for its icon system. Icons are loaded from a minified JavaScript file and rendered using the `data-lucide` attribute.

## Current Version
- **Version**: 0.562.0
- **File**: `src/main/resources/META-INF/resources/js/vendor/lucide.min.js`
- **Size**: 378KB
- **Icons**: 1864 icons available

## Finding Available Icon Names

### Method 1: Extract from Minified File (Command Line)
Since the Lucide file is minified (all on one line), use these commands:

```bash
# 1. Check the version (first few lines contain version info)
head -c 2000 ./src/main/resources/META-INF/resources/js/vendor/lucide.min.js | grep -o "lucide v[0-9.]*"

# 2. Extract all icon names from the exports section at the end
tail -c 30000 ./src/main/resources/META-INF/resources/js/vendor/lucide.min.js | \
  grep -o "a\.[A-Z][a-zA-Z0-9]*=" | \
  sed 's/a\.//g' | sed 's/=//g' | \
  sort | uniq > /tmp/lucide_icons.txt

# 3. Count total icons
wc -l /tmp/lucide_icons.txt

# 4. Search for specific icons
grep -i "search" /tmp/lucide_icons.txt
grep -i "^Search$" /tmp/lucide_icons.txt  # Exact match
```

### Method 2: Check Specific Icon Existence
```bash
# Create a list of icons to check
cat > /tmp/icons_to_check.txt << 'EOF'
activity
RefreshCw
CheckCircle
XCircle
EOF

# Check each one
for icon in $(cat /tmp/icons_to_check.txt); do
    capitalized=$(echo "$icon" | sed 's/^./\u&/')
    if grep -qi "^$capitalized$" /tmp/lucide_icons.txt; then
        echo "✓ $icon exists (as $capitalized)"
    else
        echo "✗ $icon NOT FOUND"
    fi
done
```

### Method 3: Browse Online Reference
For a human-readable list, visit:
- [Lucide Icons Gallery](https://lucide.dev/icons/)
- Search for icons by name
- Note: Ensure you're looking at v0.562.0 (some icons may have been added/removed in later versions)

## Common Issues and Solutions

### Issue: Console Warning "icon name was not found"
**Symptoms**: Browser console shows warning: `icon name was not found in the provided icons object`

**Causes**:
1. Icon name doesn't exist in the current Lucide version
2. Wrong case (e.g., `xcircle` instead of `XCircle`)
3. Typo in icon name

**Debugging Steps**:
1. Check the exact icon name in the warning message
2. Verify it exists using Method 2 above
3. Check if case needs correction (Lucide uses PascalCase: `CheckCircle`, not `checkCircle`)

### Issue: Icon Not Displaying
**Check**:
1. Is Lucide script loaded? (`<script src="/js/vendor/lucide.min.js" defer></script>`)
2. Are icons initialized? (`lucide.createIcons()` called after DOM updates)
3. Is the icon name correct?

## Icon Naming Convention

### PascalCase Required
Lucide expects icon names in **PascalCase** (first letter of each word capitalized):
- ✅ `CheckCircle`, `XCircle`, `RefreshCw`, `FileText`
- ❌ `checkCircle`, `xcircle`, `refreshcw`, `filetext`

### Case Conversion
Lucide internally converts names using a function that:
1. Capitalizes first letter of each word
2. Converts underscores/dashes to PascalCase
3. Example: `check-circle` → `CheckCircle`

However, for consistency and clarity, **always use PascalCase** in code.

## Icons Used in FileSurf v2

### Tool Activity Icons (in `fileChat.js`)
```javascript
const toolMap = {
    'Read': { icon: 'FileText', label: 'Reading file', verb: 'Read' },
    'Write': { icon: 'PenLine', label: 'Writing file', verb: 'Wrote' },
    'Edit': { icon: 'Wrench', label: 'Editing file', verb: 'Edited' },
    'MultiEdit': { icon: 'Wrench', label: 'Editing file', verb: 'Edited' },
    'Grep': { icon: 'Search', label: 'Searching', verb: 'Searched' },
    'Glob': { icon: 'Folder', label: 'Finding files', verb: 'Found files' },
    'Bash': { icon: 'Terminal', label: 'Running command', verb: 'Ran command' },
    'Subagent': { icon: 'Bot', label: 'Running subagent', verb: 'Subagent completed' },
    'TodoWrite': { icon: 'CheckSquare', label: 'Updating tasks', verb: 'Updated tasks' },
    'MemoryStore': { icon: 'Database', label: 'Storing memory', verb: 'Stored memory' },
    'MemoryRecall': { icon: 'Brain', label: 'Recalling memory', verb: 'Recalled memory' },
    'MemorySearch': { icon: 'Search', label: 'Searching memory', verb: 'Searched memory' },
    'UploadImage': { icon: 'Image', label: 'Processing image', verb: 'Processed image' },
    'Sleep': { icon: 'Clock', label: 'Waiting', verb: 'Waited' },
    'CheckSubagentProgress': { icon: 'RefreshCw', label: 'Checking progress', verb: 'Checked progress' },
    'InterruptSubagent': { icon: 'Slash', label: 'Stopping subagent', verb: 'Stopped subagent' }
};
```

### Status Icons
```javascript
// Error status
`<i data-lucide="XCircle" class="w-3.5 h-3.5 text-red-500"></i>`

// Success status  
`<i data-lucide="CheckCircle" class="w-3.5 h-3.5 text-emerald-500"></i>`
```

## Lessons Learned from Debugging

### 1. Version Matters
- Different Lucide versions have different icon sets
- Always check the version in the file header
- `activity` icon existed in earlier versions but not in v0.562.0

### 2. Case Sensitivity
- While Lucide may handle case conversion, be explicit
- Use PascalCase consistently to avoid confusion
- Fixed: `xCircle` → `XCircle`, `checkCircle` → `CheckCircle`

### 3. Icon Selection
- Choose icons that semantically match the action
- `activity` (missing) → `RefreshCw` (better for "checking progress")
- Consider alternatives: `SquareActivity`, `Eye`, `Monitor`

### 4. Build Process
- JS files are rebuilt with `npm run build:dev` or `npm run build`
- Dist files are gitignored, version info in `js-version.properties`
- Always rebuild after changing icon names

## Adding New Icons

### Steps:
1. Check if icon exists in current Lucide version
2. Use PascalCase name in code
3. Rebuild JS files if needed
4. Test in browser (check console for warnings)

### Example:
```javascript
// Before (if 'activity' doesn't exist):
{ icon: 'activity', label: 'Checking progress', verb: 'Checked progress' }

// After (using existing icon):
{ icon: 'RefreshCw', label: 'Checking progress', verb: 'Checked progress' }
```

## Useful Commands Summary

```bash
# Extract all icon names
tail -c 30000 ./src/main/resources/META-INF/resources/js/vendor/lucide.min.js | \
  grep -o "a\.[A-Z][a-zA-Z0-9]*=" | sed 's/a\.//g' | sed 's/=//g' | sort | uniq

# Check specific icon
icon="RefreshCw"
if tail -c 30000 ./src/main/resources/META-INF/resources/js/vendor/lucide.min.js | \
   grep -q "a\.$icon="; then echo "Exists"; else echo "Missing"; fi

# Find similar icons
grep -i "search\|find\|look" /tmp/lucide_icons.txt

# Count total icons
tail -c 30000 ./src/main/resources/META-INF/resources/js/vendor/lucide.min.js | \
  grep -o "a\.[A-Z][a-zA-Z0-9]*=" | wc -l
```

## Related Files
- `src/main/resources/META-INF/resources/js/fileChat.js` - Tool icons definition
- `src/main/resources/META-INF/resources/js/vendor/lucide.min.js` - Lucide library
- `docs/JS_CACHE_BUSTING.md` - JS build process
- `package.json` - Build scripts

## References
- [Lucide Icons Documentation](https://lucide.dev/)
- [Lucide GitHub](https://github.com/lucide-icons/lucide)
- [Icon Search](https://lucide.dev/icons/)
