package com.filesurf.model;

import com.fasterxml.jackson.annotation.JsonIgnoreProperties;
import com.fasterxml.jackson.annotation.JsonProperty;

import java.util.List;

@JsonIgnoreProperties(ignoreUnknown = true)
public class KlawedResponse {

    @JsonProperty("id")
    private String id;

    @JsonProperty("object")
    private String object;

    @JsonProperty("created")
    private long created;

    @JsonProperty("model")
    private String model;

    @JsonProperty("choices")
    private List<Choice> choices;

    @JsonProperty("usage")
    private Usage usage;

    @JsonProperty("system_fingerprint")
    private String systemFingerprint;

    @JsonProperty("error")
    private String error;

    // Getters and setters
    public String getId() {
        return id;
    }

    public void setId(String id) {
        this.id = id;
    }

    public String getObject() {
        return object;
    }

    public void setObject(String object) {
        this.object = object;
    }

    public long getCreated() {
        return created;
    }

    public void setCreated(long created) {
        this.created = created;
    }

    public String getModel() {
        return model;
    }

    public void setModel(String model) {
        this.model = model;
    }

    public List<Choice> getChoices() {
        return choices;
    }

    public void setChoices(List<Choice> choices) {
        this.choices = choices;
    }

    public Usage getUsage() {
        return usage;
    }

    public void setUsage(Usage usage) {
        this.usage = usage;
    }

    public String getSystemFingerprint() {
        return systemFingerprint;
    }

    public void setSystemFingerprint(String systemFingerprint) {
        this.systemFingerprint = systemFingerprint;
    }

    public String getError() {
        return error;
    }

    public void setError(String error) {
        this.error = error;
    }

    /**
     * Convenience method to get the assistant's response content
     */
    public String getContent() {
        if (choices != null && !choices.isEmpty()) {
            Choice firstChoice = choices.get(0);
            if (firstChoice.getMessage() != null) {
                return firstChoice.getMessage().getContent();
            }
        }
        return null;
    }

    /**
     * Check if this response contains an error
     */
    public boolean hasError() {
        return error != null && !error.trim().isEmpty();
    }

    @JsonIgnoreProperties(ignoreUnknown = true)
    public static class Choice {
        @JsonProperty("index")
        private int index;

        @JsonProperty("message")
        private Message message;

        @JsonProperty("logprobs")
        private Object logprobs; // Can be null

        @JsonProperty("finish_reason")
        private String finishReason;

        public int getIndex() {
            return index;
        }

        public void setIndex(int index) {
            this.index = index;
        }

        public Message getMessage() {
            return message;
        }

        public void setMessage(Message message) {
            this.message = message;
        }

        public Object getLogprobs() {
            return logprobs;
        }

        public void setLogprobs(Object logprobs) {
            this.logprobs = logprobs;
        }

        public String getFinishReason() {
            return finishReason;
        }

        public void setFinishReason(String finishReason) {
            this.finishReason = finishReason;
        }
    }

    @JsonIgnoreProperties(ignoreUnknown = true)
    public static class Message {
        @JsonProperty("role")
        private String role;

        @JsonProperty("content")
        private String content;

        @JsonProperty("tool_calls")
        private List<ToolCall> toolCalls;

        public String getRole() {
            return role;
        }

        public void setRole(String role) {
            this.role = role;
        }

        public String getContent() {
            return content;
        }

        public void setContent(String content) {
            this.content = content;
        }

        public List<ToolCall> getToolCalls() {
            return toolCalls;
        }

        public void setToolCalls(List<ToolCall> toolCalls) {
            this.toolCalls = toolCalls;
        }

        public boolean hasToolCalls() {
            return toolCalls != null && !toolCalls.isEmpty();
        }
    }

    @JsonIgnoreProperties(ignoreUnknown = true)
    public static class ToolCall {
        @JsonProperty("index")
        private int index;

        @JsonProperty("id")
        private String id;

        @JsonProperty("type")
        private String type;

        @JsonProperty("function")
        private Function function;

        public int getIndex() {
            return index;
        }

        public void setIndex(int index) {
            this.index = index;
        }

        public String getId() {
            return id;
        }

        public void setId(String id) {
            this.id = id;
        }

        public String getType() {
            return type;
        }

        public void setType(String type) {
            this.type = type;
        }

        public Function getFunction() {
            return function;
        }

        public void setFunction(Function function) {
            this.function = function;
        }
    }

    @JsonIgnoreProperties(ignoreUnknown = true)
    public static class Function {
        @JsonProperty("name")
        private String name;

        @JsonProperty("arguments")
        private String arguments;

        public String getName() {
            return name;
        }

        public void setName(String name) {
            this.name = name;
        }

        public String getArguments() {
            return arguments;
        }

        public void setArguments(String arguments) {
            this.arguments = arguments;
        }
    }

    @JsonIgnoreProperties(ignoreUnknown = true)
    public static class Usage {
        @JsonProperty("prompt_tokens")
        private int promptTokens;

        @JsonProperty("completion_tokens")
        private int completionTokens;

        @JsonProperty("total_tokens")
        private int totalTokens;

        @JsonProperty("prompt_tokens_details")
        private PromptTokensDetails promptTokensDetails;

        @JsonProperty("prompt_cache_hit_tokens")
        private int promptCacheHitTokens;

        @JsonProperty("prompt_cache_miss_tokens")
        private int promptCacheMissTokens;

        public int getPromptTokens() {
            return promptTokens;
        }

        public void setPromptTokens(int promptTokens) {
            this.promptTokens = promptTokens;
        }

        public int getCompletionTokens() {
            return completionTokens;
        }

        public void setCompletionTokens(int completionTokens) {
            this.completionTokens = completionTokens;
        }

        public int getTotalTokens() {
            return totalTokens;
        }

        public void setTotalTokens(int totalTokens) {
            this.totalTokens = totalTokens;
        }

        public PromptTokensDetails getPromptTokensDetails() {
            return promptTokensDetails;
        }

        public void setPromptTokensDetails(PromptTokensDetails promptTokensDetails) {
            this.promptTokensDetails = promptTokensDetails;
        }

        public int getPromptCacheHitTokens() {
            return promptCacheHitTokens;
        }

        public void setPromptCacheHitTokens(int promptCacheHitTokens) {
            this.promptCacheHitTokens = promptCacheHitTokens;
        }

        public int getPromptCacheMissTokens() {
            return promptCacheMissTokens;
        }

        public void setPromptCacheMissTokens(int promptCacheMissTokens) {
            this.promptCacheMissTokens = promptCacheMissTokens;
        }

        @JsonIgnoreProperties(ignoreUnknown = true)
        public static class PromptTokensDetails {
            @JsonProperty("cached_tokens")
            private int cachedTokens;

            public int getCachedTokens() {
                return cachedTokens;
            }

            public void setCachedTokens(int cachedTokens) {
                this.cachedTokens = cachedTokens;
            }
        }
    }
}