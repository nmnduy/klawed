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

import base64
import json
import os
import socket
import sys
import tempfile
import time


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
        parsed = json.loads(line)

        # If this is a screenshot response, save the image to a tmp file
        # instead of dumping base64 into the LLM context
        data_url = parsed.get("dataUrl") or parsed.get("data_url")
        if data_url and data_url.startswith("data:image/"):
            header, _, b64data = data_url.partition(",")
            ext = "png" if "png" in header else "jpg"
            fname = os.path.join(
                tempfile.gettempdir(),
                f"klawed_screenshot_{int(time.time())}.{ext}"
            )
            with open(fname, "wb") as f:
                f.write(base64.b64decode(b64data))
            parsed = {"file": fname, "format": ext}

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
