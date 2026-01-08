# Memory Tool Guidance Enhancement - Summary

## Overview

Enhanced guidance for Klawed on when and how to use memory-related tools (MemoryStore, MemoryRecall, MemorySearch).

## Changes Made

### 1. Enhanced Tool Descriptions in `src/klawed.c`

#### MemoryStore
**Before:** Basic description about storing memories
**After:** Proactive guidance with specific triggers:
- "BE PROACTIVE - Store important information when you notice:"
- (1) User preferences: 'I prefer...', 'I don't like...', 'always use...', 'never use...'
- (2) User facts: 'I work at...', 'I'm learning...', 'my team uses...'
- (3) Project constraints: 'we use tabs', 'we follow style X', 'this project requires Y'
- (4) Recurring patterns in their coding style or requests
- (5) User explicitly asks 'remember that...' or 'keep in mind...'
- Clear DO NOT store list: temporary context, sensitive data, transient state, info in KLAWED.md

#### MemoryRecall
**Before:** Basic description about recalling memories
**After:** Specific use cases:
- Check what you already know about user preferences or project details
- Starting a new conversation and want to recall context from previous sessions
- User mentions something you may have stored before
- Verifying if a preference/fact exists before storing a new one

#### MemorySearch
**Before:** Basic description about searching memories
**After:** Specific scenarios:
- Need to find related past context but don't know the specific entity/slot
- User asks about something you may have discussed before
- After auto-compaction notice - search for relevant past conversation context
- Starting a complex task and want to check for relevant project knowledge
- Exploring what you already know about a topic

### 2. Enhanced Documentation in `docs/memvid.md`

#### Expanded "Best Practices" Section
- Changed from simple lists to detailed categories with examples
- Added "BE PROACTIVE" emphasis
- Expanded each category with specific language patterns to watch for
- Clarified what NOT to store with rationale

#### Added "Workflow Guide for Klawed" Section
New section with practical workflows:
- **Starting a New Conversation**: What to do at conversation start
- **During a Conversation**: How to listen for signals and store proactively
- **After Auto-Compaction**: How to retrieve context after compaction
- **Example Workflow**: Complete example showing the full cycle

#### Enhanced Tool Descriptions in Documentation
Added "When to use" sections for each tool with specific scenarios

## Files Modified

1. `/Users/puter/git/klawedspace/src/klawed.c` (lines ~5025-5120)
   - Enhanced MemoryStore description
   - Enhanced MemoryRecall description
   - Enhanced MemorySearch description

2. `/Users/puter/git/klawedspace/docs/memvid.md`
   - Expanded "Best Practices" section (lines ~130-180)
   - Added "Workflow Guide for Klawed" section (new, ~80 lines)
   - Enhanced individual tool descriptions with "When to use"

## Testing

✅ Compilation successful with no warnings or errors
✅ Tool descriptions verified in compiled binary using `strings` command
✅ All three enhanced descriptions confirmed present in build

## Expected Impact

### For Klawed (the AI)
- **More proactive**: Will actively look for opportunities to store memories instead of waiting to be asked
- **Better context awareness**: Clear guidance on when to recall/search for existing memories
- **Fewer duplicates**: Understands when to check for existing memories before storing
- **Session continuity**: Will check for memories at conversation start

### For Users
- **Better personalization**: Klawed will remember preferences and patterns automatically
- **Less repetition**: Won't need to re-explain preferences in new sessions
- **More context-aware responses**: Klawed will use past context appropriately
- **Consistent behavior**: More predictable memory usage across conversations

## Integration with Existing Features

- **Auto-compaction**: Tool descriptions now reference compaction notices and encourage using MemorySearch after compaction
- **KLAWED.md**: Explicit guidance to avoid duplicating information already in project documentation
- **Session management**: Encourages checking memories at session start

## Future Considerations

Potential enhancements:
1. Add memory usage metrics/logging to track how often tools are used
2. Implement memory suggestion hints in the UI
3. Add memory summarization features for long-term context
4. Create memory templates for common use cases
5. Add memory pruning/cleanup guidance

## Notes

- Changes are backward compatible - no API changes
- Tool parameter structures unchanged
- Documentation improvements can be reviewed independently of code changes
- All changes align with existing NASA C coding standards
- No new dependencies introduced
