## History Search Improvements - 2026-01-14

### Problems Fixed

1. **Fuzzy matching returned nonsense results**
   - The fuzzy matching algorithm was giving poor scores for exact/prefix matches
   - Example: searching for "make" would rank "make test" and "make" equally
   - Single character searches would get very low scores

2. **Menu too small on mobile**
   - History search popup was only 33% of screen height
   - File search uses 60%, help modal uses 80%
   - On mobile devices, 33% was too small to be usable

### Changes Made

1. **Replaced fuzzy matching with exact substring matching** (`src/history_search.c`)
   - New `match_score()` function replaces `fuzzy_score()`
   - Uses case-insensitive substring search (strstr)
   - Scoring system:
     - Exact match (same length): 11000+ score
     - Prefix match: 6000+ score  
     - Word boundary match: 3000+ score
     - Suffix match: 1000+ score
   - Longer pattern matches get bonus (more specific)
   - Shorter commands get bonus (prefer concise)
   - Case-sensitive character matches get small bonus

2. **Increased popup size** (`src/history_search.c`)
   - Changed height from 33% (screen_height / 3) to 80% for mobile with keyboard
   - Changed width from 66% (2/3) to 90% for better use of screen space
   - Minimum height increased from 5 to 10 lines
   - Much better mobile/phone experience when keyboard is visible

3. **Updated documentation** (`src/history_search.h`)
   - Changed "Fuzzy search" to "Substring/exact match search"

### Test Results

Exact matching now works correctly:
- "make" matches "make" exactly: score 11440
- "make" matches "make test" as prefix: score 6440
- "test" matches "test" exactly: score 11440
- "test" matches "make test" as suffix: score 3440

This is a HUGE improvement - exact matches now score ~2x higher than prefix matches,
and prefix matches score ~2x higher than word/suffix matches.

### Files Changed

- `src/history_search.c` - replaced fuzzy matching with exact substring matching, increased popup size
- `src/history_search.h` - updated documentation

### Unit Tests

Created basic test file at `tests/test_history_search.c` but it has compilation issues 
due to ncurses dependency conflicts. The standalone test (`test_match_simple.c`) 
demonstrates the algorithm works correctly.

A proper unit test should be created that:
1. Tests exact match prioritization
2. Tests prefix match prioritization
3. Tests case-insensitive matching
4. Tests chronological ordering when scores tie
5. Tests empty pattern behavior
6. Tests no-match scenarios
