# Cloud Storage Integration

## Overview
Access user's cloud storage (Dropbox, Google Drive, OneDrive, S3, etc.) directly from your workspace using **rclone**.

rclone is a command-line tool that lets you sync files and directories to and from 70+ cloud storage providers. It works like `rsync` but for cloud storage.

## Installation Check

```bash
# Check if rclone is installed
which rclone && rclone version

# If not installed (shouldn't happen in container)
curl https://rclone.org/install.sh | sudo bash
```

## Quick Start - Dropbox

### Interactive Setup (Recommended)

```bash
# Start interactive configuration
rclone config

# Follow prompts:
# n) New remote
# name> dropbox
# Storage> dropbox
# client_id> (press Enter for default)
# client_secret> (press Enter for default)
# Edit advanced config? n
# Use web browser to automatically authenticate? Y
#
# Browser opens → User authorizes → Done!
```

### Quick Commands

```bash
# List files
rclone ls dropbox:
rclone lsd dropbox:  # Directories only

# Download file
rclone copy dropbox:Documents/report.pdf ./

# Upload file
rclone copy ./output.txt dropbox:Results/

# Mount as filesystem (best for browsing)
mkdir -p ~/dropbox
rclone mount dropbox: ~/dropbox --vfs-cache-mode full --daemon

# Now use like normal files:
ls ~/dropbox/
cat ~/dropbox/notes.txt
cp ~/dropbox/data.csv ./
```

## Google Drive

### Method 1: User OAuth (Individual)

```bash
rclone config
# n) New remote
# name> gdrive
# Storage> drive
# client_id> (Enter for default)
# client_secret> (Enter for default)
# scope> drive (full access)
# Use web browser? Y
# (Browser opens for authorization)

# Use it:
rclone ls gdrive:
rclone mount gdrive: ~/gdrive --daemon
```

### Method 2: Service Account (Teams)

If admin has provided a service account:

```bash
# Check if service account is configured
rclone listremotes | grep gdrive_shared

# If yes, use it directly:
rclone ls gdrive_shared:

# User needs to share folders with service account email
# (Admin will provide the email)
```

## OneDrive

```bash
rclone config
# n) New remote  
# name> onedrive
# Storage> onedrive
# Follow OAuth prompts

# Use it:
rclone ls onedrive:
rclone mount onedrive: ~/onedrive --daemon
```

## Amazon S3

```bash
rclone config
# n) New remote
# name> s3
# Storage> s3
# provider> AWS (or compatible)
# Access Key ID> (user provides)
# Secret Access Key> (user provides)
# Region> us-east-1 (or user's region)

# Use it:
rclone ls s3:mybucket/
rclone copy s3:mybucket/data/ ./data/
```

## Essential Commands

### List Files

```bash
# List with sizes
rclone ls remote:path

# List directories only
rclone lsd remote:path

# List with details (size, date)
rclone lsl remote:path

# Tree view
rclone tree remote:path
```

### Download/Upload

```bash
# Copy file
rclone copy remote:file.txt ./

# Copy directory
rclone copy remote:folder ./folder

# Sync directory (only downloads changes)
rclone sync remote:folder ./folder

# Copy with progress
rclone copy remote:large_file.zip ./ --progress
```

### Mount (Best for Interactive Work)

```bash
# Mount cloud storage as local filesystem
mkdir -p ~/cloud
rclone mount remote: ~/cloud --vfs-cache-mode full --daemon

# Now access like normal files:
ls ~/cloud/
cat ~/cloud/documents/readme.txt
nano ~/cloud/notes.txt  # Edit directly!

# Unmount when done
fusermount -u ~/cloud
# or: pkill rclone
```

### Bidirectional Sync

```bash
# First time: initialize sync
rclone bisync remote:Work ./work --resync

# Subsequent syncs (syncs both ways)
rclone bisync remote:Work ./work

# Changes in ./work → uploaded to cloud
# Changes in cloud → downloaded to ./work
```

### Search

```bash
# Find files by name
rclone ls remote: | grep "keyword"

# Find specific file type
rclone ls remote: --include "*.pdf"

# Find files larger than 10MB
rclone ls remote: --min-size 10M
```

## Workflow Examples

### Example 1: Analyze User's Documents

```bash
# User: "Analyze all PDFs in my Dropbox Documents folder"

# 1. Check what's there
rclone ls dropbox:Documents | grep '\.pdf$'

# 2. Download PDFs
mkdir -p documents
rclone copy dropbox:Documents ./documents --include "*.pdf" --progress

# 3. Process each PDF
for pdf in documents/*.pdf; do
    echo "Processing: $pdf"
    # Extract text, analyze, etc.
done

# 4. Create report
echo "# Analysis Report" > report.md
# Add findings...

# 5. Upload report
rclone copy report.md dropbox:Documents/
echo "✅ Report uploaded to Dropbox: Documents/report.md"
```

### Example 2: Continuous Workspace Sync

```bash
# User: "Keep my Project folder in sync with Google Drive"

# Mount Drive as local filesystem
mkdir -p ~/gdrive
rclone mount gdrive: ~/gdrive --vfs-cache-mode full --daemon &

# Create symlink to project folder
ln -s ~/gdrive/Projects ./projects

# Work normally - changes sync automatically!
cd projects
ls
cat README.md
nano code.py  # Changes save directly to Drive!

# Tell user:
echo "✅ Your Google Drive Projects folder is now synced at ./projects"
echo "   All changes are automatically synced to Google Drive"
```

### Example 3: Backup Results to Cloud

```bash
# After completing work, backup everything

TIMESTAMP=$(date +%Y%m%d_%H%M%S)

# Upload workspace to cloud
rclone copy ./ dropbox:Backups/filesurf_$TIMESTAMP \
    --exclude ".git/" \
    --exclude "node_modules/" \
    --exclude "tmp/" \
    --exclude "*.log" \
    --progress

echo "✅ Workspace backed up to Dropbox: Backups/filesurf_$TIMESTAMP"
```

### Example 4: Download Dataset from S3

```bash
# User: "Download my ML dataset from S3"

# Download entire bucket/folder
rclone copy s3:ml-datasets/images/ ./dataset/images/ --progress

# Download only specific files
rclone copy s3:ml-datasets/ ./dataset/ \
    --include "train_*.jpg" \
    --include "*.csv" \
    --progress

# Check download
du -sh dataset/
echo "✅ Downloaded $(find dataset -type f | wc -l) files"
```

### Example 5: Multi-Cloud Aggregation

```bash
# User: "Combine files from my Dropbox and Google Drive"

# Mount both clouds
mkdir -p ~/dropbox ~/gdrive
rclone mount dropbox: ~/dropbox --daemon &
rclone mount gdrive: ~/gdrive --daemon &

# Wait for mounts
sleep 2

# Find and process files from both
find ~/dropbox -name "*.csv" -exec cp {} ./combined_data/ \;
find ~/gdrive -name "*.csv" -exec cp {} ./combined_data/ \;

# Process combined data
echo "✅ Found $(ls combined_data/*.csv | wc -l) CSV files from both sources"
```

## Advanced Features

### Filtering

```bash
# Include only specific files
rclone copy remote:folder ./folder --include "*.{jpg,png,gif}"

# Exclude files
rclone copy remote:folder ./folder --exclude "*.tmp" --exclude "*.log"

# Filter by size
rclone copy remote:folder ./folder --min-size 1M --max-size 100M

# Filter by date
rclone copy remote:folder ./folder --max-age 7d  # Last 7 days only
```

### Progress and Logging

```bash
# Show progress
rclone copy remote:large/ ./large/ --progress

# Show statistics
rclone copy remote:folder/ ./folder/ --stats 1s

# Log to file
rclone sync remote:folder/ ./folder/ --log-file sync.log

# Verbose output (debugging)
rclone ls remote: -vv
```

### Bandwidth Control

```bash
# Limit upload/download speed (useful for large files)
rclone copy remote:large/ ./large/ --bwlimit 10M  # 10 MB/s

# Different limits for upload/download
rclone sync ./folder remote:folder --bwlimit 8M:2M  # 8M down, 2M up
```

### Dry Run (Preview Changes)

```bash
# See what would happen without doing it
rclone sync ./folder remote:folder --dry-run

# Shows:
# - Files to upload
# - Files to delete
# - Files to update
```

### Checksums and Verification

```bash
# Verify files are identical
rclone check ./folder remote:folder

# Use checksums instead of size/time
rclone sync ./folder remote:folder --checksum
```

## Troubleshooting

### Mount Issues

```bash
# Check if FUSE is available
fusermount --version

# Unmount stuck mount
fusermount -u ~/cloud
# or force kill:
pkill -9 rclone

# Mount with debugging
rclone mount remote: ~/cloud -vv
```

### Authentication Issues

```bash
# Re-authenticate
rclone config reconnect remote:

# Delete and recreate remote
rclone config delete remote
rclone config  # Create again

# Check if token expired
rclone about remote:  # Will fail if auth is broken
```

### Performance Issues

```bash
# Use VFS caching for faster mounts
rclone mount remote: ~/cloud \
    --vfs-cache-mode full \
    --vfs-cache-max-size 1G \
    --vfs-cache-max-age 1h \
    --daemon

# Use sync instead of copy (only transfers changes)
rclone sync remote:folder ./folder

# Transfer in parallel
rclone copy remote:folder ./folder --transfers 10
```

### Check Configuration

```bash
# List configured remotes
rclone listremotes

# Show config for a remote
rclone config show remote

# Test a remote
rclone lsd remote:  # Should list directories

# Get remote info (quota, etc.)
rclone about remote:
```

## Security Best Practices

```bash
# 1. Use read-only mode when browsing
rclone mount remote: ~/cloud --read-only --daemon

# 2. Preview destructive operations
rclone delete remote:folder --dry-run  # Check first!
rclone delete remote:folder             # Then delete

# 3. Encrypt sensitive files
rclone copy sensitive.txt remote: --crypt

# 4. Verify uploads
rclone check ./file remote:file

# 5. Use specific scopes (don't request full access)
# Configure in rclone config → scope → drive.file (files created by app only)
```

## When to Ask User for Setup

When you need cloud storage access, tell the user:

```
🔐 I need access to your [Dropbox/Google Drive/etc] to [specific task].

To set this up:
1. Run: rclone config
2. Follow the prompts to connect your account
3. Let me know when done!

Alternatively, I can guide you through the setup step-by-step.
```

## Provider-Specific Tips

### Dropbox
- Fast and reliable
- Good for file sharing
- 2GB free tier

### Google Drive
- 15GB free tier
- Great for documents (Docs, Sheets)
- Good collaboration features
- Can use service accounts for teams

### OneDrive
- 5GB free tier
- Good Microsoft Office integration
- Personal vs Business accounts (different auth)

### Amazon S3
- No free tier (pay per use)
- Very fast and scalable
- Great for large datasets
- Supports versioning

## Supported Providers (70+)

rclone supports:
- **Cloud Storage**: Dropbox, Google Drive, OneDrive, Box, Mega, pCloud
- **Object Storage**: Amazon S3, Google Cloud Storage, Azure Blob, Backblaze B2
- **File Servers**: FTP, SFTP, WebDAV, HTTP
- **Business**: Sharepoint, Google Workspace, Microsoft 365
- And many more...

Full list: `rclone listproviders`

## Summary

**rclone is your Swiss Army knife for cloud storage:**
- ✅ Works with 70+ providers
- ✅ CLI-driven (perfect for agents)
- ✅ Mount as local filesystem
- ✅ Bidirectional sync
- ✅ Fast and efficient
- ✅ Production-ready
- ✅ User controls access via OAuth

**Basic Workflow:**
1. User runs `rclone config` to connect their cloud account
2. You use `rclone` commands to access files
3. Files appear as local files in your workspace
4. User sees changes in their cloud storage app
5. Fully bidirectional - changes sync both ways

**Pro Tip:** Use `rclone mount` for interactive work - it makes cloud storage feel like local files!
