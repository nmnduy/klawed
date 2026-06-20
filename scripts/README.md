# Klawed CI — Post-Receive Hook

This directory contains the post-receive git hook that turns the `registry`
bare repo into a self-service CI server.

## How it works

```
push to master
  → git post-receive hook fires
  → clones repo to temp dir
  → make test           (all unit tests)
  → make comprehensive-scan  (static analysis, sanitizers, valgrind)
  → make bump-patch     (bump version, commit, tag, push)
```

The hook skips itself when the push comes from `make bump-patch` (commit
message starts with `chore: bump version`), preventing infinite recursion.

## Deployment

The hook runs on the **registry server** inside the bare repo at
`/opt/git/klawed.git`.

### 1. Copy the hook into place

From this repo (the same repo you already have cloned):

```bash
scp scripts/post-receive-ci.sh root@registry.kasafox.com:/opt/git/klawed.git/hooks/post-receive
```

### 2. Make it executable on the server

```bash
ssh root@registry.kasafox.com "chmod +x /opt/git/klawed.git/hooks/post-receive"
```

### 3. Verify

Push a commit to master and check the CI log:

```bash
ssh root@registry.kasafox.com "tail -f /var/log/klawed-ci.log"
```

## Configuration (optional)

Set these environment variables on the registry server to customise:

| Variable | Default | Description |
|----------|---------|-------------|
| `KLAWED_CI_LOG_DIR` | `/var/log` | Where to write the CI log file |
| `KLAWED_CI_BRANCHES` | `master` | Space-separated list of branches that trigger CI |
| `KLAWED_CI_MAKE_TIMEOUT` | `1800` | Seconds before each make target is killed (0 = no limit) |

Edit `/opt/git/klawed.git/hooks/post-receive` and add exports near the top,
or set them system-wide (e.g. `/etc/environment`).

## Removing CI

To disable the CI hook without losing the script:

```bash
ssh root@registry.kasafox.com "mv /opt/git/klawed.git/hooks/post-receive /opt/git/klawed.git/hooks/post-receive.disabled"
```

To re-enable:

```bash
ssh root@registry.kasafox.com "mv /opt/git/klawed.git/hooks/post-receive.disabled /opt/git/klawed.git/hooks/post-receive"
```
