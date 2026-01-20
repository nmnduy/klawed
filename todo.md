## General

---

## Bugs

- [ ] no lucides icons
- [x] typing indicators shows but not at bottom
- [ ] mount .klawed folder in /tmp/
- [x] sqlite viewer. no row seen

## Distribution

### Sales

- [ ] sales materials

### SEO

- [ ] find semrush keywords
- [ ] write blogs
- [ ] check seo output

### Email

- [ ] list of emails from database
- [ ] how to segment by markets?
    - [ ] .edu?
    - [ ] use ai to segment 
        - [ ] find products

### Features

- [ ] show storage usage
- [ ] context7: documentation search mcp
- [ ] web browsing agent
- [ ] secrets management
- [ ] cronjobs
- [ ] background services for notifications and scheduled tasks
- [ ] pricing
- [ ] voice input
- [ ] web search
- [ ] backups/ folder
    - [ ] LLM can't delete files here
    - [ ] files get synced into external

- [ ] s3 as workspace backend
    - [ ] progress bar for sync status

- [ ] connector
    - [ ] google drive
    - [ ] box
    - [ ] dropbox

- [ ] randomnized tips
- [ ] highlight related new files after a work session
- [ ] show KLAWED what cli tools it already has

- [x] handle out of context error...
- [x] feedback button and error reporting
- [x] one button in file drop down menu copy full file path into input box
- [x] file search does not include folder in results

- [x] prompts
    - [x] give subagents more time. esp for tasks that might take time
    - [x] subagents are more resilient now. give them time. they will stop when fail/ needs more guidance
    - [x] you should build software
    - [x] You are FileSurf. A personal computer in the Cloud, with AI.
- [x] database viewer with: sql runner. tabular view. search. filter.
    - [x] of course for now we only support file based databases: sqlite3

- [x] lots of left over klawed db files in the workspace. maybe klawed files dont get cleaned up?
- [x] fix svg icons for open file and download file. use three dots to open file options like show open and download

- [x] auto-scroll should not scroll all the way down. should stop at the first response from AI, then let user scroll. also add scroll to bottom floatting button
- [x] show progress bar for file upload if the file is gonna be large.
- [x] file explorer search doesn't work. use fuzzy search with optimization. use libraries if neeced. this can be very fast if we use specialized algo and and build specific data structure

---

## In general...

- [ ] help students with homework
    - [ ] good for university
    - [ ] dont give a crap about security
    - [ ] is there a market for this

---

## In general...

- [ ] bug: having multiple windows for the same user means all windows will return the same response. we manage by workspace and not sessions
- [ ] chrome extension that communicate with local file system
- [ ] ui test scenarios
- [ ] test auto compaction
- [x] use typst for pdf
- [x] three dots aren't very good
- [x] what if klawed die

---

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
