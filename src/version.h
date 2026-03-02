/*
 * version.h - Central version management for Klawed
 *
 * This file provides a single source of truth for version information.
 * It's automatically generated from the VERSION file during build.
 */

#ifndef VERSION_H
#define VERSION_H

// Version string (e.g., "0.0.2", "1.0.0", "1.2.3-beta.1")
#define KLAWED_VERSION "0.30.15"

// Version components for programmatic use
#define KLAWED_VERSION_MAJOR 0
#define KLAWED_VERSION_MINOR 30
#define KLAWED_VERSION_PATCH 15

// Version as numeric value for comparisons (e.g., 0x000002)
#define KLAWED_VERSION_NUMBER 0x001e0f

// Build timestamp (automatically generated)
#define KLAWED_BUILD_TIMESTAMP "2026-03-02"

// Full version string with build info
#define KLAWED_VERSION_FULL "0.30.15 (built 2026-03-02)"

#endif // VERSION_H
