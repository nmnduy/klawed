package com.filesurf.model;

import com.fasterxml.jackson.annotation.JsonIgnoreProperties;
import com.fasterxml.jackson.annotation.JsonProperty;

@JsonIgnoreProperties(ignoreUnknown = true)
public class KlawedToolResponse {
    
    @JsonProperty("type")
    private String type; // "tool_response"
    
    @JsonProperty("data")
    private ToolResponseData data;
    
    // Getters and setters
    public String getType() {
        return type;
    }
    
    public void setType(String type) {
        this.type = type;
    }
    
    public ToolResponseData getData() {
        return data;
    }
    
    public void setData(ToolResponseData data) {
        this.data = data;
    }
    
    @JsonIgnoreProperties(ignoreUnknown = true)
    public static class ToolResponseData {
        @JsonProperty("tool_id")
        private String toolId;
        
        @JsonProperty("tool_name")
        private String toolName;
        
        @JsonProperty("result")
        private ToolResult result;
        
        public String getToolId() {
            return toolId;
        }
        
        public void setToolId(String toolId) {
            this.toolId = toolId;
        }
        
        public String getToolName() {
            return toolName;
        }
        
        public void setToolName(String toolName) {
            this.toolName = toolName;
        }
        
        public ToolResult getResult() {
            return result;
        }
        
        public void setResult(ToolResult result) {
            this.result = result;
        }
    }
    
    @JsonIgnoreProperties(ignoreUnknown = true)
    public static class ToolResult {
        @JsonProperty("output")
        private String output;
        
        @JsonProperty("exit_code")
        private Integer exitCode;
        
        @JsonProperty("truncation_warning")
        private String truncationWarning;
        
        @JsonProperty("error")
        private String error;
        
        public String getOutput() {
            return output;
        }
        
        public void setOutput(String output) {
            this.output = output;
        }
        
        public Integer getExitCode() {
            return exitCode;
        }
        
        public void setExitCode(Integer exitCode) {
            this.exitCode = exitCode;
        }
        
        public String getTruncationWarning() {
            return truncationWarning;
        }
        
        public void setTruncationWarning(String truncationWarning) {
            this.truncationWarning = truncationWarning;
        }
        
        public String getError() {
            return error;
        }
        
        public void setError(String error) {
            this.error = error;
        }
    }
}