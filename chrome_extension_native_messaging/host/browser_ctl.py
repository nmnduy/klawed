#!/usr/bin/env python3
"""
browser_ctl.py — Klawed browser control client

Sends a JSON command to the Go native messaging host via Unix socket
and prints the JSON response.

Usage:
    browser_ctl.py '{"command": "navigate", "params": {"url": "https://example.com"}}'
    browser_ctl.py '{"command": "getPageSource"}'
    browser_ctl.py '{"command": "listTabs"}'

Environment:
    KLAWED_BROWSER_SOCKET  Override socket path (default: /tmp/klawed-browser.sock)
"""

import json
import os
import socket
import sys


def main():
    if len(sys.argv) < 2:
        print(json.dumps({"error": "Usage: browser_ctl.py '<json-command>'"}))
        sys.exit(1)

    sock_path = os.environ.get("KLAWED_BROWSER_SOCKET", "/tmp/klawed-browser.sock")

    try:
        payload = sys.argv[1].strip()
        # Validate JSON input
        json.loads(payload)
    except json.JSONDecodeError as e:
        print(json.dumps({"error": f"Invalid JSON input: {e}"}))
        sys.exit(1)

    try:
        with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as sock:
            sock.settimeout(35)  # 30s host timeout + 5s margin
            sock.connect(sock_path)
            sock.sendall((payload + "\n").encode("utf-8"))

            # Read response line
            buf = b""
            while True:
                chunk = sock.recv(65536)
                if not chunk:
                    break
                buf += chunk
                if b"\n" in buf:
                    break

        line = buf.split(b"\n")[0].decode("utf-8")
        # Pretty-print the response
        parsed = json.loads(line)
        print(json.dumps(parsed, indent=2))

    except FileNotFoundError:
        print(json.dumps({
            "error": f"Socket not found: {sock_path}",
            "hint": "Is the Klawed Browser Controller extension connected in Chrome?"
        }))
        sys.exit(1)
    except ConnectionRefusedError:
        print(json.dumps({
            "error": "Connection refused",
            "hint": "The native host may not be running. Open Chrome with the extension."
        }))
        sys.exit(1)
    except TimeoutError:
        print(json.dumps({"error": "Timeout waiting for browser response (35s)"}))
        sys.exit(1)
    except Exception as e:
        print(json.dumps({"error": str(e)}))
        sys.exit(1)


if __name__ == "__main__":
    main()
