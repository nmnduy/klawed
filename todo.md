- [x] stderr output from bash command still printed on top of the input box
- [x] if bash command output collected is too large, truncate and let the AI knows that. also update the tool description that this will happen.
- [x] no total token usage count shown in normal mode
- [x] $ sqlite3  .claude-c/api_calls.db 'select response_json from api_calls order by timestamp desc limit 1; '
{"error":{"message":"This model's maximum context length is 131072 tokens. However, you requested 158254 tokens (158254 in the messages, 0 in the completion). Please reduce the length of the messages or completion.","type":"invalid_request_error","param":null,"code":"invalid_request_error"}}
- [ ] Upload Image doesn't show file being uploaded.
- [ ] implement /config
    - [ ] { env: <map of env vars to load. good for proxies>, ... <other config i usually set> }
    - file at $HOME/.claude-c/settings.json
- [x] can use ( and ) to jump in normal mode
