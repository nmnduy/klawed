/*
 * minimax_coding_provider.h - MiniMax Coding Plan API provider interface
 *
 * Thin wrapper around Anthropic provider for MiniMax's Anthropic-compatible API.
 */

#ifndef MINIMAX_CODING_PROVIDER_H
#define MINIMAX_CODING_PROVIDER_H

#include "provider.h"

/**
 * Create a MiniMax Coding Plan provider
 *
 * @param api_key The API key for MiniMax Coding Plan
 * @param base_url Optional custom base URL (NULL for default)
 * @return A configured Provider instance, or NULL on failure
 */
Provider* minimax_coding_provider_create(const char *api_key, const char *base_url);

#endif /* MINIMAX_CODING_PROVIDER_H */
