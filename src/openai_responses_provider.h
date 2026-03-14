/*
 * openai_responses_provider.h - OpenAI Responses API Provider
 *
 * Implements the Provider interface for OpenAI's /v1/responses endpoint.
 * This is a dedicated provider type ("openai_responses") that always uses
 * the Responses API message format, regardless of the configured URL.
 *
 * Unlike the generic openai_provider which auto-detects the Responses API
 * by sniffing "/responses" in the URL, this provider always uses the
 * Responses API format and constructs the correct endpoint URL automatically
 * from the configured base URL.
 *
 * Authentication uses the OPENAI_API_KEY environment variable or the api_key
 * field in the provider configuration.
 *
 * Example configuration:
 *   {
 *     "provider_type": "openai_responses",
 *     "model": "gpt-4o",
 *     "api_base": "https://api.openai.com/v1/responses"
 *   }
 *
 * Or via KLAWED_LLM_PROVIDER pointing to a named provider with
 * provider_type = "openai_responses".
 */

#ifndef OPENAI_RESPONSES_PROVIDER_H
#define OPENAI_RESPONSES_PROVIDER_H

#include "provider.h"

/**
 * OpenAI Responses API provider configuration
 */
typedef struct {
    char *api_key;   /* API key (from config or OPENAI_API_KEY env var) */
    char *api_base;  /* Full endpoint URL, e.g. "https://api.openai.com/v1/responses" */
    char *model;     /* Model name, e.g. "gpt-4o" */
} OpenAIResponsesProviderConfig;

/**
 * Create an OpenAI Responses API provider instance.
 *
 * @param api_key  API key string. If NULL or empty, falls back to OPENAI_API_KEY env var.
 * @param api_base Base URL for the Responses endpoint. If NULL, defaults to
 *                 "https://api.openai.com/v1/responses".
 * @param model    Model name. If NULL, defaults to "gpt-4o".
 * @return Provider instance (caller must clean up via provider->cleanup()),
 *         or NULL on allocation error.
 */
Provider *openai_responses_provider_create(const char *api_key,
                                            const char *api_base,
                                            const char *model);

#endif /* OPENAI_RESPONSES_PROVIDER_H */
