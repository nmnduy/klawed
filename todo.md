## General

- [x] lots of left over klawed db files in the workspace. maybe klawed files dont get cleaned up?
- [x] fix svg icons for open file and download file. use three dots to open file options like show open and download

- [x] auto-scroll should not scroll all the way down. should stop at the first response from AI, then let user scroll. also add scroll to bottom floatting button
- [x] show progress bar for file upload if the file is gonna be large.
- [x] file explorer search doesn't work. use fuzzy search with optimization. use libraries if neeced. this can be very fast if we use specialized algo and and build specific data structure

---

- [ ] one narrated demo
- [ ] randomnized tips

---

- [ ] pricing
- [ ] work on distribution

---

- [ ] bug: having multiple windows for the same user means all windows will return the same response. we manage by workspace and not sessions
- [ ] highlight related new files after a work session
- [ ] show KLAWED what cli tools it already has
- [ ] chrome extension that communicate with local file system
- [ ] ui test scenarios
- [ ] test auto compaction
- [x] use typst for pdf
- [x] three dots aren't very good
- [x] what if klawed die

---

- [ ] context7: documentation search mcp
- [ ] web browsing agent
- [ ] secrets management
- [ ] put some stuff in the starting workspace for everyone
    - [ ] stock analysis
    - [ ] youtube video transcriber
- [x] we can preview more common files: html

---

- [x] better tour
- [x] production
    - [x] install tools: python, latex, klawed binary
    - [x] anthropic run env var
- [x] file explorer side by side with chat when we have space
- [x] dark mode
- [x] alert
    - [x] cpu, memory, error
    - [x] disk size

- [x] write filesurf KLAWED.md inside the workspace agent
    - [x] agent should organize files and clean up files often. sometimes agent write lots of files. or put random files in workspace root.
- [x] connection indicator rewrite completely. styles do not show up correctly
- [x] dont show the spinner in file explorer since it's periodical and seeing it a lot is just annoying. also, any way to avoid the jitter when the file reload?
- [x] chat view is ugly
- [x] /metrics endpoint should not be public. we expose the application via cloudflare. prometheus server is scraping the server metrics via tailscale, network group 100.x.x.x
- [x] upload button in file explorer only allow folder upload. not sure if input box upload button has the same issue

- [x] version the js, css files
- [x] privacy policies
- [x] context size limit. it should be able to continue indefinitely
    - [x] autocompaction
- [x] agent sandbox
    - [x] docker image packages
        - [x] klawed with memvid
        - [x] latex

---

## Improvements

- [ ] s3 as workspace backend
    - [ ] progress bar for sync status

---

## Security Hardening

### Download Safety
- [ ] Add confirmation modal for risky file extensions (.exe, .bat, .sh, .zip, .dmg, etc.)
- [ ] Detect and warn about double extensions (e.g., invoice.pdf.exe)
- [ ] Add `X-Content-Type-Options: nosniff` header to downloads

### Upload Controls
- [ ] Warn user on files > 200MB before upload
- [ ] Per-user storage quota (e.g., 500MB in total workspace
- [ ] Allow user to upload any files (if not already)

## Keeps me up at night

- [ ] Long running script that keeps spinning CPU
- [ ] Malicious script that tries to break out of the sandbox
- [ ] Script that keeps using LLM tokens
