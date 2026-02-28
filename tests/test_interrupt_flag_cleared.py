#!/usr/bin/env python3
"""
Integration test for: interrupt flag not cleared between turns (sqlite queue mode).

Scenario:
  1. Start klawed in --sqlite-queue daemon mode
  2. Send a TEXT message that triggers a long-running tool (sleep)
  3. While klawed is mid-turn (in the sleep), send an INTERRUPT
  4. Wait for END_AI_TURN (confirming the interrupt was handled)
  5. Send a follow-up TEXT message
  6. Verify klawed processes it and sends a TEXT response + END_AI_TURN
     (not an immediate ERROR/silent failure due to stale interrupt flag)

Before the fix, step 6 would fail: END_AI_TURN with no TEXT, because
call_api_with_retries' pre-call interrupt check at api_client.c:266 fired
on the stale interrupt_requested flag.
"""

import json
import os
import sqlite3
import subprocess
import sys
import tempfile
import time

DB_PATH = tempfile.mktemp(suffix=".db", prefix="klawed_interrupt_test_")
KLAWED_BIN = os.path.join(os.path.dirname(__file__), "..", "build", "klawed")


# ── helpers ──────────────────────────────────────────────────────────────────

def db_connect(path):
    con = sqlite3.connect(path, timeout=10)
    con.row_factory = sqlite3.Row
    return con


def ensure_schema(path):
    con = db_connect(path)
    con.execute("""
        CREATE TABLE IF NOT EXISTS messages (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            sender TEXT NOT NULL,
            receiver TEXT NOT NULL,
            message TEXT NOT NULL,
            sent INTEGER DEFAULT 0,
            created_at INTEGER DEFAULT (strftime('%s','now')),
            updated_at INTEGER DEFAULT (strftime('%s','now'))
        )
    """)
    con.execute("CREATE INDEX IF NOT EXISTS idx_messages_sender ON messages(sender, sent)")
    con.execute("CREATE INDEX IF NOT EXISTS idx_messages_receiver ON messages(receiver, sent)")
    con.commit()
    con.close()


def send_message(path, msg_type, content=None):
    """Send a message from client to klawed using the correct messageType field."""
    payload = {"messageType": msg_type}
    if content is not None:
        payload["content"] = content
    con = db_connect(path)
    con.execute(
        "INSERT INTO messages (sender, receiver, message, sent) VALUES (?, ?, ?, 0)",
        ("client", "klawed", json.dumps(payload))
    )
    con.commit()
    con.close()
    snippet = content[:80] if content else ""
    print(f"  [client→klawed] {msg_type}: {snippet}{'...' if snippet and len(snippet) >= 80 else ''}")


def get_klawed_messages(path, after_id):
    """Fetch all klawed→client messages after after_id."""
    con = db_connect(path)
    rows = con.execute(
        "SELECT id, message FROM messages WHERE sender='klawed' AND receiver='client' AND id > ? ORDER BY id",
        (after_id,)
    ).fetchall()
    con.close()
    results = []
    for row in rows:
        try:
            payload = json.loads(row["message"])
        except json.JSONDecodeError:
            continue
        results.append((row["id"], payload.get("messageType", ""), payload.get("content", "")))
    return results


def wait_for_event(path, after_id, timeout, event_types, label):
    """Poll until one of event_types is received. Returns (last_id, found_type)."""
    deadline = time.time() + timeout
    last_id = after_id
    while time.time() < deadline:
        msgs = get_klawed_messages(path, last_id)
        for mid, mtype, mcontent in msgs:
            print(f"     ← {mtype}: {str(mcontent)[:100]}")
            last_id = mid
            if mtype in event_types:
                return last_id, mtype
        time.sleep(0.3)
    print(f"  TIMEOUT: {label} — did not see {event_types} within {timeout}s")
    return last_id, None


def wait_for_turn_complete(path, after_id, timeout):
    """Poll until END_AI_TURN. Returns (last_id, got_text, got_error, got_end)."""
    deadline = time.time() + timeout
    last_id = after_id
    got_text = False
    got_error = False
    got_end = False
    while time.time() < deadline:
        msgs = get_klawed_messages(path, last_id)
        for mid, mtype, mcontent in msgs:
            print(f"     ← {mtype}: {str(mcontent)[:120]}")
            last_id = mid
            if mtype == "TEXT" and mcontent:
                got_text = True
            if mtype == "ERROR":
                got_error = True
            if mtype == "END_AI_TURN":
                got_end = True
                return last_id, got_text, got_error, got_end
        time.sleep(0.3)
    return last_id, got_text, got_error, got_end


# ── main test ─────────────────────────────────────────────────────────────────

def run_test():
    print(f"\n=== Interrupt-flag-cleared integration test ===")
    print(f"DB:     {DB_PATH}")
    print(f"Binary: {KLAWED_BIN}")

    ensure_schema(DB_PATH)

    env = os.environ.copy()
    env["KLAWED_LOG_LEVEL"] = "INFO"
    env["KLAWED_SQLITE_POLL_INTERVAL"] = "200"
    env["KLAWED_MAX_TOKENS"] = "256"   # short responses = faster turns

    proc = subprocess.Popen(
        [KLAWED_BIN, "--sqlite-queue", DB_PATH],
        env=env,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
    )
    print(f"  klawed PID: {proc.pid}")

    # Give daemon time to initialise
    time.sleep(2)
    if proc.poll() is not None:
        out, _ = proc.communicate()
        print(f"FAIL: klawed exited early (rc={proc.returncode})\n{out.decode()[:2000]}")
        sys.exit(1)

    last_id = 0

    # ── Step 1: send a task with a long sleep so we can interrupt mid-turn ──
    # We ask klawed to sleep for 30 seconds — plenty of time to interrupt it.
    print("\n[1] Sending initial message (30-second sleep to ensure we're mid-turn)...")
    send_message(
        DB_PATH, "TEXT",
        "Please run this bash command: sleep 30 && echo done. Wait for it to finish before responding."
    )

    # ── Step 2: wait for the TOOL message (confirming klawed started the sleep)
    # then send the interrupt
    print("\n[2] Waiting for TOOL message (klawed started the sleep)...")
    last_id, found = wait_for_event(DB_PATH, last_id, 30, {"TOOL"}, "TOOL start")
    if not found:
        print("FAIL: klawed never started the tool — no TOOL message")
        proc.terminate(); sys.exit(1)
    print("  ✓ TOOL message seen — klawed is now mid-turn (sleeping)")

    # Small gap then interrupt
    time.sleep(1)
    print("\n[3] Sending INTERRUPT while tool is running...")
    send_message(DB_PATH, "INTERRUPT")

    # ── Step 3: wait for END_AI_TURN confirming interrupt was handled ────────
    print("\n[4] Waiting for END_AI_TURN after interrupt (max 20 s)...")
    last_id, found = wait_for_event(DB_PATH, last_id, 20, {"END_AI_TURN"}, "post-interrupt END_AI_TURN")

    if not found:
        print("FAIL: no END_AI_TURN after interrupt")
        proc.terminate(); sys.exit(1)
    print("  ✓ END_AI_TURN received — interrupt handled correctly")

    # ── Step 4: send follow-up message ──────────────────────────────────────
    time.sleep(0.5)
    print("\n[5] Sending follow-up message after interrupt...")
    send_message(
        DB_PATH, "TEXT",
        "Great, now please just tell me how many .c files are in src/ by running: ls src/*.c | wc -l"
    )

    # ── Step 5: verify a real TEXT response arrives (the key assertion) ───────
    print("\n[6] Waiting for TEXT response to follow-up (max 60 s)...")
    print("    (if bug is present: END_AI_TURN arrives with NO TEXT — stale interrupt fires)")
    last_id, got_text, got_error, got_end = wait_for_turn_complete(DB_PATH, last_id, 60)

    proc.terminate()
    try:
        proc.wait(timeout=5)
    except subprocess.TimeoutExpired:
        proc.kill()

    # Cleanup
    for f in [DB_PATH]:
        try:
            os.unlink(f)
        except OSError:
            pass

    # ── Verdict ──────────────────────────────────────────────────────────────
    print("\n=== Results ===")
    print(f"  TOOL seen during first turn      : ✓")
    print(f"  END_AI_TURN after interrupt      : ✓")
    print(f"  TEXT response to follow-up       : {'✓' if got_text    else '✗  ← BUG: stale interrupt consumed the turn'}")
    print(f"  ERROR response received (bad)    : {'✗ yes' if got_error else '✓ none'}")
    print(f"  END_AI_TURN after follow-up      : {'✓' if got_end     else '✗ MISSING'}")

    passed = got_text and not got_error and got_end
    if passed:
        print("\nPASS ✓  — interrupt flag cleared correctly; follow-up processed normally")
        sys.exit(0)
    else:
        if got_end and not got_text:
            print("\nFAIL ✗  — got END_AI_TURN but no TEXT: stale interrupt flag bug still present")
        elif not got_end:
            print("\nFAIL ✗  — follow-up turn never completed (timeout)")
        else:
            print("\nFAIL ✗  — unexpected result (see above)")
        sys.exit(1)


if __name__ == "__main__":
    run_test()
