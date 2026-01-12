# Agent sandbox

Here are practical, non‑Docker ways to confine the agent to its workspace on a Linux production host, from simplest to stronger. Pick the one that fits your ops model.

Minimal and robust (systemd sandbox + file perms)
1) Create a dedicated user for the agent (no shell, no home):
   sudo useradd -r -s /usr/sbin/nologin agent
2) Create the workspace root and own it:
   sudo mkdir -p /srv/agent-workspaces
   sudo chown agent:agent /srv/agent-workspaces
   chmod 700 /srv/agent-workspaces
3) Run the agent via a systemd unit with a tight sandbox. Key directives:
   [Unit]
   Description=InvoiceSurf Agent
   After=network.target

   [Service]
   User=agent
   Group=agent
   WorkingDirectory=/srv/agent-workspaces
   ExecStart=/path/to/agent-binary   # or wrapper script
   NoNewPrivileges=yes
   PrivateTmp=yes
   PrivateDevices=yes
   ProtectSystem=strict
   ProtectHome=yes
   ProtectControlGroups=yes
   ProtectKernelTunables=yes
   ProtectKernelModules=yes
   ProtectClock=yes
   RestrictAddressFamilies=AF_UNIX AF_INET AF_INET6
   CapabilityBoundingSet=
   AmbientCapabilities=
   RestrictSUIDSGID=yes
   LockPersonality=yes
   MemoryDenyWriteExecute=yes
   ReadWritePaths=/srv/agent-workspaces
   ReadOnlyPaths=/dev /proc /sys
   PrivateMounts=yes

   [Install]
   WantedBy=multi-user.target
   Notes:
   - ProtectSystem=strict makes /, /usr, /boot, etc. read-only; ReadWritePaths punches a writable hole only for the workspace.
   - PrivateMounts isolates mounts in the unit; agent can’t see host mounts added later.
   - ProtectHome=yes blocks /home and /root.
   - CapabilityBoundingSet= (empty) drops all caps; ensure the binary doesn’t require extra caps.
   - Add LimitNOFILE/LimitNPROC if you want resource caps.

4) Ensure your app only points the agent at paths under /srv/agent-workspaces, and reject absolute paths or path traversal in code (normalize and check that resolved paths stay within the workspace).

Stronger confinement (add MAC):
- AppArmor: write a profile that allows rw only in /srv/agent-workspaces/** and denies /, /home, /var, etc. Load and attach to the systemd service with AppArmorProfile= in newer systemd, or via aa-enforce profile.
- SELinux: create a confined type for the agent domain with allow rules only for /srv/agent-workspaces; run the unit with SELinuxContext=.

Alternative lightweight sandbox (no chroot, no Docker):
- bubblewrap (bwrap) or systemd-nspawn --private-network off: create a mount namespace with the workspace bind-mounted, /usr read-only, and no other paths. Example bwrap:
  bwrap --unshare-all --die-with-parent \
    --bind /srv/agent-workspaces /workspace \
    --ro-bind /usr /usr --ro-bind /lib /lib --ro-bind /lib64 /lib64 \
    --dev /dev --proc /proc --tmpfs /tmp \
    --chdir /workspace \
    /path/to/agent-binary
  Run that from a systemd unit under User=agent.

If you must use chroot:
- Create a minimal chroot rooted at /srv/agent-chroot containing /srv/agent-workspaces bound inside; run the agent with chroot(8). You’ll need to populate /usr/lib, libs, /dev/null, /dev/urandom, etc. This is heavier than systemd+bwrap.

Coding-side guardrails (do these regardless):
- Validate and normalize any file path input; reject if it escapes the workspace (e.g., using realpath and checking it starts with /srv/agent-workspaces).
- Disable following untrusted symlinks inside the workspace if possible, or open files with O_NOFOLLOW (when applicable).
- Keep the agent process non-root.

Recommended default: use the systemd unit above plus file perms; add AppArmor if you want defense in depth.

  [user]
  do we really need systemd unit for the bwrap approach? i just want to run a command that later i can kill from the main app.

  [assistant]
  No, you don’t need a systemd unit for the bubblewrap approach. You can just spawn a bwrap’d child process from your app, keep its PID, and kill it when you’re done. Systemd is only a convenient wrapper for lifecycle and sandbox flags; bwrap already gives you the isolation.

Minimal pattern (run as a non-root user dedicated to the agent):

```sh
bwrap --unshare-all --die-with-parent \
  --bind /srv/agent-workspaces /workspace \
  --ro-bind /usr /usr --ro-bind /lib /lib --ro-bind /lib64 /lib64 \
  --dev /dev --proc /proc --tmpfs /tmp \
  --chdir /workspace \
  /path/to/agent-binary arg1 arg2 ...
```

Notes:
- Keep the parent process alive; `--die-with-parent` ensures the sandboxed child exits if the parent dies.
- Your main app can store the child PID and send SIGTERM/SIGKILL when needed. If you want the agent and its children in their own process group, start it with `setsid` (or `posix_spawn` with a new pgroup) and kill the group.
- Add/remove flags as needed:
  - If the agent needs network: drop `--unshare-net` (implied by `--unshare-all`); or explicitly allow with `--share-net`. If you want isolation, keep the unshared network.
  - If you need only a subset of /dev, replace `--dev /dev` with specific `--dev-bind` for `/dev/null`, `/dev/urandom`, etc.
  - If the binary needs other host paths (fonts, locale), add read-only binds.
- Ensure `/srv/agent-workspaces` is owned by the agent user and mode 700, so that’s the only writable tree.

So: spawn bwrap from your app, keep the PID, and kill it when done. Systemd not required.

  [user]
  how battle-tested is bwrap. any previous exploits? 

RESPONSE:

  Bubblewrap is widely used and mature: it’s the core sandbox for Flatpak apps and is used by many Linux desktops, ChromeOS Crostini, and CI sandboxes. It’s actively maintained by Red Hat/Flatpak folks.

Past issues (all fixed promptly):
- CVE-2017-5226: symlink race on user-controlled working directory.
- CVE-2019-11461: TOCTOU on /proc during sandbox setup.
- CVE-2020-5291: environment variable handling in a specific setup path.
- CVE-2022-3560: privilege escalation in some setups when combined with polkit misconfig (really a polkit/authorization issue but surfaced via bwrap calls).
- A handful of distro-specific hardening/packaging fixes over the years.

There are no known unfixed privilege-escalation bugs in current releases. It’s considered solid for unprivileged user namespaces isolation (the typical use). As with any sandbox, keep it updated, drop capabilities, run as a non-root user, and bind only what you need.

  [user]
  how do i allow agents to download packages?

  [assistant]
  If agents need network access (to download packages, call APIs, etc.), you have a few options with bwrap:

**Option 1: Share the host's network namespace**
```sh
bwrap --unshare-all --share-net --die-with-parent \
  --bind /srv/agent-workspaces /workspace \
  --ro-bind /usr /usr --ro-bind /lib /lib --ro-bind /lib64 /lib64 \
  --ro-bind /etc/resolv.conf /etc/resolv.conf \
  --ro-bind /etc/ssl /etc/ssl \
  --ro-bind /etc/ca-certificates /etc/ca-certificates \
  --dev /dev --proc /proc --tmpfs /tmp \
  --chdir /workspace \
  /path/to/agent-binary
```

The `--share-net` overrides the network isolation from `--unshare-all`. The additional binds ensure DNS resolution and HTTPS work.

**Option 2: Use selective unsharing**
Instead of `--unshare-all`, explicitly unshare only what you need (omitting `--unshare-net`):
```sh
bwrap --unshare-user --unshare-pid --unshare-ipc --unshare-uts \
  --die-with-parent \
  --bind /srv/agent-workspaces /workspace \
  --ro-bind /usr /usr --ro-bind /lib /lib --ro-bind /lib64 /lib64 \
  --ro-bind /etc/resolv.conf /etc/resolv.conf \
  --ro-bind /etc/ssl /etc/ssl \
  --dev /dev --proc /proc --tmpfs /tmp \
  --chdir /workspace \
  /path/to/agent-binary
```

**Security trade-off**: Network access allows arbitrary outbound connections. Consider firewall rules (iptables/nftables) to restrict what the agent user can access if needed.

  [user]
  seems like we should use docker if we want file system security and network access... is there an alternative to bwrap that is easier to use? is there a container runtime that is a lot more lightweight than docker?

## Recommended: Podman (lightweight container runtime)

**Podman** is the best balance of isolation, ease-of-use, and lightweightness for this use case:
- **No daemon** - Unlike Docker, Podman runs containers directly (no background service)
- **Rootless by default** - Better security, no privileged daemon
- **Docker-compatible** - Uses same images, Dockerfiles, and similar CLI
- **Full isolation** - Namespaces + cgroups + seccomp, with easy network control
- **Lightweight** - Minimal overhead compared to Docker

### Setup

1) **Install Podman** (most distros have it packaged):
   ```sh
   # Debian/Ubuntu
   sudo apt install podman
   
   # RHEL/Fedora/CentOS
   sudo dnf install podman
   
   # Arch
   sudo pacman -S podman
   ```

2) **Create agent user** (if not already done):
   ```sh
   sudo useradd -r -s /usr/sbin/nologin agent
   sudo mkdir -p /srv/agent-workspaces
   sudo chown agent:agent /srv/agent-workspaces
   chmod 700 /srv/agent-workspaces
   ```

3) **Build or use an agent image**. Example Dockerfile:
   ```dockerfile
   FROM debian:bookworm-slim
   # or ubuntu:22.04, alpine:latest, etc.
   
   # Install runtime dependencies
   RUN apt-get update && apt-get install -y \
       python3 python3-pip curl git \
       && rm -rf /var/lib/apt/lists/*
   
   # Copy agent binary/code
   COPY agent-binary /usr/local/bin/agent
   RUN chmod +x /usr/local/bin/agent
   
   # Non-root user inside container
   RUN useradd -m -u 1000 agentuser
   USER agentuser
   WORKDIR /workspace
   
   ENTRYPOINT ["/usr/local/bin/agent"]
   ```
   
   Build it:
   ```sh
   podman build -t agent-sandbox:latest .
   ```

4) **Run agent from your app**:
   ```sh
   podman run --rm \
     --name agent-task-123 \
     --cap-drop=ALL \
     --network=bridge \
     --tmpfs /tmp:rw,noexec,nosuid,size=1g \
     -v /srv/agent-workspaces/task-123:/workspace:Z \
     -w /workspace \
     --memory=2g \
     --cpus=2 \
     --pids-limit=512 \
     agent-sandbox:latest arg1 arg2
   ```

   **Flag breakdown**:
   - `--rm`: Auto-remove container when it exits
   - `--name`: Unique name for this agent task (for tracking/killing)
   - `--cap-drop=ALL`: Drop all capabilities
   - `--network=bridge`: Allow network access (use `--network=none` to disable)
   - `--tmpfs /tmp`: Writable /tmp that disappears after container exits
   - `-v .../task-123:/workspace:Z`: Bind mount workspace (`:Z` relabels for SELinux)
   - `--memory`, `--cpus`, `--pids-limit`: Resource limits
   
   **Note**: Do NOT use `--security-opt=no-new-privileges` if your entrypoint uses `gosu` or `su-exec` to drop privileges.
   Do NOT use `--user` if the entrypoint handles permission setup internally.

5) **Managing the container from your app**:
   
   **Spawn container** (store the container name/ID):
   ```python
   import subprocess
   
   container_name = f"agent-task-{task_id}"
   subprocess.Popen([
       "podman", "run", "--rm", 
       f"--name={container_name}",
       "--cap-drop=ALL",
       "--network=bridge",
       "--tmpfs", "/tmp:rw,noexec,nosuid,size=1g",
       "-v", f"/srv/agent-workspaces/{task_id}:/workspace:Z",
       "-w", "/workspace",
       "--memory=2g",
       "--cpus=2",
       "--pids-limit=512",
       "agent-sandbox:latest"
   ])
   ```
   
   **Kill container** when done:
   ```python
   subprocess.run(["podman", "kill", container_name])
   # or: podman stop --time=5 {container_name}  (graceful shutdown)
   ```
   
   **Check if running**:
   ```python
   result = subprocess.run(
       ["podman", "ps", "-q", "--filter", f"name={container_name}"],
       capture_output=True, text=True
   )
   is_running = bool(result.stdout.strip())
   ```

### Why Podman over bwrap?

| Feature | bwrap | Podman |
|---------|-------|--------|
| **Ease of use** | Manual bind mounts, complex flags | Standard container workflow |
| **Network + isolation** | Requires careful mount setup for DNS/certs | Works out of the box |
| **Resource limits** | Requires cgroups setup separately | Built-in (--memory, --cpus) |
| **Image management** | None (bare binary) | Standard container images |
| **Rootless** | ✓ | ✓ |
| **Maturity** | Battle-tested (Flatpak) | Battle-tested (RHEL, Fedora default) |

Use **bwrap** if: you want minimal dependencies and are comfortable with manual configuration.

Use **Podman** if: you want container-level isolation with less configuration complexity, especially when network access is needed.

### Alternatives to Podman

- **Docker** - Similar to Podman but requires root daemon; Podman is lighter and more secure for this use case.
- **gVisor (runsc)** - Can be used as a Podman/Docker runtime for even stronger isolation (userspace kernel); adds overhead.
- **Firejail** - Easier than bwrap but has had more security issues; not recommended for production.
- **systemd-nspawn** - Good middle-ground between bwrap and containers; less ecosystem than Podman.
