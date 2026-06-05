package main

import (
	"bytes"
	"encoding/json"
	"testing"
)

func TestWriteKlawedResponse_Error(t *testing.T) {
	msg := Message{Error: "something went wrong"}
	var buf bytes.Buffer

	err := writeKlawedResponse(&buf, msg)
	if err != nil {
		t.Fatalf("writeKlawedResponse failed: %v", err)
	}

	// Should get {"error":"something went wrong"}\n — no id, no result wrapper
	var parsed map[string]string
	if err := json.Unmarshal(bytes.TrimSpace(buf.Bytes()), &parsed); err != nil {
		t.Fatalf("invalid JSON output: %v (raw: %q)", err, buf.String())
	}
	if parsed["error"] != "something went wrong" {
		t.Errorf("expected error 'something went wrong', got %q", parsed["error"])
	}
	if _, hasID := parsed["id"]; hasID {
		t.Error("response should NOT contain 'id' field")
	}
	if _, hasResult := parsed["result"]; hasResult {
		t.Error("response should NOT contain 'result' wrapper")
	}
}

func TestWriteKlawedResponse_Success(t *testing.T) {
	// Simulate Chrome extension returning newWindow result:
	// {"windowId": 893514234, "focused": true, "tabCount": 1}
	chromeResult := json.RawMessage(`{"windowId": 893514234, "focused": true, "tabCount": 1}`)
	msg := Message{
		ID:     "12345-67890",
		Result: chromeResult,
	}
	var buf bytes.Buffer

	err := writeKlawedResponse(&buf, msg)
	if err != nil {
		t.Fatalf("writeKlawedResponse failed: %v", err)
	}

	// Should get just the result object — no id, no result wrapper
	var parsed map[string]interface{}
	if err := json.Unmarshal(bytes.TrimSpace(buf.Bytes()), &parsed); err != nil {
		t.Fatalf("invalid JSON output: %v (raw: %q)", err, buf.String())
	}

	// Verify windowId is directly accessible (the original bug)
	windowID, ok := parsed["windowId"]
	if !ok {
		t.Error("response should have top-level 'windowId' field")
	}
	if wid, ok := windowID.(float64); !ok || wid != 893514234 {
		t.Errorf("expected windowId 893514234, got %v", windowID)
	}

	// Verify no wrapping fields leaked
	if _, hasID := parsed["id"]; hasID {
		t.Error("response should NOT contain 'id' field")
	}
	if _, hasResult := parsed["result"]; hasResult {
		t.Error("response should NOT contain 'result' wrapper")
	}

	// Verify focus and tabCount also present
	if focused, ok := parsed["focused"]; !ok || focused != true {
		t.Errorf("expected focused=true, got %v", focused)
	}
	if tc, ok := parsed["tabCount"]; !ok || tc != float64(1) {
		t.Errorf("expected tabCount=1, got %v", tc)
	}
}

func TestWriteKlawedResponse_Screenshot(t *testing.T) {
	// Screenshot responses have dataUrl — should still be unwrapped
	chromeResult := json.RawMessage(`{"dataUrl": "data:image/png;base64,AAAA", "format": "png"}`)
	msg := Message{
		ID:     "abc-def",
		Result: chromeResult,
	}
	var buf bytes.Buffer

	err := writeKlawedResponse(&buf, msg)
	if err != nil {
		t.Fatalf("writeKlawedResponse failed: %v", err)
	}

	var parsed map[string]interface{}
	if err := json.Unmarshal(bytes.TrimSpace(buf.Bytes()), &parsed); err != nil {
		t.Fatalf("invalid JSON output: %v (raw: %q)", err, buf.String())
	}

	if parsed["dataUrl"] != "data:image/png;base64,AAAA" {
		t.Errorf("expected dataUrl, got %v", parsed["dataUrl"])
	}
	// browser_ctl's prettyPrint should still find dataUrl at top level
}

func TestWriteKlawedResponse_Empty(t *testing.T) {
	msg := Message{} // no error, no result
	var buf bytes.Buffer

	err := writeKlawedResponse(&buf, msg)
	if err != nil {
		t.Fatalf("writeKlawedResponse failed: %v", err)
	}

	output := bytes.TrimSpace(buf.Bytes())
	if string(output) != "{}" {
		t.Errorf("expected '{}' for empty message, got %q", output)
	}
}

func TestWriteKlawedResponse_NilResult(t *testing.T) {
	// Result is nil (not just empty) — should produce {}
	msg := Message{
		ID:     "test-id",
		Result: nil,
	}
	var buf bytes.Buffer

	err := writeKlawedResponse(&buf, msg)
	if err != nil {
		t.Fatalf("writeKlawedResponse failed: %v", err)
	}

	output := bytes.TrimSpace(buf.Bytes())
	if string(output) != "{}" {
		t.Errorf("expected '{}' for nil result, got %q", output)
	}
}

func TestWriteKlawedResponse_ErrorWithID(t *testing.T) {
	// Error from Chrome with an ID — ID should be stripped
	msg := Message{
		ID:    "should-be-removed",
		Error: "Chrome extension not found",
	}
	var buf bytes.Buffer

	err := writeKlawedResponse(&buf, msg)
	if err != nil {
		t.Fatalf("writeKlawedResponse failed: %v", err)
	}

	var parsed map[string]string
	if err := json.Unmarshal(bytes.TrimSpace(buf.Bytes()), &parsed); err != nil {
		t.Fatalf("invalid JSON output: %v (raw: %q)", err, buf.String())
	}
	if parsed["error"] != "Chrome extension not found" {
		t.Errorf("expected error, got %q", parsed["error"])
	}
	if _, hasID := parsed["id"]; hasID {
		t.Error("response should NOT contain 'id' field in error response")
	}
}

func TestWriteKlawedResponse_ComplexResult(t *testing.T) {
	// getPageSource or findElements might return nested objects
	chromeResult := json.RawMessage(`{"elements": [{"tag": "div", "text": "hello"}], "count": 1}`)
	msg := Message{
		ID:     "complex-id",
		Result: chromeResult,
	}
	var buf bytes.Buffer

	err := writeKlawedResponse(&buf, msg)
	if err != nil {
		t.Fatalf("writeKlawedResponse failed: %v", err)
	}

	var parsed map[string]interface{}
	if err := json.Unmarshal(bytes.TrimSpace(buf.Bytes()), &parsed); err != nil {
		t.Fatalf("invalid JSON output: %v (raw: %q)", err, buf.String())
	}

	if _, hasElements := parsed["elements"]; !hasElements {
		t.Error("response should have top-level 'elements' field")
	}
	if _, hasResult := parsed["result"]; hasResult {
		t.Error("response should NOT contain 'result' wrapper")
	}
}
