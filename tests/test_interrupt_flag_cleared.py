#!/usr/bin/env python3
"""
Integration test for: interrupt flag not cleared between turns (sqlite queue mode).

Mirrors how interactive mode (Ctrl+C) works: the interrupt fires while klawed
is blocked in an in-flight API call, exactly as the TUI input thread sets
interrupt_requested=1 while the AI worker thread is inside call_api_with_retries.

Scenario:
  1. Start klawed in --sqlite-queue daemon mode (klawed creates its own DB)
  2. Send a TEXT message
  3. As soon as we see the API_CALL message (klawed is now in-flight), send INTERRUPT
  4. Wait for END_AI_TURN confirming the interrupt was handled
  5. Send a follow-up TEXT message
  6. Assert we get a real TEXT response + END_AI_TURN
     (before the fix: stale interrupt_requested fires the pre-call check
      at api_client.c:266 and the turn silently fails with only END_AI_TURN)
"""

import json
import os
import sqlite3
import subprocess
import sys
import tempfile
import time

# Let klawed create the DB — we just choose the path
DB_PATH = tempfile.mktemp(suffix=".db", prefix="klawed_interrupt_test_")
KLAWED_BIN = os.path.join(os.path.dirname(__file__), "..", "build", "klawed")


# ── helpers ───────────────────────────────────────────────────────────────────

def wait_for_db(path, timeout=10):
    """Wait until klawed has created and initialised the DB (table exists)."""
    deadline = time.time() + timeout
    while time.time() < deadline:
        if os.path.exists(path) and os.path.getsize(path) > 0:
            try:
                con = sqlite3.connect(path, timeout=5)
                con.row_factory = sqlite3.Row
                con.execute("SELECT 1 FROM messages LIMIT 1")
                con.close()
                return True
            except sqlite3.OperationalError:
                pass  # table not yet created
        time.sleep(0.2)
    return False


def db_connect(path):
    con = sqlite3.connect(path, timeout=10)
    con.row_factory = sqlite3.Row
    return con


def send_message(path, msg_type, content=None):
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
    snippet = (content or "")[:80]
    print(f"  [client→klawed] {msg_type}: {snippet}{'...' if len(content or '') > 80 else ''}")


def poll_messages(path, after_id, timeout):
    """Yield (id, messageType, content) for klawed→client messages after after_id."""
    deadline = time.time() + timeout
    seen = set()
    while time.time() < deadline:
        try:
            con = db_connect(path)
            rows = con.execute(
                "SELECT id, message FROM messages "
                "WHERE sender='klawed' AND receiver='client' AND id > ? ORDER BY id",
                (after_id,)
            ).fetchall()
            con.close()
            for row in rows:
                if row["id"] not in seen:
                    seen.add(row["id"])
                    try:
                        p = json.loads(row["message"])
                    except json.JSONDecodeError:
                        continue
                    yield row["id"], p.get("messageType", ""), p.get("content", "")
        except sqlite3.OperationalError:
            pass  # DB not ready yet
        time.sleep(0.15)


# ── main test ─────────────────────────────────────────────────────────────────

def run_test():
    print(f"\n=== Interrupt-flag-cleared integration test ===")
    print(f"DB:     {DB_PATH}")
    print(f"Binary: {KLAWED_BIN}")

    env = os.environ.copy()
    env["KLAWED_LOG_LEVEL"] = "INFO"
    env["KLAWED_SQLITE_POLL_INTERVAL"] = "100"
    env["KLAWED_MAX_TOKENS"] = "256"

    # klawed creates its own DB — do NOT pre-create it
    proc = subprocess.Popen(
        [KLAWED_BIN, "--sqlite-queue", DB_PATH],
        env=env,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
    )
    print(f"  klawed PID: {proc.pid}")

    print("  Waiting for klawed DB to be ready...")
    if not wait_for_db(DB_PATH, timeout=15):
        out, _ = proc.communicate()
        print(f"FAIL: klawed DB never became ready\n{out.decode()[:2000]}")
        proc.terminate()
        sys.exit(1)
    print("  ✓ DB ready")

    if proc.poll() is not None:
        out, _ = proc.communicate()
        print(f"FAIL: klawed exited early (rc={proc.returncode})\n{out.decode()[:2000]}")
        sys.exit(1)

    last_id = 0

    # ── Turn 1: send a message, interrupt as soon as API_CALL fires ──────────
    print("\n[1] Sending first message...")
    send_message(DB_PATH, "TEXT",
                 "Please explore the codebase and describe what klawed does. "
                 "Check multiple files and be thorough.")

    print("\n[2] Waiting for API_CALL (klawed is now in-flight)...")
    interrupted = False
    for mid, mtype, mcontent in poll_messages(DB_PATH, last_id, timeout=30):
        print(f"     ← {mtype}")
        last_id = mid
        if mtype == "API_CALL":
            # Interrupt fires while the HTTP request is in-flight — mirrors Ctrl+C in TUI
            print("\n[3] API_CALL seen — sending INTERRUPT (mid-flight, mirrors Ctrl+C)...")
            send_message(DB_PATH, "INTERRUPT")
            interrupted = True
            break

    if not interrupted:
        print("FAIL: never saw API_CALL — klawed didn't start processing")
        proc.terminate(); sys.exit(1)

    print("\n[4] Waiting for END_AI_TURN after interrupt (max 20 s)...")
    got_end = False
    for mid, mtype, mcontent in poll_messages(DB_PATH, last_id, timeout=20):
        print(f"     ← {mtype}: {str(mcontent)[:80]}")
        last_id = mid
        if mtype == "END_AI_TURN":
            got_end = True
            break

    if not got_end:
        print("FAIL: no END_AI_TURN after interrupt within 20 s")
        proc.terminate(); sys.exit(1)
    print("  ✓ END_AI_TURN received — interrupt handled correctly")

    # ── Turn 2: follow-up — this is what the bug broke ───────────────────────
    time.sleep(0.3)
    print("\n[5] Sending follow-up message after interrupt...")
    # Keep the task simple and single-tool so it completes quickly and reliably
    send_message(DB_PATH, "TEXT",
                 "Just run this one command and tell me the result: find src/ -name '*.c' | wc -l")

    print("\n[6] Waiting for TEXT response to follow-up (max 60 s)...")
    print("    (bug = END_AI_TURN with no TEXT: stale interrupt fires pre-call check)")
    got_text = False
    got_error = False
    got_end2 = False

    for mid, mtype, mcontent in poll_messages(DB_PATH, last_id, timeout=60):
        print(f"     ← {mtype}: {str(mcontent)[:120]}")
        last_id = mid
        if mtype == "TEXT" and mcontent:
            got_text = True
        if mtype == "ERROR":
            got_error = True
        if mtype == "END_AI_TURN":
            got_end2 = True
            break

    proc.terminate()
    try:
        proc.wait(timeout=5)
    except subprocess.TimeoutExpired:
        proc.kill()

    try:
        os.unlink(DB_PATH)
    except OSError:
        pass

    # ── Verdict ───────────────────────────────────────────────────────────────
    print("\n=== Results ===")
    print(f"  API_CALL seen, INTERRUPT sent mid-flight : ✓")
    print(f"  END_AI_TURN after interrupt              : ✓")
    print(f"  TEXT response to follow-up               : {'✓' if got_text  else '✗  ← stale interrupt bug'}")
    print(f"  ERROR received (bad)                     : {'✗ yes' if got_error else '✓ none'}")
    print(f"  END_AI_TURN after follow-up              : {'✓' if got_end2  else '✗ missing'}")

    passed = got_text and not got_error and got_end2
    if passed:
        print("\nPASS ✓  — interrupt flag cleared; follow-up processed normally")
        sys.exit(0)
    else:
        if got_end2 and not got_text:
            print("\nFAIL ✗  — END_AI_TURN with no TEXT: stale interrupt flag bug still present")
        elif not got_end2:
            print("\nFAIL ✗  — follow-up turn never completed (timeout)")
        else:
            print("\nFAIL ✗  — unexpected result")
        sys.exit(1)


if __name__ == "__main__":
    run_test()
