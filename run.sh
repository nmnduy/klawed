set -xe
make && CLAUDE_C_THEME=${CLAUDE_C_THEME:-kitty-default} \
  ./build/claude-c

# examples config:
# export OPENAI_API_KEY="$OPENROUTER_API_KEY"
# export OPENAI_API_BASE="https://openrouter.ai/api"
# export OPENAI_MODEL="z-ai/glm-4.6"
