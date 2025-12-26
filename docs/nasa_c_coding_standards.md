# The Power of 10 Rules for Writing Safety-Critical Code

## Overview

The **Power of 10 Rules** were created in 2006 by **Gerard J. Holzmann** of the NASA/JPL Laboratory for Reliable Software[1]. These rules are designed to eliminate certain C coding practices that make code difficult to review or statically analyze. They complement the MISRA C guidelines and have been incorporated into the broader set of JPL coding standards[2].

## The Ten Rules

### 1. Simple Control Flow
**Restrict all code to very simple control flow constructs.**
- Do not use `goto` statements
- Do not use `setjmp` or `longjmp` constructs
- Do not use direct or indirect recursion

### 2. Bounded Loops
**Give all loops a fixed upper bound.**
- It must be trivially possible for a checking tool to prove statically that the loop cannot exceed a preset upper bound on the number of iterations
- If a tool cannot prove the loop bound statically, the rule is considered violated

### 3. No Dynamic Memory Allocation After Initialization
**Do not use dynamic memory allocation after initialization.**

### 4. Function Length Limit
**No function should be longer than what can be printed on a single sheet of paper.**
- Standard format: one line per statement and one line per declaration
- Typically, this means no more than about **60 lines of code per function**

### 5. Assertion Density
**The code's assertions density should average to minimally two assertions per function.**
- Assertions must be used to check for anomalous conditions that should never happen in real-life executions
- Assertions must be side-effect free and should be defined as Boolean tests
- When an assertion fails, an explicit recovery action must be taken (e.g., returning an error condition to the caller)
- Any assertion for which a static checking tool can prove that it can never fail or never hold violates this rule

### 6. Minimal Scope
**Declare all data objects at the smallest possible level of scope.**

### 7. Return Value and Parameter Checking
**Each calling function must check the return value of non-void functions.**
**Each called function must check the validity of all parameters provided by the caller.**

### 8. Limited Preprocessor Use
**The use of the preprocessor must be limited to:**
- Inclusion of header files
- Simple macro definitions

**Prohibited:**
- Token pasting
- Variable argument lists (ellipses)
- Recursive macro calls
- All macros must expand into complete syntactic units
- The use of conditional compilation directives must be kept to a minimum

### 9. Restricted Pointer Use
**The use of pointers must be restricted.**
- No more than one level of dereference should be used
- Pointer dereference operations may not be hidden in macro definitions or inside `typedef` declarations
- Function pointers are not permitted

### 10. Compiler Warnings and Static Analysis
**All code must be compiled, from the first day of development, with:**
- All compiler warnings enabled at the most pedantic setting available
- All code must compile without warnings
- All code must also be checked daily with at least one (preferably more than one) strong static source code analyzer
- Should pass all analyses with zero warnings

## Purpose and Application

These rules are particularly important for:
- **Safety-critical systems** (aerospace, medical devices, automotive)
- **High-reliability software** where failures could have catastrophic consequences
- **Code that requires rigorous static analysis and formal verification**
- **Teams practicing code reviews and pair programming**

## References

[1] Holzmann, Gerard J. (2006). "The Power of 10: Rules for Developing Safety-Critical Code". IEEE Computer.

[2] NASA/JPL Laboratory for Reliable Software. JPL Institutional Coding Standard for the C Programming Language.

---

*Note: These rules represent a strict subset of coding practices suitable for the most critical software systems. For less critical applications, some rules may be relaxed while still maintaining good software engineering practices.*