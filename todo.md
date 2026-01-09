- [x] better tour
- [x] production
    - [x] install tools: python, latex, klawed binary
    - [x] anthropic run env var
- [x] file explorer side by side with chat when we have space
- [x] dark mode
- [x] alert
    - [x] cpu, memory, error
    - [x] disk size

- [ ] write filesurf KLAWED.md inside the workspace agent
    - [ ] agent should organize files and clean up files often. sometimes agent write lots of files. or put random files in workspace root.
- [ ] connection indicator rewrite completely. styles do not show up correctly
- [ ] dont show the spinner in file explorer since it's periodical and seeing it a lot is just annoying. also, any way to avoid the jitter when the file reload?
- [ ] chat view is ugly
- [x] /metrics endpoint should not be public. we expose the application via cloudflare. prometheus server is scraping the server metrics via tailscale, network group 100.x.x.x
- [ ] upload button in file explorer only allow folder upload. not sure if input box upload button has the same issue

- [x] version the js, css files
- [x] privacy policies
- [x] context size limit. it should be able to continue indefinitely
    - [x] autocompaction
- [x] agent sandbox
    - [x] docker image packages
        - [x] klawed with memvid
        - [x] latex

---

- [ ] how to show SKILLS
- [ ] file download button

---

## Security Hardening

### Download Safety
- [ ] Add confirmation modal for risky file extensions (.exe, .bat, .sh, .zip, .dmg, etc.)
- [ ] Detect and warn about double extensions (e.g., invoice.pdf.exe)
- [ ] Add `X-Content-Type-Options: nosniff` header to downloads

### Upload Controls
- [ ] Remove per-file size limit (currently 10MB)
- [ ] Warn user on files > 200MB before upload
- [ ] Per-user storage quota (e.g., 500MB total across all sessions)
- [ ] Per-session storage quota (e.g., 200MB)
- [ ] File count limit per session (e.g., 200 files)
- [ ] Expand allowed upload extensions (add .py, .js, .md, .json, .html, .css, .tex, etc.)

### AI Agent Sandbox
- [ ] Container execution timeout (e.g., 30 min max)
- [ ] Workspace disk quota monitoring - kill agent if exceeded
- [ ] Watchdog to kill long-running/stuck containers
- [ ] Update AI system prompt: never execute files from uploads/ directly

### Session Management
- [ ] Auto-cleanup old/inactive sessions (e.g., > 30 days)
- [ ] Verify session ownership on all endpoints
