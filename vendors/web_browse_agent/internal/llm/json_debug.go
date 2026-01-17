package llm

import (
	"bytes"
	"encoding/json"
	"fmt"
)

// formatJSON pretty-prints JSON if possible, otherwise returns the raw string.
func formatJSON(raw []byte) string {
	var buf bytes.Buffer
	if err := json.Indent(&buf, raw, "", "  "); err != nil {
		return string(raw)
	}
	return buf.String()
}

// formatJSONBytes is a convenience wrapper for raw JSON bytes.
func formatJSONBytes(raw []byte) string {
	return formatJSON(raw)
}

// formatJSONStruct marshals a struct and pretty-prints it; falls back to the
// default string representation on error.
func formatJSONStruct(v interface{}) string {
	raw, err := json.Marshal(v)
	if err != nil {
		return fmt.Sprintf("%+v", v)
	}
	return formatJSON(raw)
}
