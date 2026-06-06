#!/bin/bash
# test_transcribe_api.sh — End-to-end test of OpenAI transcription pipeline
#
# Generates speech via OpenAI TTS, transcribes it via the transcription API,
# and verifies the result matches expected text.
#
# Usage:
#   OPENAI_API_KEY=sk-... ./tests/test_transcribe_api.sh [test_text]
#
# Prerequisites: curl, ffmpeg, python3

set -euo pipefail

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

KNOWN_TEXT="${1:-hello world this is a test of voice transcription}"
API_KEY="${OPENAI_API_KEY:-}"

if [ -z "$API_KEY" ]; then
    echo -e "${RED}ERROR: OPENAI_API_KEY not set${NC}"
    echo "  export OPENAI_API_KEY=sk-..."
    exit 1
fi

echo "========================================"
echo " Voice Transcription Pipeline Test"
echo "========================================"
echo " Known text: '$KNOWN_TEXT'"
echo ""

# ── Step 1: Generate speech via OpenAI TTS ─────────────────────────

echo -n " [1/3] Generating speech via OpenAI TTS... "
TMP_WAV="/tmp/test_tts_$$.wav"
rm -f "$TMP_WAV"
trap "rm -f $TMP_WAV 2>/dev/null" EXIT

# Escape the text for JSON
ESCAPED_TEXT=$(python3 -c "import json; print(json.dumps('$KNOWN_TEXT'))")

HTTP_CODE=$(curl -s -w "%{http_code}" -o "$TMP_WAV" \
    -X POST "https://api.openai.com/v1/audio/speech" \
    -H "Authorization: Bearer $API_KEY" \
    -H "Content-Type: application/json" \
    -d "{\"model\":\"tts-1\",\"input\":$ESCAPED_TEXT,\"voice\":\"alloy\",\"response_format\":\"wav\"}")

if [ "$HTTP_CODE" != "200" ]; then
    echo -e "${RED}FAIL (HTTP $HTTP_CODE)${NC}"
    cat "$TMP_WAV" 2>/dev/null || true
    exit 1
fi

FILE_SIZE=$(wc -c < "$TMP_WAV" | tr -d ' ')
echo -e "${GREEN}OK${NC} (${FILE_SIZE} bytes)"

# ── Step 2: Convert to 16kHz mono WAV ──────────────────────────────

echo -n " [2/3] Converting to 16kHz mono WAV... "
CONVERTED_WAV="/tmp/test_speech.wav"
ffmpeg -y -i "$TMP_WAV" -ar 16000 -ac 1 "$CONVERTED_WAV" 2>/dev/null

if [ ! -s "$CONVERTED_WAV" ]; then
    echo -e "${RED}FAIL${NC}"
    exit 1
fi

CONV_SIZE=$(wc -c < "$CONVERTED_WAV" | tr -d ' ')
DURATION=$(ffprobe -v quiet -show_entries format=duration -of csv=p=0 "$CONVERTED_WAV")
echo -e "${GREEN}OK${NC} (${CONV_SIZE} bytes, ${DURATION}s)"

# ── Step 3: Transcribe via OpenAI ───────────────────────────────────

echo -n " [3/3] Transcribing via gpt-4o-transcribe... "

RESPONSE=$(curl -s -X POST "https://api.openai.com/v1/audio/transcriptions" \
    -H "Authorization: Bearer $API_KEY" \
    -F "file=@$CONVERTED_WAV;type=audio/wav" \
    -F "model=gpt-4o-transcribe" \
    -F "response_format=json")

# Extract text field from JSON response
TRANSCRIPTION=$(echo "$RESPONSE" | python3 -c "import sys,json; print(json.load(sys.stdin).get('text',''))" 2>/dev/null || echo "")

if [ -z "$TRANSCRIPTION" ]; then
    echo -e "${RED}FAIL${NC}"
    echo "  Raw response: $RESPONSE"
    exit 1
fi

echo -e "${GREEN}OK${NC}"

# ── Verification ────────────────────────────────────────────────────

echo ""
echo "════════════════════════════════════════════"
echo " Known:       '$KNOWN_TEXT'"
echo " Transcribed: '$TRANSCRIPTION'"
echo "════════════════════════════════════════════"

# Case-insensitive word match check
KNOWN_LOWER=$(echo "$KNOWN_TEXT" | tr '[:upper:]' '[:lower:]')
TRANS_LOWER=$(echo "$TRANSCRIPTION" | tr '[:upper:]' '[:lower:]')

# Count how many words from known text appear in transcription
FOUND=0
TOTAL=0
for word in $KNOWN_LOWER; do
    TOTAL=$((TOTAL + 1))
    if echo "$TRANS_LOWER" | grep -qw "$word"; then
        FOUND=$((FOUND + 1))
    fi
done

MATCH_PCT=$((FOUND * 100 / TOTAL))

if [ $FOUND -ge $((TOTAL * 60 / 100)) ]; then
    echo ""
    echo -e " ${GREEN}✓ PASS${NC} — $FOUND/$TOTAL words matched ($MATCH_PCT%)"
    exit 0
else
    echo ""
    echo -e " ${RED}✗ FAIL${NC} — only $FOUND/$TOTAL words matched ($MATCH_PCT%)"
    exit 1
fi
