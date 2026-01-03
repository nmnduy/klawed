- [ ] search results are not highlihted when using '/' and '?' in normal mode
- [ ] cant clear search?
- [x] token count should show total token, not prompt
- [ ] /help doesn't show shit
- [ ] /bash to run any bash commands then return to this file
- [ ] this simple read fails?
```sh
[Read] openai-responses.md:1-400

[Error] Read failed: Failed to read file: No such file or directory
```
- [x] bash timeout unusually long is not 120 seconds

- [x] in normal mode, allow searching forward and backward using '/' and '?' respectively
    - Implemented search functionality in TUI
    - Added TUI_MODE_SEARCH mode
    - '/' enters forward search mode, '?' enters backward search mode
    - 'n' repeats search in same direction, 'N' repeats in opposite direction
    - Search is case-insensitive and wraps around
    - Status messages show search results
- [ ] app unable to continue after canceling a long running task. submitting new instructions doesn't send more api calls.
```sh
[...]
[Assistant] Now let me restart the Quarkus server to pick up all changes. First, let me kill the current process:

[Bash] kill 43231 43349 2>/dev/null; sleep 3

[Assistant] Now let me start the server in the background:

[Bash] mvn quarkus:dev > /tmp/quarkus.log 2>&1 &

[User] hey
[User] hey
[User] hey
[User] hey
Instruction queued (4/16 pending)
>>>
```

- [x] doesn't auto-scroll at 100% scroll position. should always auto-scroll at that position
    - Fixed: Changed auto-scroll condition from `scroll_offset >= max_scroll` to `scroll_offset >= max_scroll - 1` (98-100% range)
    - Updated both occurrences in src/tui.c with proper logging
    - Added comprehensive unit tests in tests/test_tui_auto_scroll.c
    - Added test target to Makefile: `make test-tui-auto-scroll`

- [x] zsh: segmentation fault  ./build/klawed --zmq-client tcp://127.0.0.1:5555

- [x] Investigate session resume crash (pointer being freed was not allocated)
  - Root cause: Potential double-free or memory corruption in free_internal_message
  - Fix: Added safety checks in free_internal_message() and session loading code
  - Changes: src/openai_messages.c, src/session.c
  - Test with: ./test_session_resume.sh

- [x] ZMQ client-daemon communication issue
  - Issue: Client runs without daemon, messages resent without ACKs
  - Root cause: No daemon running to receive messages and send ACKs
  - Solution: Run daemon first: `./build/klawed --zmq tcp://127.0.0.1:5555`
  - Then run client: `./build/klawed --zmq-client tcp://127.0.0.1:5555`
  - Test with: ./test_zmq_setup.sh
  - Documentation: ZMQ_README.md 

- [ ] subagents
    - [ ] explore: explore code base or current directtory and write a concise markdown with findings
    - [ ] oracle
        - can think hard
        - has web search

- [ ] make sure every 'Bash' command is started with a 'timeout'
- [ ] can open vim and when exits go back to the bot
- [ ] $ klawed -r sess_1766925100_140aa178
... BUG: session_load_from_db: checking message 32
DEBUG: session_load_from_db: checking message 31
DEBUG: session_load_from_db: checking message 30
DEBUG: session_load_from_db: checking message 29
DEBUG: session_load_from_db: checking message 28
DEBUG: session_load_from_db: checking message 27
DEBUG: session_load_from_db: checking message 26
DEBUG: session_load_from_db: checking message 25
DEBUG: session_load_from_db: checking message 24
DEBUG: session_load_from_db: checking message 23
DEBUG: session_load_from_db: checking message 22
DEBUG: session_load_from_db: checking message 21
DEBUG: session_load_from_db: checking message 20
DEBUG: session_load_from_db: checking message 19
DEBUG: session_load_from_db: checking message 18
DEBUG: session_load_from_db: checking message 17
DEBUG: session_load_from_db: checking message 16
DEBUG: session_load_from_db: checking message 15
DEBUG: session_load_from_db: checking message 14
DEBUG: session_load_from_db: checking message 13
DEBUG: session_load_from_db: checking message 12
DEBUG: session_load_from_db: checking message 11
DEBUG: session_load_from_db: checking message 10
DEBUG: session_load_from_db: checking message 9
DEBUG: session_load_from_db: checking message 8
DEBUG: session_load_from_db: checking message 7
DEBUG: session_load_from_db: checking message 6
DEBUG: session_load_from_db: checking message 5
DEBUG: session_load_from_db: checking message 4
DEBUG: session_load_from_db: checking message 3
DEBUG: session_load_from_db: checking message 2
DEBUG: session_load_from_db: checking message 1
DEBUG: session_load_from_db: found user message at index 1
DEBUG: session_load_from_db: last_user_message = 0x60000142e400
DEBUG: session_load_from_db: extracting content
DEBUG: session_load_from_db: content = 0x60000142cdc0
DEBUG: session_load_from_db: checking content type
DEBUG: session_load_from_db: content is string, adding user message
DEBUG: session_load_from_db: user message added
DEBUG: session_load_from_db: request deleted
DEBUG: session_load_from_db: parsing response JSON
DEBUG: session_load_from_db: response = 0x60000142be40
DEBUG: session_load_from_db: calling parse_openai_response
DEBUG: session_load_from_db: parse_openai_response returned, content_count = 2
DEBUG: session_load_from_db: adding assistant message to conversation
DEBUG: session_load_from_db: locking conversation state
DEBUG: session_load_from_db: locked, state->count = 79
DEBUG: session_load_from_db: assigning message to array
DEBUG: session_load_from_db: message assigned, new count = 80
DEBUG: session_load_from_db: got row 41
DEBUG: session_load_from_db: getting column data
DEBUG: session_load_from_db: got column data
DEBUG: session_load_from_db: checking column completeness
DEBUG: session_load_from_db: checking status
DEBUG: session_load_from_db: status OK
DEBUG: session_load_from_db: parsing request JSON
DEBUG: session_load_from_db: cJSON_Parse returned 0x60000142be40
DEBUG: session_load_from_db: request parsed successfully
DEBUG: session_load_from_db: getting messages array
DEBUG: session_load_from_db: messages = 0x60000142ba00
DEBUG: session_load_from_db: messages is array, size = 82
DEBUG: session_load_from_db: finding last user message
DEBUG: session_load_from_db: msg_count = 82
DEBUG: session_load_from_db: checking message 81
DEBUG: session_load_from_db: checking message 80
DEBUG: session_load_from_db: checking message 79
DEBUG: session_load_from_db: checking message 78
DEBUG: session_load_from_db: checking message 77
DEBUG: session_load_from_db: checking message 76
DEBUG: session_load_from_db: checking message 75
DEBUG: session_load_from_db: checking message 74
DEBUG: session_load_from_db: checking message 73
DEBUG: session_load_from_db: checking message 72
DEBUG: session_load_from_db: checking message 71
DEBUG: session_load_from_db: checking message 70
DEBUG: session_load_from_db: checking message 69
DEBUG: session_load_from_db: checking message 68
DEBUG: session_load_from_db: checking message 67
DEBUG: session_load_from_db: checking message 66
DEBUG: session_load_from_db: checking message 65
DEBUG: session_load_from_db: checking message 64
DEBUG: session_load_from_db: checking message 63
DEBUG: session_load_from_db: checking message 62
DEBUG: session_load_from_db: checking message 61
DEBUG: session_load_from_db: checking message 60
DEBUG: session_load_from_db: checking message 59
DEBUG: session_load_from_db: checking message 58
DEBUG: session_load_from_db: checking message 57
DEBUG: session_load_from_db: checking message 56
DEBUG: session_load_from_db: checking message 55
DEBUG: session_load_from_db: checking message 54
DEBUG: session_load_from_db: checking message 53
DEBUG: session_load_from_db: checking message 52
DEBUG: session_load_from_db: checking message 51
DEBUG: session_load_from_db: checking message 50
DEBUG: session_load_from_db: checking message 49
DEBUG: session_load_from_db: checking message 48
DEBUG: session_load_from_db: checking message 47
DEBUG: session_load_from_db: checking message 46
DEBUG: session_load_from_db: checking message 45
DEBUG: session_load_from_db: checking message 44
DEBUG: session_load_from_db: checking message 43
DEBUG: session_load_from_db: checking message 42
DEBUG: session_load_from_db: checking message 41
DEBUG: session_load_from_db: checking message 40
DEBUG: session_load_from_db: checking message 39
DEBUG: session_load_from_db: checking message 38
DEBUG: session_load_from_db: checking message 37
DEBUG: session_load_from_db: checking message 36
DEBUG: session_load_from_db: checking message 35
DEBUG: session_load_from_db: checking message 34
DEBUG: session_load_from_db: checking message 33
DEBUG: session_load_from_db: checking message 32
DEBUG: session_load_from_db: checking message 31
DEBUG: session_load_from_db: checking message 30
DEBUG: session_load_from_db: checking message 29
DEBUG: session_load_from_db: checking message 28
DEBUG: session_load_from_db: checking message 27
DEBUG: session_load_from_db: checking message 26
DEBUG: session_load_from_db: checking message 25
DEBUG: session_load_from_db: checking message 24
DEBUG: session_load_from_db: checking message 23
DEBUG: session_load_from_db: checking message 22
DEBUG: session_load_from_db: checking message 21
DEBUG: session_load_from_db: checking message 20
DEBUG: session_load_from_db: checking message 19
DEBUG: session_load_from_db: checking message 18
DEBUG: session_load_from_db: checking message 17
DEBUG: session_load_from_db: checking message 16
DEBUG: session_load_from_db: checking message 15
DEBUG: session_load_from_db: checking message 14
DEBUG: session_load_from_db: checking message 13
DEBUG: session_load_from_db: checking message 12
DEBUG: session_load_from_db: checking message 11
DEBUG: session_load_from_db: checking message 10
DEBUG: session_load_from_db: checking message 9
DEBUG: session_load_from_db: checking message 8
DEBUG: session_load_from_db: checking message 7
DEBUG: session_load_from_db: checking message 6
DEBUG: session_load_from_db: checking message 5
DEBUG: session_load_from_db: checking message 4
DEBUG: session_load_from_db: checking message 3
DEBUG: session_load_from_db: checking message 2
DEBUG: session_load_from_db: checking message 1
DEBUG: session_load_from_db: found user message at index 1
DEBUG: session_load_from_db: last_user_message = 0x60000142b900
DEBUG: session_load_from_db: extracting content
DEBUG: session_load_from_db: content = 0x60000142bdc0
DEBUG: session_load_from_db: checking content type
DEBUG: session_load_from_db: content is string, adding user message
DEBUG: session_load_from_db: user message added
DEBUG: session_load_from_db: request deleted
DEBUG: session_load_from_db: parsing response JSON
DEBUG: session_load_from_db: response = 0x60000142f880
DEBUG: session_load_from_db: calling parse_openai_response
DEBUG: session_load_from_db: parse_openai_response returned, content_count = 2
DEBUG: session_load_from_db: adding assistant message to conversation
DEBUG: session_load_from_db: locking conversation state
DEBUG: session_load_from_db: locked, state->count = 81
DEBUG: session_load_from_db: assigning message to array
DEBUG: session_load_from_db: message assigned, new count = 82
DEBUG: session_load_from_db: got row 42
DEBUG: session_load_from_db: getting column data
DEBUG: session_load_from_db: got column data
DEBUG: session_load_from_db: checking column completeness
DEBUG: session_load_from_db: checking status
DEBUG: session_load_from_db: status OK
DEBUG: session_load_from_db: parsing request JSON
DEBUG: session_load_from_db: cJSON_Parse returned 0x60000142f880
DEBUG: session_load_from_db: request parsed successfully
DEBUG: session_load_from_db: getting messages array
DEBUG: session_load_from_db: messages = 0x60000142c740
DEBUG: session_load_from_db: messages is array, size = 84
DEBUG: session_load_from_db: finding last user message
DEBUG: session_load_from_db: msg_count = 84
DEBUG: session_load_from_db: checking message 83
DEBUG: session_load_from_db: checking message 82
DEBUG: session_load_from_db: checking message 81
DEBUG: session_load_from_db: checking message 80
DEBUG: session_load_from_db: checking message 79
DEBUG: session_load_from_db: checking message 78
DEBUG: session_load_from_db: checking message 77
DEBUG: session_load_from_db: checking message 76
DEBUG: session_load_from_db: checking message 75
DEBUG: session_load_from_db: checking message 74
DEBUG: session_load_from_db: checking message 73
DEBUG: session_load_from_db: checking message 72
DEBUG: session_load_from_db: checking message 71
DEBUG: session_load_from_db: checking message 70
DEBUG: session_load_from_db: checking message 69
DEBUG: session_load_from_db: checking message 68
DEBUG: session_load_from_db: checking message 67
DEBUG: session_load_from_db: checking message 66
DEBUG: session_load_from_db: checking message 65
DEBUG: session_load_from_db: checking message 64
DEBUG: session_load_from_db: checking message 63
DEBUG: session_load_from_db: checking message 62
DEBUG: session_load_from_db: checking message 61
DEBUG: session_load_from_db: checking message 60
DEBUG: session_load_from_db: checking message 59
DEBUG: session_load_from_db: checking message 58
DEBUG: session_load_from_db: checking message 57
DEBUG: session_load_from_db: checking message 56
DEBUG: session_load_from_db: checking message 55
DEBUG: session_load_from_db: checking message 54
DEBUG: session_load_from_db: checking message 53
DEBUG: session_load_from_db: checking message 52
DEBUG: session_load_from_db: checking message 51
DEBUG: session_load_from_db: checking message 50
DEBUG: session_load_from_db: checking message 49
DEBUG: session_load_from_db: checking message 48
DEBUG: session_load_from_db: checking message 47
DEBUG: session_load_from_db: checking message 46
DEBUG: session_load_from_db: checking message 45
DEBUG: session_load_from_db: checking message 44
DEBUG: session_load_from_db: checking message 43
DEBUG: session_load_from_db: checking message 42
DEBUG: session_load_from_db: checking message 41
DEBUG: session_load_from_db: checking message 40
DEBUG: session_load_from_db: checking message 39
DEBUG: session_load_from_db: checking message 38
DEBUG: session_load_from_db: checking message 37
DEBUG: session_load_from_db: checking message 36
DEBUG: session_load_from_db: checking message 35
DEBUG: session_load_from_db: checking message 34
DEBUG: session_load_from_db: checking message 33
DEBUG: session_load_from_db: checking message 32
DEBUG: session_load_from_db: checking message 31
DEBUG: session_load_from_db: checking message 30
DEBUG: session_load_from_db: checking message 29
DEBUG: session_load_from_db: checking message 28
DEBUG: session_load_from_db: checking message 27
DEBUG: session_load_from_db: checking message 26
DEBUG: session_load_from_db: checking message 25
DEBUG: session_load_from_db: checking message 24
DEBUG: session_load_from_db: checking message 23
DEBUG: session_load_from_db: checking message 22
DEBUG: session_load_from_db: checking message 21
DEBUG: session_load_from_db: checking message 20
DEBUG: session_load_from_db: checking message 19
DEBUG: session_load_from_db: checking message 18
DEBUG: session_load_from_db: checking message 17
DEBUG: session_load_from_db: checking message 16
DEBUG: session_load_from_db: checking message 15
DEBUG: session_load_from_db: checking message 14
DEBUG: session_load_from_db: checking message 13
DEBUG: session_load_from_db: checking message 12
DEBUG: session_load_from_db: checking message 11
DEBUG: session_load_from_db: checking message 10
DEBUG: session_load_from_db: checking message 9
DEBUG: session_load_from_db: checking message 8
DEBUG: session_load_from_db: checking message 7
DEBUG: session_load_from_db: checking message 6
DEBUG: session_load_from_db: checking message 5
DEBUG: session_load_from_db: checking message 4
DEBUG: session_load_from_db: checking message 3
DEBUG: session_load_from_db: checking message 2
DEBUG: session_load_from_db: checking message 1
DEBUG: session_load_from_db: found user message at index 1
DEBUG: session_load_from_db: last_user_message = 0x60000142fc00
DEBUG: session_load_from_db: extracting content
DEBUG: session_load_from_db: content = 0x60000142f7c0
DEBUG: session_load_from_db: checking content type
DEBUG: session_load_from_db: content is string, adding user message
DEBUG: session_load_from_db: user message added
DEBUG: session_load_from_db: request deleted
DEBUG: session_load_from_db: parsing response JSON
DEBUG: session_load_from_db: response = 0x600001428040
DEBUG: session_load_from_db: calling parse_openai_response
DEBUG: session_load_from_db: parse_openai_response returned, content_count = 2
DEBUG: session_load_from_db: adding assistant message to conversation
DEBUG: session_load_from_db: locking conversation state
DEBUG: session_load_from_db: locked, state->count = 83
DEBUG: session_load_from_db: assigning message to array
DEBUG: session_load_from_db: message assigned, new count = 84
DEBUG: session_load_from_db: got row 43
DEBUG: session_load_from_db: getting column data
DEBUG: session_load_from_db: got column data
DEBUG: session_load_from_db: checking column completeness
DEBUG: session_load_from_db: checking status
DEBUG: session_load_from_db: status OK
DEBUG: session_load_from_db: parsing request JSON
DEBUG: session_load_from_db: cJSON_Parse returned 0x600001428040
DEBUG: session_load_from_db: request parsed successfully
DEBUG: session_load_from_db: getting messages array
DEBUG: session_load_from_db: messages = 0x600001428d40
DEBUG: session_load_from_db: messages is array, size = 86
DEBUG: session_load_from_db: finding last user message
DEBUG: session_load_from_db: msg_count = 86
DEBUG: session_load_from_db: checking message 85
DEBUG: session_load_from_db: checking message 84
DEBUG: session_load_from_db: checking message 83
DEBUG: session_load_from_db: checking message 82
DEBUG: session_load_from_db: checking message 81
DEBUG: session_load_from_db: checking message 80
DEBUG: session_load_from_db: checking message 79
DEBUG: session_load_from_db: checking message 78
DEBUG: session_load_from_db: checking message 77
DEBUG: session_load_from_db: checking message 76
DEBUG: session_load_from_db: checking message 75
DEBUG: session_load_from_db: checking message 74
DEBUG: session_load_from_db: checking message 73
DEBUG: session_load_from_db: checking message 72
DEBUG: session_load_from_db: checking message 71
DEBUG: session_load_from_db: checking message 70
DEBUG: session_load_from_db: checking message 69
DEBUG: session_load_from_db: checking message 68
DEBUG: session_load_from_db: checking message 67
DEBUG: session_load_from_db: checking message 66
DEBUG: session_load_from_db: checking message 65
DEBUG: session_load_from_db: checking message 64
DEBUG: session_load_from_db: checking message 63
DEBUG: session_load_from_db: checking message 62
DEBUG: session_load_from_db: checking message 61
DEBUG: session_load_from_db: checking message 60
DEBUG: session_load_from_db: checking message 59
DEBUG: session_load_from_db: checking message 58
DEBUG: session_load_from_db: checking message 57
DEBUG: session_load_from_db: checking message 56
DEBUG: session_load_from_db: checking message 55
DEBUG: session_load_from_db: checking message 54
DEBUG: session_load_from_db: checking message 53
DEBUG: session_load_from_db: checking message 52
DEBUG: session_load_from_db: checking message 51
DEBUG: session_load_from_db: checking message 50
DEBUG: session_load_from_db: checking message 49
DEBUG: session_load_from_db: checking message 48
DEBUG: session_load_from_db: checking message 47
DEBUG: session_load_from_db: checking message 46
DEBUG: session_load_from_db: checking message 45
DEBUG: session_load_from_db: checking message 44
DEBUG: session_load_from_db: checking message 43
DEBUG: session_load_from_db: checking message 42
DEBUG: session_load_from_db: checking message 41
DEBUG: session_load_from_db: checking message 40
DEBUG: session_load_from_db: checking message 39
DEBUG: session_load_from_db: checking message 38
DEBUG: session_load_from_db: checking message 37
DEBUG: session_load_from_db: checking message 36
DEBUG: session_load_from_db: checking message 35
DEBUG: session_load_from_db: checking message 34
DEBUG: session_load_from_db: checking message 33
DEBUG: session_load_from_db: checking message 32
DEBUG: session_load_from_db: checking message 31
DEBUG: session_load_from_db: checking message 30
DEBUG: session_load_from_db: checking message 29
DEBUG: session_load_from_db: checking message 28
DEBUG: session_load_from_db: checking message 27
DEBUG: session_load_from_db: checking message 26
DEBUG: session_load_from_db: checking message 25
DEBUG: session_load_from_db: checking message 24
DEBUG: session_load_from_db: checking message 23
DEBUG: session_load_from_db: checking message 22
DEBUG: session_load_from_db: checking message 21
DEBUG: session_load_from_db: checking message 20
DEBUG: session_load_from_db: checking message 19
DEBUG: session_load_from_db: checking message 18
DEBUG: session_load_from_db: checking message 17
DEBUG: session_load_from_db: checking message 16
DEBUG: session_load_from_db: checking message 15
DEBUG: session_load_from_db: checking message 14
DEBUG: session_load_from_db: checking message 13
DEBUG: session_load_from_db: checking message 12
DEBUG: session_load_from_db: checking message 11
DEBUG: session_load_from_db: checking message 10
DEBUG: session_load_from_db: checking message 9
DEBUG: session_load_from_db: checking message 8
DEBUG: session_load_from_db: checking message 7
DEBUG: session_load_from_db: checking message 6
DEBUG: session_load_from_db: checking message 5
DEBUG: session_load_from_db: checking message 4
DEBUG: session_load_from_db: checking message 3
DEBUG: session_load_from_db: checking message 2
DEBUG: session_load_from_db: checking message 1
DEBUG: session_load_from_db: found user message at index 1
DEBUG: session_load_from_db: last_user_message = 0x600001429000
DEBUG: session_load_from_db: extracting content
DEBUG: session_load_from_db: content = 0x600001428b80
DEBUG: session_load_from_db: checking content type
DEBUG: session_load_from_db: content is string, adding user message
DEBUG: session_load_from_db: user message added
DEBUG: session_load_from_db: request deleted
DEBUG: session_load_from_db: parsing response JSON
DEBUG: session_load_from_db: response = 0x60000142cfc0
DEBUG: session_load_from_db: calling parse_openai_response
DEBUG: session_load_from_db: parse_openai_response returned, content_count = 1
DEBUG: session_load_from_db: adding assistant message to conversation
DEBUG: session_load_from_db: locking conversation state
DEBUG: session_load_from_db: locked, state->count = 85
DEBUG: session_load_from_db: assigning message to array
DEBUG: session_load_from_db: message assigned, new count = 86
klawed(63161,0x1fb262240) malloc: *** error for object 0x60000143ac40: pointer being freed was not allocated
klawed(63161,0x1fb262240) malloc: *** set a breakpoint in malloc_error_break to debug
zsh: abort      OPENAI_API_KEY=sk-3ec6a2b143b7498590dc72ccf074a8fe OPENAI_API_BASE= = klawed 

---

- [ ] [2025-12-18 11:57:36] [sess_1766033195_15b39516] WARN  [openai_messages.c:370] build_openai_request: Request may be invalid: 60 tool_calls but only 0 tool_results

- [x] log WARN when finish_reason is 'length' (implemented in klawed.c:6170 and klawed.c:7068)

- [ ] add `make bump-minor-version`
