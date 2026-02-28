#!/usr/bin/env python3
"""
Integration test: interrupt a running tool in sqlite queue mode.

Verifies that when an INTERRUPT arrives while a bash tool (sleep 10) is
mid-execution, klawed:
  1. Kills the running subprocess
  2. Injects a synthetic error tool result (keeps conversation API-valid)
  3. Sends END_AI_TURN
  4. Then processes the next user message normally (the original bug fix)

This mirrors how interactive mode (Ctrl+C) kills the tool thread.
"""

import json
import os
import sqlite3
import subprocess
import sys
import tempfile
import time

DB_PATH = tempfile.mktemp(suffix=".db", prefix="klawed_interrupt_tool_test_")
KLAWED_BIN = os.path.join(os.path.dirname(__file__), "..", "build", "klawed")


def wait_for_db(path, timeout=15):
    deadline = time.time() + timeout
    while time.time() < deadline:
        if os.path.exists(path) and os.path.getsize(path) > 0:
            try:
                con = sqlite3.connect(path, timeout=5)
                con.execute("SELECT 1 FROM messages LIMIT 1")
                con.close()
                return True
            except sqlite3.OperationalError:
                pass
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
            pass
        time.sleep(0.15)


def run_test():
    print(f"\n=== Interrupt-mid-tool integration test ===")
    print(f"DB:     {DB_PATH}")
    print(f"Binary: {KLAWED_BIN}")

    env = os.environ.copy()
    env["KLAWED_LOG_LEVEL"] = "INFO"
    env["KLAWED_SQLITE_POLL_INTERVAL"] = "100"
    env["KLAWED_MAX_TOKENS"] = "256"
    env["KLAWED_BASH_TIMEOUT"] = "60"   # don't let bash timeout kill the sleep before we do

    proc = subprocess.Popen(
        [KLAWED_BIN, "--sqlite-queue", DB_PATH],
        env=env,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
    )
    print(f"  klawed PID: {proc.pid}")

    print("  Waiting for DB to be ready...")
    if not wait_for_db(DB_PATH):
        proc.terminate()
        print("FAIL: DB never became ready")
        sys.exit(1)
    print("  ✓ DB ready")

    last_id = 0

    # ── Turn 1: trigger a long-running bash tool (sleep 10) ──────────────────
    # Ask the model to run sleep 10 — it will call Bash(sleep 10).
    # We wait for the TOOL message (tool is now executing), then interrupt.
    print("\n[1] Sending message that triggers a slow bash tool...")
    send_message(DB_PATH, "TEXT",
                 "Run this bash command and report the output: sleep 10 && echo 'slept'")

    print("\n[2] Waiting for TOOL message (bash tool is now executing)...")
    tool_seen = False
    for mid, mtype, mcontent in poll_messages(DB_PATH, last_id, timeout=30):
        print(f"     ← {mtype}: {str(mcontent)[:80]}")
        last_id = mid
        if mtype == "TOOL":
            tool_seen = True
            break

    if not tool_seen:
        print("FAIL: never saw TOOL message")
        proc.terminate(); sys.exit(1)
    print("  ✓ TOOL seen — bash tool is now executing (sleep 10)")

    # Send INTERRUPT while the tool subprocess is running
    time.sleep(0.5)
    t_interrupt = time.time()
    print("\n[3] Sending INTERRUPT mid-tool...")
    send_message(DB_PATH, "INTERRUPT")

    print("\n[4] Waiting for END_AI_TURN after interrupt (max 15 s)...")
    got_end = False
    got_tool_result = False
    for mid, mtype, mcontent in poll_messages(DB_PATH, last_id, timeout=15):
        print(f"     ← {mtype}: {str(mcontent)[:100]}")
        last_id = mid
        if mtype == "TOOL_RESULT":
            got_tool_result = True
        if mtype == "END_AI_TURN":
            got_end = True
            break

    elapsed = time.time() - t_interrupt
    print(f"  Elapsed since interrupt: {elapsed:.1f}s")

    if not got_end:
        print("FAIL: no END_AI_TURN after mid-tool interrupt")
        proc.terminate(); sys.exit(1)

    # Key assertion: interrupt was fast (well under 10s) — the sleep was killed
    if elapsed > 9.0:
        print(f"FAIL: took {elapsed:.1f}s — sleep was NOT killed, interrupt didn't work")
        proc.terminate(); sys.exit(1)

    print(f"  ✓ END_AI_TURN in {elapsed:.1f}s — subprocess was killed by interrupt")

    # ── Turn 2: follow-up must work (the original bug) ────────────────────────
    time.sleep(0.3)
    print("\n[5] Sending follow-up after mid-tool interrupt...")
    send_message(DB_PATH, "TEXT",
                 "Just run this: echo hello")

    print("\n[6] Waiting for TEXT response to follow-up (max 30 s)...")
    got_text = False
    got_error = False
    got_end2 = False
    for mid, mtype, mcontent in poll_messages(DB_PATH, last_id, timeout=30):
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

    print("\n=== Results ===")
    print(f"  TOOL seen                        : ✓")
    print(f"  Interrupt killed subprocess fast : ✓ ({elapsed:.1f}s < 9s)")
    print(f"  TOOL_RESULT after interrupt      : {'✓' if got_tool_result else '✗ missing (synthetic result not sent?)'}")
    print(f"  END_AI_TURN after interrupt      : ✓")
    print(f"  TEXT response to follow-up       : {'✓' if got_text  else '✗  ← stale interrupt or broken'}")
    print(f"  ERROR received (bad)             : {'✗ yes' if got_error else '✓ none'}")
    print(f"  END_AI_TURN after follow-up      : {'✓' if got_end2  else '✗ missing'}")

    passed = got_tool_result and got_text and not got_error and got_end2
    if passed:
        print("\nPASS ✓  — tool interrupted correctly, follow-up processed normally")
        sys.exit(0)
    else:
        print("\nFAIL ✗  — see above")
        sys.exit(1)


if __name__ == "__main__":
    run_test()
