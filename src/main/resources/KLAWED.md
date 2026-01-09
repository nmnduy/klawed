# Workspace Agent Instructions

## Privacy and Security

### Overview
FileSurf v2 is built with privacy and security as core principles. This section explains how user data is protected and what measures are in place to ensure isolation and confidentiality.

### How We Protect User Data

#### 1. Sandboxed AI Processing
All AI agents run in **isolated, sandboxed containers**. This ensures that processing happens in a secure environment with strict access controls, preventing unauthorized access to user data.

**Container Isolation (Podman/Docker)**:
- Each agent session runs in its own container
- Filesystem is read-only except for the workspace directory
- Network access is controlled (can be enabled or disabled per container)
- Resource limits enforced (memory, CPU, process limits)
- No privileged access or capabilities
- Containers are automatically removed after session ends

**Sandbox Security Features**:
- `--security-opt=no-new-privileges` - Prevents privilege escalation
- `--cap-drop=ALL` - Drops all Linux capabilities
- `--read-only` - Root filesystem is read-only
- `--tmpfs /tmp` - Temporary files exist only during session
- Resource limits: `--memory`, `--cpus`, `--pids-limit`

#### 2. Enterprise-Grade LLM Security (AWS Bedrock)
FileSurf uses **Amazon Bedrock** for AI-powered features. AWS Bedrock provides enterprise-grade data protection guarantees:

- **No data storage**: Your prompts and completions are not stored or logged by Amazon Bedrock
- **No training data**: Your content is never used to train any AWS models or distributed to third parties
- **No provider access**: Model providers have no access to your prompts, completions, or Amazon Bedrock logs
- **Encrypted in transit**: All communications use TLS 1.2+ encryption (AWS requires TLS 1.2 minimum, recommends 1.3)
- **Isolated processing**: Models run in AWS-controlled deployment accounts with strict access controls

Reference: [AWS Bedrock Data Protection](https://docs.aws.amazon.com/bedrock/latest/userguide/data-protection.html)

#### 3. Session Isolation
Each user session is completely isolated:

- **Separate workspaces**: Each session has its own isolated directory (`/srv/agent-workspaces/{sessionId}`)
- **No cross-session access**: Files and conversations from one session cannot be accessed by other sessions
- **User-scoped data**: All database queries are filtered by userId to prevent cross-user data access
- **Container per session**: Each agent runs in its own sandboxed container

#### 4. Secure Authentication
- **HttpOnly cookies**: `filesurf_userId` cookie with HttpOnly flag (365-day expiration)
- **Cookie security**: Prevents XSS attacks, configurable secure flag for production HTTPS

### What Data We Collect

#### Account Information
- Email address (for invite-only authentication)
- User ID (automatically generated UUID)
- Login timestamps

#### Files and Content
- Files you upload to your sessions
- Chat messages and conversations with AI
- Session metadata (session names, creation times, status)

### Data Retention

- **Session Data**: Your files and chat history remain stored until you explicitly delete them or conclude your session
- **Account Data**: Your email and user ID are retained for the lifetime of your account unless you request deletion
- **AI Processing**: Your prompts and responses are processed in real-time by AWS Bedrock and are not retained by AWS or model providers

### User Rights

You have the following rights regarding your data:

- **Access**: View what data we have about you
- **Deletion**: Delete your sessions, files, and conversations at any time through the UI
- **Export**: Download your files from any session
- **Account Closure**: Request complete account deletion by contacting the administrator

### Technical Security Measures

- **TLS 1.2+ Encryption**: All data in transit is encrypted
- **HttpOnly Cookies**: Protected from XSS attacks
- **Container Isolation**: AI agents run in sandboxed containers with strict resource limits
- **Secure Data Storage**: Industry-standard encryption and access controls
- **Path Validation**: All file operations validate paths to prevent directory traversal
- **Prepared Statements**: Database queries use parameterized statements to prevent SQL injection

### Network Security

- **Network-Restricted Endpoints**: `/metrics` endpoint is restricted to Tailscale network (100.x.x.x) only
- **Protected Endpoints**: All session and file management endpoints require authentication
- **Public Endpoints**: Only authentication pages and static assets are publicly accessible

### Important Privacy Considerations

**AI Processing**: When you interact with the AI chat:
- Your messages and uploaded files are sent to AWS Bedrock for processing
- AWS Bedrock does not store, log, or use your data for training
- Processing happens in real-time and data is not retained by AWS
- All communication with AWS is encrypted via TLS 1.2+

**Session Isolation**: Your sessions are private:
- Other users cannot view your sessions, files, or conversations
- Each session runs in an isolated container environment
- Files are stored in session-specific directories with proper permissions

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
