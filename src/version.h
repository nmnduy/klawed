/*
 * version.h - Central version management for Klawed
 *
 * This file provides a single source of truth for version information.
 * It's automatically generated from the VERSION file during build.
 */

#ifndef VERSION_H
#define VERSION_H

// Version string (e.g., "0.0.2", "1.0.0", "1.2.3-beta.1")
#define KLAWED_VERSION "0.34.0"

// Version components for programmatic use
#define KLAWED_VERSION_MAJOR 0
#define KLAWED_VERSION_MINOR 34
#define KLAWED_VERSION_PATCH 0

// Version as numeric value for comparisons (e.g., 0x000002)
#define KLAWED_VERSION_NUMBER 0x002200

// Build timestamp (automatically generated)
#define KLAWED_BUILD_TIMESTAMP "2026-06-15"

// Full version string with build info
#define KLAWED_VERSION_FULL "0.34.0 (built 2026-06-15)"

#endif // VERSION_H
