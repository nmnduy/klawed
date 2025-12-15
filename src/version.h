/*
 * version.h - Central version management for Claude C
 *
 * This file provides a single source of truth for version information.
 * It's automatically generated from the VERSION file during build.
 */

#ifndef VERSION_H
#define VERSION_H

// Version string (e.g., "0.0.2", "1.0.0", "1.2.3-beta.1")
#define CLAUDE_C_VERSION "0.1.11"

// Version components for programmatic use
#define CLAUDE_C_VERSION_MAJOR 0
#define CLAUDE_C_VERSION_MINOR 1
#define CLAUDE_C_VERSION_PATCH 11

// Version as numeric value for comparisons (e.g., 0x000002)
#define CLAUDE_C_VERSION_NUMBER 0x00010b

// Build timestamp (automatically generated)
#define CLAUDE_C_BUILD_TIMESTAMP "2025-11-30"

// Full version string with build info
#define CLAUDE_C_VERSION_FULL "0.1.11 (built 2025-11-30)"

#endif // VERSION_H
