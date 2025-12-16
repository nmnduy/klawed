set -xe
make && KLAWED_THEME=${KLAWED_THEME:-kitty-default} \
  ./build/klawed

# examples config:
# export OPENAI_API_KEY="$OPENROUTER_API_KEY"
# export OPENAI_API_BASE="https://openrouter.ai/api"
# export OPENAI_MODEL="z-ai/glm-4.6"
