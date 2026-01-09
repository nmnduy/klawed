- [ ] put some stuff in the starting workspace for everyone
    - [ ] stock analysis
    - [ ] youtube transcriber
    - [ ] reminders
- [ ] s3 as workspace backend
    - [ ] progress bar for sync status
- [ ] what if someone upload a file that
    - [ ] is a virus program
    - [ ] spin CPU cycles
    - [ ] run a bitcoin miner

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

- [ ] how to show SKILLS
- [ ] file download button
