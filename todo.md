- [ ] search results are not highlihted when using '/' and '?' in normal mode
- [ ] cant clear search?
- [ ] /help doesn't show shit
- [ ] doesn't auto-scroll at 100% scroll position. should always auto-scroll at that position
    - Fixed: Changed auto-scroll condition from `scroll_offset >= max_scroll` to `scroll_offset >= max_scroll - 1` (98-100% range)
    - Updated both occurrences in src/tui.c with proper logging
    - Added comprehensive unit tests in tests/test_tui_auto_scroll.c
    - Added test target to Makefile: `make test-tui-auto-scroll`

---

- [ ] subagents
    - [ ] explore: explore code base or current directtory and write a concise markdown with findings
    - [ ] oracle
        - can think hard
        - has web search

- [x] can open vim and when exits go back to the bot
