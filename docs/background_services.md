## Background Services

| Service | Frequency | Purpose |
|---------|-----------|---------|
| **`KlawedSandboxService.manageContainerLifecycle()`** | every 10s | **THE MAIN PROBLEM** - Starts/stops Podman containers |
| `ChatMessagePollingService.pollAndSendUnsentMessages()` | every 1s | Polls DB for unsent messages to WebSocket |
| `SQLiteQueuePollingService.pollSQLiteQueues()` | every 1s | Polls SQLite queues from klawed agents |
| `KlawedDbCleanupService.cleanupOldDbFiles()` | daily @ 3AM | Cleans old klawed DB files |
| `ChatDbCleanupService.cleanupOldDbFiles()` | daily @ 3AM | Cleans old chat DB files |
