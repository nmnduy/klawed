/*
 * bedrock_provider.h - AWS Bedrock API provider
 *
 * Implements the Provider interface for AWS Bedrock
 * Handles SigV4 signing, credential refresh, and format conversion
 */

#ifndef BEDROCK_PROVIDER_H
#define BEDROCK_PROVIDER_H

#include "provider.h"
#include "aws_bedrock.h"

/**
 * Create a Bedrock provider instance
 *
 * @param model - Model name (e.g., "claude-sonnet-4-20250514")
 * @return Provider instance (caller must cleanup via provider->cleanup()), or NULL on error
 *         On error, check logs for details (credential loading, region detection, etc.)
 */
Provider* bedrock_provider_create(const char *model);

#endif // BEDROCK_PROVIDER_H
