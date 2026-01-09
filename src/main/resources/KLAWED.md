# Workspace Agent Instructions

## File Organization and Cleanup Guidelines

### Core Principles
1. **Keep workspace root clean** - Do not clutter the root directory with files
2. **Organize files logically** - Use appropriate directories for different file types
3. **Clean up regularly** - Remove temporary and unnecessary files after use
4. **Be intentional** - Every file should have a purpose and proper location

### Directory Structure

#### Standard Directories
- `uploads/` - User-uploaded files (do not modify)
- `tmp/` - Temporary files (clean up after use)
- `SKILLS/` - Reusable scripts and tools (reference only)
- `.klawed/` - Agent logs and metadata (do not modify)

#### Recommended Working Directories
Create and use these directories as needed for your work:

- `documents/` - For generated documents, reports, and text files
- `code/` - For code files and scripts you create
- `data/` - For datasets, JSON, CSV, and other data files
- `images/` - For generated or processed images
- `analysis/` - For analysis outputs, charts, and visualizations
- `drafts/` - For work-in-progress files
- `archive/` - For completed work that should be preserved

**Important:** Always create and use appropriate subdirectories. Do NOT put files directly in the workspace root unless they are configuration files (like `.gitignore`, `README.md`).

### File Cleanup Rules

#### When to Clean Up
1. **After completing a task** - Remove temporary files that are no longer needed
2. **Before starting new work** - Clean up old temporary files from previous tasks
3. **After generating final output** - Remove intermediate/draft files if not needed
4. **When workspace gets cluttered** - Periodically audit and clean

#### What to Clean Up
- Temporary files in `tmp/` directory (`.tmp`, `.temp`, backup files)
- Intermediate processing files that served their purpose
- Duplicate files or outdated versions
- Files created during debugging or testing
- Empty directories that are no longer needed

#### What NOT to Delete
- User-uploaded files in `uploads/` directory
- Final deliverables and completed work
- Configuration files
- SKILLS directory and its contents
- `.klawed/` directory and logs
- Any files the user explicitly requested to keep

#### How to Clean Up
```bash
# Example cleanup commands you can use:

# Remove old temporary files
find tmp/ -type f -mtime +1 -delete

# Remove backup files in working directories
find documents/ code/ -name "*~" -o -name "*.bak" -delete

# Clean up empty directories
find . -type d -empty -delete

# Remove common temporary file patterns
rm -f *.tmp *.temp .*.swp
```

### Best Practices

#### Before Creating Files
1. **Choose the right directory** - Determine where the file logically belongs
2. **Create subdirectories** - If working on a complex task, create a dedicated subdirectory
3. **Use descriptive names** - File names should clearly indicate their purpose
4. **Plan for cleanup** - Know which files are temporary vs. permanent

#### During Work
1. **Stay organized** - Put files in their designated directories as you create them
2. **Track temporary files** - Keep mental note of which files are temporary
3. **Use tmp/ for temporary work** - All temporary files should go in tmp/
4. **Document important files** - If you create something important, note it in your response

#### After Work
1. **Clean up immediately** - Remove temporary files right after task completion
2. **Organize deliverables** - Move final outputs to appropriate directories
3. **Remove failed attempts** - Delete files from failed or abandoned approaches
4. **Verify cleanup** - List the workspace root to ensure it's clean

### Workspace Root Policy

**The workspace root should be kept clean and minimal.**

Acceptable files in root:
- Configuration files (`.gitignore`, `README.md`, `.env`, etc.)
- Project manifest files (`package.json`, `pom.xml`, etc.)
- Documentation files specific to the workspace

**NOT acceptable in root:**
- Code files (use `code/` directory)
- Data files (use `data/` directory)
- Documents (use `documents/` directory)
- Images (use `images/` directory)
- Temporary files (use `tmp/` directory)
- Random output files

### Examples

#### Good: Organized Approach
```bash
# Task: Analyze CSV data and generate report

# 1. Create working directory
mkdir -p analysis/sales_report_2024

# 2. Process data (keeping intermediate files organized)
cp uploads/sales_data.csv data/sales_data_raw.csv
python code/process_sales.py > analysis/sales_report_2024/processed.json

# 3. Generate final report
python code/generate_report.py > documents/sales_report_2024.md

# 4. Clean up
rm -f tmp/*.tmp analysis/sales_report_2024/processed.json
```

#### Bad: Disorganized Approach
```bash
# Task: Analyze CSV data and generate report

# ❌ Files scattered in root
cp uploads/sales_data.csv ./sales.csv
python process.py > output.json
python report.py > report.md

# ❌ No cleanup
# Leaves: sales.csv, output.json, process.py, report.py, report.md in root
```

### Cleanup Checklist

Before reporting task completion, verify:
- [ ] Workspace root is clean (minimal files)
- [ ] Temporary files removed from `tmp/`
- [ ] Working files organized in proper directories
- [ ] No duplicate or backup files left behind
- [ ] No empty directories lingering
- [ ] Final deliverables are in logical locations

### Automation

Consider creating cleanup scripts for repeated tasks:

```bash
#!/bin/bash
# cleanup.sh - Standard workspace cleanup

echo "Cleaning temporary files..."
rm -rf tmp/*.tmp tmp/*.temp

echo "Removing backup files..."
find . -name "*~" -o -name "*.bak" -o -name ".*.swp" -delete

echo "Removing empty directories..."
find . -type d -empty -delete

echo "Workspace cleaned!"
```

### Remember

**A clean, organized workspace is a sign of professional work.**
- Users appreciate coming back to a tidy workspace
- Organization helps you find files later
- Cleanup prevents confusion and errors
- Good habits build trust with users

---

## Available Resources

The following directories and files are available in your workspace:
