package com.filesurf.service;

import jakarta.enterprise.context.ApplicationScoped;
import jakarta.inject.Inject;
import org.eclipse.microprofile.config.inject.ConfigProperty;

import java.io.*;
import java.nio.file.*;
import java.util.concurrent.TimeUnit;
import java.util.logging.Logger;

@ApplicationScoped
public class LatexCompilerService {

    private static final Logger LOGGER = Logger.getLogger(LatexCompilerService.class.getName());

    @Inject
    @ConfigProperty(name = "invoice.latex.timeout", defaultValue = "30")
    int latexTimeout;

    @Inject
    @ConfigProperty(name = "invoice.latex.maxAttempts", defaultValue = "2")
    int maxCompilationAttempts;

    /**
     * Compile a LaTeX file to PDF
     *
     * @param texFile Path to the .tex file
     * @param outputDir Directory where PDF should be created (defaults to same directory as .tex file)
     * @param engine LaTeX engine to use (pdflatex, xelatex, lualatex)
     * @return Path to the generated PDF file, or null if compilation failed
     */
    public Path compileToPdf(Path texFile, Path outputDir, String engine) {
        if (texFile == null || !Files.exists(texFile)) {
            LOGGER.warning("LaTeX file does not exist: " + texFile);
            return null;
        }

        if (!texFile.toString().toLowerCase().endsWith(".tex")) {
            LOGGER.warning("File is not a .tex file: " + texFile);
            return null;
        }

        // Validate LaTeX engine
        String validEngine = validateEngine(engine);
        if (validEngine == null) {
            LOGGER.warning("Invalid LaTeX engine: " + engine);
            return null;
        }

        // Use tex file directory if outputDir is null
        Path workingDir = (outputDir != null) ? outputDir : texFile.getParent();
        if (workingDir == null) {
            workingDir = Paths.get(".");
        }

        // Create working directory if it doesn't exist
        try {
            Files.createDirectories(workingDir);
        } catch (IOException e) {
            LOGGER.warning("Failed to create output directory: " + workingDir + " - " + e.getMessage());
            return null;
        }

        // Check for dangerous shell escape commands
        try {
            if (containsDangerousCommands(texFile)) {
                LOGGER.warning("LaTeX file contains dangerous commands, refusing to compile: " + texFile);
                return null;
            }
        } catch (IOException e) {
            LOGGER.warning("Failed to scan LaTeX file for dangerous commands: " + e.getMessage());
            return null;
        }

        // Pre-compilation syntax check
        if (!validateLatexSyntax(texFile, validEngine)) {
            LOGGER.warning("LaTeX syntax validation failed for: " + texFile);
            return null;
        }

        String texFileName = texFile.getFileName().toString();
        String baseName = texFileName.substring(0, texFileName.length() - 4); // Remove .tex extension
        Path pdfFile = workingDir.resolve(baseName + ".pdf");

        LOGGER.info("Compiling LaTeX file: " + texFile + " with engine: " + validEngine);

        try {
            // Build command
            ProcessBuilder pb = new ProcessBuilder(
                validEngine,
                "-interaction=nonstopmode",
                "-halt-on-error",
                "-no-shell-escape",  // CRITICAL: Prevents command execution
                "-output-directory=" + workingDir.toString(),
                texFile.toString()
            );

            // Set working directory to the tex file's directory for proper relative paths
            pb.directory(texFile.getParent().toFile());

            // Redirect error stream to output stream
            pb.redirectErrorStream(true);

            // Start process
            Process process = pb.start();

            // Read output
            StringBuilder output = new StringBuilder();
            try (BufferedReader reader = new BufferedReader(new InputStreamReader(process.getInputStream()))) {
                String line;
                while ((line = reader.readLine()) != null) {
                    output.append(line).append("\n");
                }
            }

            // Wait for process with timeout
            boolean completed = process.waitFor(latexTimeout, TimeUnit.SECONDS);

            if (!completed) {
                process.destroyForcibly();
                LOGGER.warning("LaTeX compilation timed out after " + latexTimeout + " seconds");
                return null;
            }

            int exitCode = process.exitValue();

            if (exitCode != 0) {
                LOGGER.warning("LaTeX compilation failed with exit code " + exitCode + "\nOutput:\n" + output.toString());

                // Try to extract error message
                String errorMessage = extractErrorMessage(output.toString());
                LOGGER.warning("LaTeX error: " + errorMessage);

                return null;
            }

            LOGGER.info("LaTeX compilation successful: " + pdfFile);

            // Check if PDF was actually created
            if (!Files.exists(pdfFile)) {
                LOGGER.warning("PDF file was not created: " + pdfFile);
                return null;
            }

            // Sometimes LaTeX needs multiple passes for references, table of contents, etc.
            // Run a second pass if file size is suspiciously small (< 100 bytes)
            long fileSize = Files.size(pdfFile);
            if (fileSize < 100) {
                LOGGER.info("PDF file is very small (" + fileSize + " bytes), running second compilation pass");
                return compileToPdf(texFile, outputDir, validEngine);
            }

            return pdfFile;

        } catch (IOException | InterruptedException e) {
            LOGGER.warning("Failed to compile LaTeX: " + e.getMessage());
            return null;
        }
    }

    /**
     * Compile LaTeX to PDF with default engine (pdflatex)
     */
    public Path compileToPdf(Path texFile) {
        return compileToPdf(texFile, null, "pdflatex");
    }

    /**
     * Compile LaTeX to PDF with specified engine
     */
    public Path compileToPdf(Path texFile, String engine) {
        return compileToPdf(texFile, null, engine);
    }

    /**
     * Validate and normalize LaTeX engine name
     */
    private String validateEngine(String engine) {
        if (engine == null || engine.trim().isEmpty()) {
            return "pdflatex";
        }

        String normalized = engine.trim().toLowerCase();
        switch (normalized) {
            case "pdflatex":
            case "xelatex":
            case "lualatex":
                return normalized;
            default:
                // Check if engine exists in PATH
                try {
                    Process process = new ProcessBuilder(normalized, "--version").start();
                    if (process.waitFor(2, TimeUnit.SECONDS) && process.exitValue() == 0) {
                        return normalized;
                    }
                } catch (Exception e) {
                    // Engine not found
                }
                return null;
        }
    }

    /**
     * Extract error message from LaTeX output
     */
    private String extractErrorMessage(String output) {
        if (output == null || output.isEmpty()) {
            return "Unknown error";
        }

        // Look for common LaTeX error patterns
        String[] lines = output.split("\n");
        for (int i = 0; i < lines.length; i++) {
            String line = lines[i];
            if (line.contains("! ") && !line.startsWith("!pdfTeX error:")) {
                // Found an error line, try to get context
                StringBuilder error = new StringBuilder(line);
                // Add next few lines for context
                for (int j = i + 1; j < Math.min(i + 3, lines.length); j++) {
                    error.append("\n").append(lines[j]);
                }
                return error.toString();
            }
        }

        // If no specific error found, return first 5 lines
        StringBuilder firstLines = new StringBuilder();
        for (int i = 0; i < Math.min(5, lines.length); i++) {
            firstLines.append(lines[i]).append("\n");
        }
        return firstLines.toString();
    }

    /**
     * Check if a LaTeX engine is available
     */
    public boolean isEngineAvailable(String engine) {
        String validEngine = validateEngine(engine);
        if (validEngine == null) {
            return false;
        }

        try {
            Process process = new ProcessBuilder(validEngine, "--version").start();
            return process.waitFor(2, TimeUnit.SECONDS) && process.exitValue() == 0;
        } catch (Exception e) {
            return false;
        }
    }

    /**
     * Get available LaTeX engines
     */
    public String[] getAvailableEngines() {
        java.util.List<String> engines = new java.util.ArrayList<>();
        String[] possibleEngines = {"pdflatex", "xelatex", "lualatex"};

        for (String engine : possibleEngines) {
            if (isEngineAvailable(engine)) {
                engines.add(engine);
            }
        }

        return engines.toArray(new String[0]);
    }

    /**
     * Check if LaTeX file contains dangerous shell escape commands.
     * This is defense in depth - -no-shell-escape flag is the primary protection.
     */
    private boolean containsDangerousCommands(Path texFile) throws IOException {
        String content = Files.readString(texFile);

        // Check for shell escape commands (even though -no-shell-escape blocks them)
        String[] dangerousPatterns = {
            "\\write18",
            "\\immediate\\write18",
            "\\input{|",
            "\\openin15=|",
            "\\include{|",
            "\\ShellEscape"
        };

        for (String pattern : dangerousPatterns) {
            if (content.contains(pattern)) {
                LOGGER.severe("Dangerous LaTeX command detected in " + texFile + ": " + pattern);
                return true;
            }
        }
        return false;
    }

    /**
     * Validate LaTeX syntax before compilation
     * Checks for common syntax errors that would cause compilation to fail
     */
    private boolean validateLatexSyntax(Path texFile, String engine) {
        try {
            String content = Files.readString(texFile);

            // Check for common LaTeX syntax errors

            // 1. Check for mismatched braces
            int openBraces = 0;
            int closeBraces = 0;
            for (int i = 0; i < content.length(); i++) {
                char c = content.charAt(i);
                if (c == '{') openBraces++;
                if (c == '}') closeBraces++;
            }
            if (openBraces != closeBraces) {
                LOGGER.warning("Mismatched braces in LaTeX file: " + openBraces + " opening vs " + closeBraces + " closing");
                return false;
            }

            // 2. Check for common tabular errors
            if (content.contains("\\begin{tabular")) {
                // Check for broken column specifications
                String[] lines = content.split("\n");
                for (int i = 0; i < lines.length; i++) {
                    String line = lines[i];
                    if (line.contains("\\begin{tabular")) {
                        // Look for column specification
                        int start = line.indexOf('{', line.indexOf("\\begin{tabular"));
                        if (start != -1) {
                            int end = line.indexOf('}', start);
                            if (end == -1) {
                                LOGGER.warning("Broken tabular column specification on line " + (i + 1));
                                return false;
                            }
                        }
                    }
                }
            }

            // 3. Check for Unicode characters when using pdflatex
            if ("pdflatex".equals(engine)) {
                // Simple check for non-ASCII characters
                for (int i = 0; i < content.length(); i++) {
                    char c = content.charAt(i);
                    if (c > 127 && c != '\n' && c != '\r' && c != '\t') {
                        LOGGER.warning("Non-ASCII character detected at position " + i + " (char code: " + (int)c +
                                     "). Use xelatex or lualatex for Unicode support.");
                        return false;
                    }
                }
            }

            // 4. Check for common LaTeX command errors
            String[] problematicPatterns = {
                "@{\\\\",  // Double backslash in column spec
                "\\\\\\[", // Escaped bracket
                "\\\\\\]"  // Escaped bracket
            };

            for (String pattern : problematicPatterns) {
                if (content.contains(pattern)) {
                    LOGGER.warning("Potential LaTeX syntax issue detected: " + pattern);
                    // Don't fail immediately, just warn
                }
            }

            return true;

        } catch (IOException e) {
            LOGGER.warning("Failed to read LaTeX file for syntax validation: " + e.getMessage());
            return false;
        }
    }
}