- [ ] subagents
    - [ ] explore
    - [ ] oracle
        - can think hard
        - has web search
- [ ] sometimes cursor placement is incorrect. maybe after some alt + <key> movements
- [ ] make sure timeout is implemented
- [ ] can open vim and when exits go back to the bot
- [ ] reasoning effort
    - [ ] providers API diff
        - [ ] deepseek
        - [ ] moonshot
        - [ ] aws bedrock
        - [ ] anthropic
    - [ ] toggle

- [ ] read api_calls.db for token per second stats

---

- [ ] ctrl + c doesn't terminate the running tool

- [ ] [2025-12-18 11:57:36] [sess_1766033195_15b39516] WARN  [openai_messages.c:370] build_openai_request: Request may be invalid: 60 tool_calls but only 0 tool_results

- [x] log WARN when finish_reason is 'length' (implemented in klawed.c:6170 and klawed.c:7068)
