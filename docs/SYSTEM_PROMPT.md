### Current system prompt structure
```
System Prompt
├─ Environment block <env>
│  ├─ Planning mode status
│  ├─ Working directory
│  ├─ Additional working directories (if any)
│  ├─ Git repo status (Yes/No)
│  ├─ Platform
│  ├─ OS version
│  └─ Today's date
├─ Git status (full git status text, if repo)
├─ SKILLS directory note (if SKILLS/ exists)
│  ├─ Description of SKILLS/ directory purpose
│  ├─ List of top-level files/directories (up to 50 items)
│  └─ "[...]" indicator if more than 50 items exist
└─ System reminder block <system-reminder>
   ├─ “As you answer…” preamble
   ├─ KLAWED.md contents (project instructions)
   └─ “IMPORTANT…” note about relevance
```

---

### Proposed structure (adding “past-but-relevant messages”)
```
System Prompt
├─ Environment block <env>
│  ├─ Planning mode status
│  ├─ Working directory
│  ├─ Additional working directories
│  ├─ Git repo status (Yes/No)
│  ├─ Platform
│  ├─ OS version
│  └─ Today's date
├─ Git status (if repo)
├─ SKILLS directory note (if SKILLS/ exists)
│  ├─ Description of SKILLS/ directory purpose
│  ├─ List of top-level files/directories (up to 50 items)
│  └─ "[...]" indicator if more than 50 items exist
├─ Past-but-relevant messages block <context-retrieval>
│  ├─ Retrieval summary (brief synopsis of retrieved history)
│  ├─ Retrieved snippets (K items: role, timestamp, content excerpt)
│  └─ Optional recent tail (last N turns for continuity)
└─ System reminder block <system-reminder>
   ├─ “As you answer…” preamble
   ├─ KLAWED.md contents (project instructions)
   └─ “IMPORTANT…” note about relevance
```
