#!/usr/bin/env bash
set -euo pipefail

# Convert a Kitty theme .conf (file path or URL) into a C snippet for builtin_themes.c
# Usage: ./scripts/embed_kitty_theme.sh "theme-name" /path/or/url/to/theme.conf > /tmp/snippet.c

if [ $# -ne 2 ]; then
  echo "Usage: $0 <theme-name> <path-or-url-to-conf>" >&2
  exit 1
fi

THEME_NAME="$1"
SOURCE="$2"

if [ -z "$THEME_NAME" ]; then
  echo "Error: theme name must not be empty" >&2
  exit 1
fi

temp_conf="$(mktemp)"
trap 'rm -f "$temp_conf"' EXIT

# Fetch or copy the theme
if [[ "$SOURCE" =~ ^https?:// ]]; then
  curl -fsSL "$SOURCE" -o "$temp_conf"
else
  cp "$SOURCE" "$temp_conf"
fi

# Normalize line endings and remove trailing CR
perl -pi -e 's/\r$//' "$temp_conf"

# Emit C snippet
python3 - "$THEME_NAME" "$temp_conf" <<'PY'
import sys
from pathlib import Path

def escape_for_c(s: str) -> str:
    # Escape backslashes and double quotes, keep newlines
    s = s.replace('\\', '\\\\').replace('"', '\\"')
    return s.replace('\n', '\\n\n')

name = sys.argv[1]
conf_path = Path(sys.argv[2])
content = conf_path.read_text(encoding='utf-8')
escaped = escape_for_c(content)

snippet = f"    {{ \"{name}\",\n      \"{escaped}\" }},\n"
print(snippet)
PY
