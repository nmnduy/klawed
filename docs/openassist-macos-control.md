# OpenAssist macOS Control Implementation

This document summarizes how [OpenAssist](https://github.com/manikv12/OpenAssist) implements macOS control and automation.

## Overview

OpenAssist controls macOS through three complementary automation layers, all orchestrated by a central `LocalAutomationHelper` actor that bridges the assistant’s reasoning to the system.

---

## 1. Browser Use (`browser_use`)

Controls your **real, signed-in browser profile** (Chrome, Brave, or Edge) rather than a sandboxed automation browser.

- Uses **AppleScript** (`tell application "Chrome"…`) and **Accessibility APIs** to activate the browser, open URLs, and read the current tab.
- Reuses your actual cookies and logins, so the assistant can interact with sites you’re already signed into.
- If a site requires a fresh login, the assistant pauses and asks you to sign in manually before proceeding.

---

## 2. Direct App Actions (`app_action`)

Structured, app-specific automation for a fixed set of supported Mac apps:

- **Finder, Terminal, Calendar, System Settings**: controlled via **AppleScript** (e.g., `reveal`, `run command`, `open settings pane`).
- **Reminders, Contacts, Notes, Messages**: controlled via **native frameworks** (EventKit, `CNContactStore`, direct SQLite reads) rather than AppleScript. This avoids macOS privacy consent dialogs that hang when scripting those apps.
- Attempts to automate privacy-protected apps (Mail, Photos, Safari, Music, etc.) are blocked because macOS restricts them.

---

## 3. Screenshot-Based Computer Use (`computer_use` / `computer_batch`)

A generic, visual “agent” mode where the assistant **sees your screen** and acts via mouse/keyboard simulation.

- **Observation**: Captures the display using `CGDisplayCreateImage` and encodes it as a base64 PNG data URL for the AI model.
- **Action execution**: Simulates input using **Core Graphics event taps** (`CGEvent`):
  - **Mouse**: `click`, `double_click`, `right_click`, `middle_click`, `drag`, `scroll`, `move`, `mouse_down/up`
  - **Keyboard**: `keypress`, `hold_key`, `type` (uses `CGEvent` with unicode strings and virtual key codes)
  - **Other**: `open_application`, `clipboard_read/write`, `wait`, `switch_display`, `zoom` (region inspect)
- **Coordinate mapping**: Actions use screenshot-pixel coordinates mapped to screen points via captured snapshot metadata.

---

## 4. Semantic UI Automation (`ui_inspect`, `ui_click`, `ui_type`, `ui_press_key`)

An alternative to raw coordinate clicking that uses the **macOS Accessibility API** (`AXUIElement`) to inspect and interact with UI elements by label or role.

- `ui_inspect`: Lists visible accessibility elements (buttons, fields, etc.) with labels, roles, and bounds.
- `ui_click` / `ui_type`: Targets elements by semantic name rather than raw `(x, y)` coordinates.
- Requires **Accessibility permission** (`AXIsProcessTrusted()`).

---

## 5. Permissions & Security

The helper checks for required permissions before executing and throws descriptive errors if any are missing.

| Permission | Purpose |
|---|---|
| **Accessibility** | Clicking, typing, UI inspection (`AXIsProcessTrusted`) |
| **Screen Recording** | Capturing the desktop (`CGPreflightScreenCaptureAccess`) |
| **Automation / Apple Events** | Browser and app scripting |
| **Full Disk Access** | Some native reads (e.g., Notes SQLite) |

- **Approval flow**: The assistant asks for user approval before executing important actions.
- **Privacy blocking**: Direct app actions explicitly refuse to script apps like Mail, Photos, or Safari because macOS hangs on consent dialogs for them.

---

## 6. Local Automation API Server

`AutomationAPICoordinator` optionally exposes these capabilities over a **localhost HTTP API** (`127.0.0.1`), so external tools or scheduled jobs can trigger the same macOS control pipeline.

---

## Key Source Files

| File | Responsibility |
|---|---|
| `Sources/OpenAssist/Services/LocalAutomationHelper.swift` | Core executor (CGEvent, AppleScript, screenshots) |
| `Sources/OpenAssist/Assistant/AssistantComputerUseService.swift` | Screenshot-based agent loop |
| `Sources/OpenAssist/Assistant/AssistantAppActionService.swift` | Structured app actions |
| `Sources/OpenAssist/Assistant/AssistantBrowserUseService.swift` | Browser profile automation |
| `Sources/OpenAssist/Services/AssistantAccessibilityAutomationService.swift` | Semantic `AXUIElement` automation |
| `Sources/OpenAssist/Services/AutomationAPICoordinator.swift` | Localhost API server for external triggers |
