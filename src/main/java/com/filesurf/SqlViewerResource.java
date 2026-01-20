package com.filesurf;

import com.filesurf.service.MetricsService;
import com.filesurf.service.SessionManager;
import jakarta.inject.Inject;
import jakarta.ws.rs.*;
import jakarta.ws.rs.core.MediaType;
import jakarta.ws.rs.core.Response;
import jakarta.ws.rs.core.StreamingOutput;

import java.io.IOException;
import java.io.OutputStreamWriter;
import java.io.PrintWriter;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.sql.*;
import java.util.*;
import java.util.logging.Logger;

/**
 * SQLite Database Viewer Resource for browsing and querying SQLite databases
 * that users have uploaded to their session directories.
 */
@Path("/app/explorer/sql")
public class SqlViewerResource {

    private static final Logger LOGGER = Logger.getLogger(SqlViewerResource.class.getName());

    // Valid SQLite file extensions
    private static final Set<String> VALID_EXTENSIONS = Set.of("db", "sqlite", "sqlite3");

    // Maximum page size for data queries
    private static final int MAX_PAGE_SIZE = 500;
    private static final int DEFAULT_PAGE_SIZE = 50;

    @Inject
    SessionManager sessionManager;

    @Inject
    MetricsService metricsService;

    /**
     * List all tables in the SQLite database file.
     */
    @GET
    @Path("/tables")
    @Produces(MediaType.APPLICATION_JSON)
    public Response listTables(
            @HeaderParam("X-Session-ID") String sessionId,
            @HeaderParam("X-User-ID") String headerUserId,
            @CookieParam("filesurf_userId") String cookieUserId,
            @QueryParam("path") String dbPath) {

        LOGGER.info("Listing tables for session: " + sessionId + ", path: " + dbPath);

        // Validate session and user
        Response validationError = validateSessionAndUser(sessionId, headerUserId, cookieUserId);
        if (validationError != null) {
            return validationError;
        }

        if (dbPath == null || dbPath.trim().isEmpty()) {
            return errorResponse(Response.Status.BAD_REQUEST, "No database path provided");
        }

        String userId = resolveUserId(headerUserId, cookieUserId);

        try {
            java.nio.file.Path dbFile = resolveAndValidateDbPath(sessionId, userId, dbPath);
            if (dbFile == null) {
                return errorResponse(Response.Status.BAD_REQUEST, "Invalid database path");
            }

            if (!Files.exists(dbFile)) {
                return errorResponse(Response.Status.NOT_FOUND, "Database file not found");
            }

            List<String> tables = new ArrayList<>();
            try (Connection conn = openConnection(dbFile)) {
                DatabaseMetaData metaData = conn.getMetaData();
                try (ResultSet rs = metaData.getTables(null, null, "%", new String[]{"TABLE"})) {
                    while (rs.next()) {
                        tables.add(rs.getString("TABLE_NAME"));
                    }
                }
            }

            Collections.sort(tables);

            Map<String, Object> response = new HashMap<>();
            response.put("success", true);
            response.put("tables", tables);

            metricsService.incrementFileOperations();
            metricsService.trackUserActivity(userId);

            return Response.ok(response).build();

        } catch (SQLException e) {
            return handleSqlException(e, "sql_list_tables");
        } catch (IOException e) {
            LOGGER.severe("IO error listing tables: " + e.getMessage());
            metricsService.incrementErrors("sql_list_tables");
            return errorResponse(Response.Status.INTERNAL_SERVER_ERROR, "IO error: " + e.getMessage());
        }
    }

    /**
     * Get the schema of a specific table.
     */
    @GET
    @Path("/schema/{tableName}")
    @Produces(MediaType.APPLICATION_JSON)
    public Response getTableSchema(
            @HeaderParam("X-Session-ID") String sessionId,
            @HeaderParam("X-User-ID") String headerUserId,
            @CookieParam("filesurf_userId") String cookieUserId,
            @PathParam("tableName") String tableName,
            @QueryParam("path") String dbPath) {

        LOGGER.info("Getting schema for table: " + tableName + ", session: " + sessionId + ", path: " + dbPath);

        // Validate session and user
        Response validationError = validateSessionAndUser(sessionId, headerUserId, cookieUserId);
        if (validationError != null) {
            return validationError;
        }

        if (dbPath == null || dbPath.trim().isEmpty()) {
            return errorResponse(Response.Status.BAD_REQUEST, "No database path provided");
        }

        if (tableName == null || tableName.trim().isEmpty()) {
            return errorResponse(Response.Status.BAD_REQUEST, "No table name provided");
        }

        String userId = resolveUserId(headerUserId, cookieUserId);

        try {
            java.nio.file.Path dbFile = resolveAndValidateDbPath(sessionId, userId, dbPath);
            if (dbFile == null) {
                return errorResponse(Response.Status.BAD_REQUEST, "Invalid database path");
            }

            if (!Files.exists(dbFile)) {
                return errorResponse(Response.Status.NOT_FOUND, "Database file not found");
            }

            // Validate table name exists
            if (!tableExists(dbFile, tableName)) {
                return errorResponse(Response.Status.NOT_FOUND, "Table not found: " + tableName);
            }

            List<Map<String, Object>> columns = new ArrayList<>();
            try (Connection conn = openConnection(dbFile)) {
                // Use PRAGMA table_info for SQLite-specific column information
                try (Statement stmt = conn.createStatement();
                     ResultSet rs = stmt.executeQuery("PRAGMA table_info(" + quoteIdentifier(tableName) + ")")) {
                    while (rs.next()) {
                        Map<String, Object> column = new HashMap<>();
                        column.put("name", rs.getString("name"));
                        column.put("type", rs.getString("type"));
                        column.put("nullable", rs.getInt("notnull") == 0);
                        column.put("primaryKey", rs.getInt("pk") > 0);
                        String defaultValue = rs.getString("dflt_value");
                        column.put("defaultValue", defaultValue);
                        columns.add(column);
                    }
                }
            }

            Map<String, Object> response = new HashMap<>();
            response.put("success", true);
            response.put("columns", columns);

            metricsService.incrementFileOperations();
            metricsService.trackUserActivity(userId);

            return Response.ok(response).build();

        } catch (SQLException e) {
            return handleSqlException(e, "sql_get_schema");
        } catch (IOException e) {
            LOGGER.severe("IO error getting schema: " + e.getMessage());
            metricsService.incrementErrors("sql_get_schema");
            return errorResponse(Response.Status.INTERNAL_SERVER_ERROR, "IO error: " + e.getMessage());
        }
    }

    /**
     * Get paginated data from a table with optional search and filters.
     */
    @GET
    @Path("/data/{tableName}")
    @Produces(MediaType.APPLICATION_JSON)
    public Response getTableData(
            @HeaderParam("X-Session-ID") String sessionId,
            @HeaderParam("X-User-ID") String headerUserId,
            @CookieParam("filesurf_userId") String cookieUserId,
            @PathParam("tableName") String tableName,
            @QueryParam("path") String dbPath,
            @QueryParam("page") @DefaultValue("0") int page,
            @QueryParam("pageSize") @DefaultValue("50") int pageSize,
            @QueryParam("search") String search,
            @QueryParam("filters") String filtersJson,
            @QueryParam("sortColumn") String sortColumn,
            @QueryParam("sortDirection") @DefaultValue("asc") String sortDirection) {

        LOGGER.info("Getting data for table: " + tableName + ", session: " + sessionId + ", page: " + page);

        // Validate session and user
        Response validationError = validateSessionAndUser(sessionId, headerUserId, cookieUserId);
        if (validationError != null) {
            return validationError;
        }

        if (dbPath == null || dbPath.trim().isEmpty()) {
            return errorResponse(Response.Status.BAD_REQUEST, "No database path provided");
        }

        if (tableName == null || tableName.trim().isEmpty()) {
            return errorResponse(Response.Status.BAD_REQUEST, "No table name provided");
        }

        String userId = resolveUserId(headerUserId, cookieUserId);

        // Validate and clamp pagination parameters
        page = Math.max(0, page);
        pageSize = Math.min(Math.max(1, pageSize), MAX_PAGE_SIZE);

        try {
            java.nio.file.Path dbFile = resolveAndValidateDbPath(sessionId, userId, dbPath);
            if (dbFile == null) {
                return errorResponse(Response.Status.BAD_REQUEST, "Invalid database path");
            }

            if (!Files.exists(dbFile)) {
                return errorResponse(Response.Status.NOT_FOUND, "Database file not found");
            }

            // Validate table name exists
            if (!tableExists(dbFile, tableName)) {
                return errorResponse(Response.Status.NOT_FOUND, "Table not found: " + tableName);
            }

            // Parse filters JSON
            Map<String, String> filters = parseFilters(filtersJson);

            // Get column names for the table
            List<String> columnNames = getColumnNames(dbFile, tableName);

            // Validate sortColumn if provided
            if (sortColumn != null && !sortColumn.isEmpty() && !columnNames.contains(sortColumn)) {
                return errorResponse(Response.Status.BAD_REQUEST, "Invalid sort column: " + sortColumn);
            }

            // Validate sortDirection
            if (!sortDirection.equalsIgnoreCase("asc") && !sortDirection.equalsIgnoreCase("desc")) {
                sortDirection = "asc";
            }

            try (Connection conn = openConnection(dbFile)) {
                // Build the query with filters and search
                StringBuilder whereClause = new StringBuilder();
                List<Object> params = new ArrayList<>();

                buildWhereClause(whereClause, params, columnNames, search, filters);

                // Get total count
                String countSql = "SELECT COUNT(*) FROM " + quoteIdentifier(tableName) + whereClause;
                long totalRows;
                try (PreparedStatement stmt = conn.prepareStatement(countSql)) {
                    setParameters(stmt, params);
                    try (ResultSet rs = stmt.executeQuery()) {
                        rs.next();
                        totalRows = rs.getLong(1);
                    }
                }

                // Build data query with pagination
                StringBuilder dataSql = new StringBuilder();
                dataSql.append("SELECT * FROM ").append(quoteIdentifier(tableName));
                dataSql.append(whereClause);

                if (sortColumn != null && !sortColumn.isEmpty()) {
                    dataSql.append(" ORDER BY ").append(quoteIdentifier(sortColumn));
                    dataSql.append(" ").append(sortDirection.toUpperCase());
                }

                dataSql.append(" LIMIT ? OFFSET ?");
                params.add(pageSize);
                params.add(page * pageSize);

                List<Map<String, Object>> data = new ArrayList<>();
                try (PreparedStatement stmt = conn.prepareStatement(dataSql.toString())) {
                    setParameters(stmt, params);
                    try (ResultSet rs = stmt.executeQuery()) {
                        int colCount = rs.getMetaData().getColumnCount();
                        while (rs.next()) {
                            Map<String, Object> row = new LinkedHashMap<>();
                            for (int i = 1; i <= colCount; i++) {
                                row.put(columnNames.get(i - 1), rs.getObject(i));
                            }
                            data.add(row);
                        }
                    }
                }

                long totalPages = (totalRows + pageSize - 1) / pageSize;

                Map<String, Object> response = new HashMap<>();
                response.put("success", true);
                response.put("columns", columnNames);
                response.put("rows", data);
                response.put("totalRows", totalRows);
                response.put("page", page);
                response.put("pageSize", pageSize);
                response.put("totalPages", totalPages);

                metricsService.incrementFileOperations();
                metricsService.trackUserActivity(userId);

                return Response.ok(response).build();
            }

        } catch (SQLException e) {
            return handleSqlException(e, "sql_get_data");
        } catch (IOException e) {
            LOGGER.severe("IO error getting data: " + e.getMessage());
            metricsService.incrementErrors("sql_get_data");
            return errorResponse(Response.Status.INTERNAL_SERVER_ERROR, "IO error: " + e.getMessage());
        }
    }

    /**
     * Execute arbitrary SQL query (supports SELECT, INSERT, UPDATE, DELETE).
     */
    @POST
    @Path("/query")
    @Consumes(MediaType.APPLICATION_JSON)
    @Produces(MediaType.APPLICATION_JSON)
    public Response executeQuery(
            @HeaderParam("X-Session-ID") String sessionId,
            @HeaderParam("X-User-ID") String headerUserId,
            @CookieParam("filesurf_userId") String cookieUserId,
            @QueryParam("path") String dbPath,
            Map<String, Object> body) {

        LOGGER.info("Executing SQL query for session: " + sessionId);

        // Validate session and user
        Response validationError = validateSessionAndUser(sessionId, headerUserId, cookieUserId);
        if (validationError != null) {
            return validationError;
        }

        if (dbPath == null || dbPath.trim().isEmpty()) {
            return errorResponse(Response.Status.BAD_REQUEST, "No database path provided");
        }

        if (body == null || !body.containsKey("sql")) {
            return errorResponse(Response.Status.BAD_REQUEST, "No SQL query provided");
        }

        String sql = (String) body.get("sql");
        if (sql == null || sql.trim().isEmpty()) {
            return errorResponse(Response.Status.BAD_REQUEST, "Empty SQL query");
        }

        @SuppressWarnings("unchecked")
        List<Object> params = body.containsKey("params") ? (List<Object>) body.get("params") : new ArrayList<>();

        String userId = resolveUserId(headerUserId, cookieUserId);

        try {
            java.nio.file.Path dbFile = resolveAndValidateDbPath(sessionId, userId, dbPath);
            if (dbFile == null) {
                return errorResponse(Response.Status.BAD_REQUEST, "Invalid database path");
            }

            if (!Files.exists(dbFile)) {
                return errorResponse(Response.Status.NOT_FOUND, "Database file not found");
            }

            // Split SQL into statements (handling semicolons in strings)
            List<String> statements = splitSqlStatements(sql);
            if (statements.isEmpty()) {
                return errorResponse(Response.Status.BAD_REQUEST, "No valid SQL statements found");
            }

            List<Map<String, Object>> results = new ArrayList<>();
            int totalAffectedRows = 0;
            int statementIndex = 0;

            try (Connection conn = openConnection(dbFile)) {
                for (String statement : statements) {
                    statementIndex++;
                    String trimmedStatement = statement.trim();
                    if (trimmedStatement.isEmpty()) {
                        continue;
                    }

                    try {
                        Map<String, Object> result = executeSingleStatement(conn, trimmedStatement,
                                statementIndex == 1 ? params : new ArrayList<>());
                        results.add(result);

                        if ("update".equals(result.get("type"))) {
                            totalAffectedRows += (Integer) result.getOrDefault("affectedRows", 0);
                        }
                    } catch (SQLException e) {
                        LOGGER.warning("SQL error in statement " + statementIndex + ": " + e.getMessage());
                        Map<String, Object> response = new HashMap<>();
                        response.put("success", false);
                        response.put("error", "Error in statement " + statementIndex + ": " + e.getMessage());
                        response.put("failedStatement", trimmedStatement);
                        response.put("statementIndex", statementIndex);
                        if (!results.isEmpty()) {
                            response.put("previousResults", results);
                        }
                        return Response.status(Response.Status.BAD_REQUEST).entity(response).build();
                    }
                }
            }

            // Build response based on results
            Map<String, Object> response = new HashMap<>();
            response.put("success", true);

            if (results.size() == 1) {
                // Single statement - return its result directly
                Map<String, Object> singleResult = results.get(0);
                response.putAll(singleResult);
            } else {
                // Multiple statements - return array of results
                response.put("type", "multi");
                response.put("results", results);
                response.put("totalStatements", results.size());
                response.put("totalAffectedRows", totalAffectedRows);
            }

            metricsService.incrementFileOperations();
            metricsService.trackUserActivity(userId);

            return Response.ok(response).build();

        } catch (SQLException e) {
            return handleSqlException(e, "sql_execute_query");
        } catch (IOException e) {
            LOGGER.severe("IO error executing query: " + e.getMessage());
            metricsService.incrementErrors("sql_execute_query");
            return errorResponse(Response.Status.INTERNAL_SERVER_ERROR, "IO error: " + e.getMessage());
        }
    }

    /**
     * Export table data as CSV or JSON.
     */
    @GET
    @Path("/export/{tableName}")
    public Response exportTableData(
            @HeaderParam("X-Session-ID") String sessionId,
            @HeaderParam("X-User-ID") String headerUserId,
            @CookieParam("filesurf_userId") String cookieUserId,
            @PathParam("tableName") String tableName,
            @QueryParam("path") String dbPath,
            @QueryParam("format") @DefaultValue("csv") String format,
            @QueryParam("search") String search,
            @QueryParam("filters") String filtersJson,
            @QueryParam("sortColumn") String sortColumn,
            @QueryParam("sortDirection") @DefaultValue("asc") String sortDirection) {

        LOGGER.info("Exporting table: " + tableName + " as " + format + ", session: " + sessionId);

        // Validate session and user
        Response validationError = validateSessionAndUser(sessionId, headerUserId, cookieUserId);
        if (validationError != null) {
            return validationError;
        }

        if (dbPath == null || dbPath.trim().isEmpty()) {
            return errorResponse(Response.Status.BAD_REQUEST, "No database path provided");
        }

        if (tableName == null || tableName.trim().isEmpty()) {
            return errorResponse(Response.Status.BAD_REQUEST, "No table name provided");
        }

        // Validate format
        if (!format.equalsIgnoreCase("csv") && !format.equalsIgnoreCase("json")) {
            return errorResponse(Response.Status.BAD_REQUEST, "Invalid format. Use 'csv' or 'json'");
        }

        String userId = resolveUserId(headerUserId, cookieUserId);

        try {
            java.nio.file.Path dbFile = resolveAndValidateDbPath(sessionId, userId, dbPath);
            if (dbFile == null) {
                return errorResponse(Response.Status.BAD_REQUEST, "Invalid database path");
            }

            if (!Files.exists(dbFile)) {
                return errorResponse(Response.Status.NOT_FOUND, "Database file not found");
            }

            // Validate table name exists
            if (!tableExists(dbFile, tableName)) {
                return errorResponse(Response.Status.NOT_FOUND, "Table not found: " + tableName);
            }

            // Parse filters JSON
            Map<String, String> filters = parseFilters(filtersJson);

            // Get column names for the table
            List<String> columnNames = getColumnNames(dbFile, tableName);

            // Validate sortColumn if provided
            if (sortColumn != null && !sortColumn.isEmpty() && !columnNames.contains(sortColumn)) {
                return errorResponse(Response.Status.BAD_REQUEST, "Invalid sort column: " + sortColumn);
            }

            // Validate sortDirection
            if (!sortDirection.equalsIgnoreCase("asc") && !sortDirection.equalsIgnoreCase("desc")) {
                sortDirection = "asc";
            }

            final String finalSortDirection = sortDirection;
            final String finalSortColumn = sortColumn;

            String contentType = format.equalsIgnoreCase("csv") ? "text/csv" : MediaType.APPLICATION_JSON;
            String fileExtension = format.equalsIgnoreCase("csv") ? ".csv" : ".json";
            String fileName = tableName + fileExtension;

            StreamingOutput stream = output -> {
                try (Connection conn = openConnection(dbFile);
                     PrintWriter writer = new PrintWriter(new OutputStreamWriter(output, StandardCharsets.UTF_8))) {

                    // Build the query with filters and search
                    StringBuilder whereClause = new StringBuilder();
                    List<Object> params = new ArrayList<>();
                    buildWhereClause(whereClause, params, columnNames, search, filters);

                    StringBuilder sql = new StringBuilder();
                    sql.append("SELECT * FROM ").append(quoteIdentifier(tableName));
                    sql.append(whereClause);

                    if (finalSortColumn != null && !finalSortColumn.isEmpty()) {
                        sql.append(" ORDER BY ").append(quoteIdentifier(finalSortColumn));
                        sql.append(" ").append(finalSortDirection.toUpperCase());
                    }

                    try (PreparedStatement stmt = conn.prepareStatement(sql.toString())) {
                        setParameters(stmt, params);
                        try (ResultSet rs = stmt.executeQuery()) {
                            if (format.equalsIgnoreCase("csv")) {
                                exportToCsv(rs, columnNames, writer);
                            } else {
                                exportToJson(rs, columnNames, writer);
                            }
                        }
                    }

                    writer.flush();
                } catch (SQLException e) {
                    throw new IOException("SQL error during export: " + e.getMessage(), e);
                }
            };

            metricsService.incrementFileOperations();
            metricsService.trackUserActivity(userId);

            return Response.ok(stream)
                    .type(contentType)
                    .header("Content-Disposition", "attachment; filename=\"" + fileName + "\"")
                    .build();

        } catch (SQLException e) {
            return handleSqlException(e, "sql_export");
        } catch (IOException e) {
            LOGGER.severe("IO error exporting data: " + e.getMessage());
            metricsService.incrementErrors("sql_export");
            return errorResponse(Response.Status.INTERNAL_SERVER_ERROR, "IO error: " + e.getMessage());
        }
    }

    // ==================== Helper Methods ====================

    private String resolveUserId(String headerUserId, String cookieUserId) {
        if (headerUserId != null && !headerUserId.isBlank()) {
            return headerUserId;
        }
        if (cookieUserId != null && !cookieUserId.isBlank()) {
            return cookieUserId;
        }
        return null;
    }

    private Response validateSessionAndUser(String sessionId, String headerUserId, String cookieUserId) {
        if (sessionId == null || sessionId.trim().isEmpty()) {
            return errorResponse(Response.Status.BAD_REQUEST, "No session ID provided");
        }

        String userId = resolveUserId(headerUserId, cookieUserId);
        if (userId == null || userId.trim().isEmpty()) {
            return errorResponse(Response.Status.BAD_REQUEST, "No user ID provided");
        }

        return null;
    }

    private Response errorResponse(Response.Status status, String message) {
        Map<String, Object> error = new HashMap<>();
        error.put("success", false);
        error.put("error", message);
        return Response.status(status).entity(error).type(MediaType.APPLICATION_JSON).build();
    }

    /**
     * Handle SQLException and return appropriate HTTP response.
     * Distinguishes between file/data errors (422) and actual server errors (500).
     */
    private Response handleSqlException(SQLException e, String operation) {
        String message = e.getMessage();

        // Check for SQLite-specific error codes indicating file/data issues
        // These are client-side data problems, not server errors
        if (message != null) {
            String upperMessage = message.toUpperCase();

            if (upperMessage.contains("SQLITE_CORRUPT") || upperMessage.contains("MALFORMED")) {
                LOGGER.warning("Corrupted database file during " + operation + ": " + message);
                return errorResponse(Response.Status.fromStatusCode(422),
                        "The database file is corrupted or incomplete. Please re-upload a valid SQLite database.");
            }

            if (upperMessage.contains("SQLITE_NOTADB") || upperMessage.contains("NOT A DATABASE")) {
                LOGGER.warning("Invalid database file during " + operation + ": " + message);
                return errorResponse(Response.Status.fromStatusCode(422),
                        "The file is not a valid SQLite database.");
            }

            if (upperMessage.contains("SQLITE_CANTOPEN") || upperMessage.contains("UNABLE TO OPEN")) {
                LOGGER.warning("Cannot open database file during " + operation + ": " + message);
                return errorResponse(Response.Status.fromStatusCode(422),
                        "Unable to open the database file. The file may be corrupted or in an unsupported format.");
            }

            if (upperMessage.contains("ENCRYPTED") || upperMessage.contains("SQLITE_AUTH")) {
                LOGGER.warning("Encrypted database file during " + operation + ": " + message);
                return errorResponse(Response.Status.fromStatusCode(422),
                        "The database file appears to be encrypted. Encrypted databases are not supported.");
            }
        }

        // For other SQL errors, it's likely a server-side issue
        LOGGER.severe("SQL error during " + operation + ": " + message);
        metricsService.incrementErrors(operation);
        return errorResponse(Response.Status.INTERNAL_SERVER_ERROR, "SQL error: " + message);
    }

    /**
     * Sanitize path to prevent directory traversal
     */
    private String sanitizePath(String path) {
        if (path == null) return "";

        // Remove any leading/trailing slashes and normalize
        path = path.trim().replace("\\", "/");

        // Remove any attempt at directory traversal
        path = path.replace("../", "").replace("..\\", "");

        // Split by path separator and filter out empty parts
        String[] parts = path.split("/");
        StringBuilder sanitized = new StringBuilder();

        for (String part : parts) {
            if (!part.isEmpty() && !part.equals(".")) {
                if (sanitized.length() > 0) {
                    sanitized.append("/");
                }
                sanitized.append(part);
            }
        }

        return sanitized.toString();
    }

    /**
     * Resolve and validate the database file path within the session directory.
     */
    private java.nio.file.Path resolveAndValidateDbPath(String sessionId, String userId, String dbPath)
            throws IOException {
        // Get session directory
        java.nio.file.Path sessionDir = sessionManager.getSessionDirectory(sessionId, userId);

        // Sanitize and resolve the file path
        String sanitizedPath = sanitizePath(dbPath);
        java.nio.file.Path targetFile = sessionDir.resolve(sanitizedPath).normalize();

        // Ensure the file is within the session directory
        if (!targetFile.startsWith(sessionDir)) {
            return null;
        }

        // Validate file extension
        String fileName = targetFile.getFileName().toString().toLowerCase();
        int dotIndex = fileName.lastIndexOf('.');
        if (dotIndex <= 0) {
            return null;
        }

        String extension = fileName.substring(dotIndex + 1);
        if (!VALID_EXTENSIONS.contains(extension)) {
            return null;
        }

        return targetFile;
    }

    /**
     * Open a connection to the SQLite database.
     */
    private Connection openConnection(java.nio.file.Path dbFile) throws SQLException {
        String url = "jdbc:sqlite:" + dbFile.toAbsolutePath();
        return DriverManager.getConnection(url);
    }

    /**
     * Check if a table exists in the database.
     */
    private boolean tableExists(java.nio.file.Path dbFile, String tableName) throws SQLException {
        try (Connection conn = openConnection(dbFile)) {
            DatabaseMetaData metaData = conn.getMetaData();
            try (ResultSet rs = metaData.getTables(null, null, tableName, new String[]{"TABLE"})) {
                return rs.next();
            }
        }
    }

    /**
     * Get column names for a table.
     */
    private List<String> getColumnNames(java.nio.file.Path dbFile, String tableName) throws SQLException {
        List<String> columns = new ArrayList<>();
        try (Connection conn = openConnection(dbFile);
             Statement stmt = conn.createStatement();
             ResultSet rs = stmt.executeQuery("PRAGMA table_info(" + quoteIdentifier(tableName) + ")")) {
            while (rs.next()) {
                columns.add(rs.getString("name"));
            }
        }
        return columns;
    }

    /**
     * Quote a SQL identifier to prevent injection.
     */
    private String quoteIdentifier(String identifier) {
        // SQLite uses double quotes for identifiers
        return "\"" + identifier.replace("\"", "\"\"") + "\"";
    }

    /**
     * Parse filters JSON string into a map.
     */
    private Map<String, String> parseFilters(String filtersJson) {
        Map<String, String> filters = new HashMap<>();
        if (filtersJson == null || filtersJson.trim().isEmpty()) {
            return filters;
        }

        try {
            // Simple JSON parsing for {"key": "value"} format
            String json = filtersJson.trim();
            if (json.startsWith("{") && json.endsWith("}")) {
                json = json.substring(1, json.length() - 1).trim();
                if (!json.isEmpty()) {
                    // Split by comma, but be careful with values containing commas
                    int depth = 0;
                    StringBuilder current = new StringBuilder();
                    List<String> pairs = new ArrayList<>();

                    for (char c : json.toCharArray()) {
                        if (c == '{' || c == '[') depth++;
                        else if (c == '}' || c == ']') depth--;
                        else if (c == ',' && depth == 0) {
                            pairs.add(current.toString().trim());
                            current = new StringBuilder();
                            continue;
                        }
                        current.append(c);
                    }
                    if (current.length() > 0) {
                        pairs.add(current.toString().trim());
                    }

                    for (String pair : pairs) {
                        int colonIndex = pair.indexOf(':');
                        if (colonIndex > 0) {
                            String key = unquoteString(pair.substring(0, colonIndex).trim());
                            String value = unquoteString(pair.substring(colonIndex + 1).trim());
                            filters.put(key, value);
                        }
                    }
                }
            }
        } catch (Exception e) {
            LOGGER.warning("Failed to parse filters JSON: " + e.getMessage());
        }

        return filters;
    }

    /**
     * Remove quotes from a JSON string value.
     */
    private String unquoteString(String s) {
        if (s == null) return null;
        s = s.trim();
        if ((s.startsWith("\"") && s.endsWith("\"")) || (s.startsWith("'") && s.endsWith("'"))) {
            return s.substring(1, s.length() - 1);
        }
        return s;
    }

    /**
     * Build WHERE clause for search and filters.
     */
    private void buildWhereClause(StringBuilder whereClause, List<Object> params,
                                   List<String> columnNames, String search, Map<String, String> filters) {
        List<String> conditions = new ArrayList<>();

        // Add search condition (search across all text columns)
        if (search != null && !search.trim().isEmpty()) {
            List<String> searchConditions = new ArrayList<>();
            for (String column : columnNames) {
                searchConditions.add(quoteIdentifier(column) + " LIKE ?");
                params.add("%" + search + "%");
            }
            if (!searchConditions.isEmpty()) {
                conditions.add("(" + String.join(" OR ", searchConditions) + ")");
            }
        }

        // Add filter conditions (exact match)
        if (filters != null && !filters.isEmpty()) {
            for (Map.Entry<String, String> entry : filters.entrySet()) {
                String column = entry.getKey();
                // Only add filter if column exists
                if (columnNames.contains(column)) {
                    conditions.add(quoteIdentifier(column) + " = ?");
                    params.add(entry.getValue());
                }
            }
        }

        if (!conditions.isEmpty()) {
            whereClause.append(" WHERE ").append(String.join(" AND ", conditions));
        }
    }

    /**
     * Set parameters on a prepared statement.
     */
    private void setParameters(PreparedStatement stmt, List<Object> params) throws SQLException {
        for (int i = 0; i < params.size(); i++) {
            Object param = params.get(i);
            if (param == null) {
                stmt.setNull(i + 1, Types.NULL);
            } else if (param instanceof Integer) {
                stmt.setInt(i + 1, (Integer) param);
            } else if (param instanceof Long) {
                stmt.setLong(i + 1, (Long) param);
            } else if (param instanceof Double) {
                stmt.setDouble(i + 1, (Double) param);
            } else if (param instanceof Boolean) {
                stmt.setBoolean(i + 1, (Boolean) param);
            } else {
                stmt.setString(i + 1, param.toString());
            }
        }
    }

    /**
     * Split SQL into multiple statements, handling semicolons in strings.
     */
    private List<String> splitSqlStatements(String sql) {
        List<String> statements = new ArrayList<>();
        StringBuilder current = new StringBuilder();
        boolean inSingleQuote = false;
        boolean inDoubleQuote = false;

        for (int i = 0; i < sql.length(); i++) {
            char c = sql.charAt(i);

            // Handle escape sequences
            if (c == '\\' && i + 1 < sql.length()) {
                current.append(c);
                current.append(sql.charAt(i + 1));
                i++;
                continue;
            }

            // Toggle quote states
            if (c == '\'' && !inDoubleQuote) {
                inSingleQuote = !inSingleQuote;
            } else if (c == '"' && !inSingleQuote) {
                inDoubleQuote = !inDoubleQuote;
            }

            // Check for statement separator
            if (c == ';' && !inSingleQuote && !inDoubleQuote) {
                String stmt = current.toString().trim();
                if (!stmt.isEmpty()) {
                    statements.add(stmt);
                }
                current = new StringBuilder();
                continue;
            }

            current.append(c);
        }

        // Add final statement
        String stmt = current.toString().trim();
        if (!stmt.isEmpty()) {
            statements.add(stmt);
        }

        return statements;
    }

    /**
     * Execute a single SQL statement and return the result.
     */
    private Map<String, Object> executeSingleStatement(Connection conn, String sql, List<Object> params)
            throws SQLException {
        Map<String, Object> result = new HashMap<>();

        // Determine if this is a query (SELECT) or update (INSERT/UPDATE/DELETE)
        String upperSql = sql.trim().toUpperCase();
        boolean isQuery = upperSql.startsWith("SELECT") || upperSql.startsWith("PRAGMA") ||
                          upperSql.startsWith("EXPLAIN");

        if (isQuery) {
            try (PreparedStatement stmt = conn.prepareStatement(sql)) {
                setParameters(stmt, params);
                try (ResultSet rs = stmt.executeQuery()) {
                    ResultSetMetaData metaData = rs.getMetaData();
                    int colCount = metaData.getColumnCount();

                    List<String> columns = new ArrayList<>();
                    for (int i = 1; i <= colCount; i++) {
                        columns.add(metaData.getColumnName(i));
                    }

                    List<List<Object>> data = new ArrayList<>();
                    while (rs.next()) {
                        List<Object> row = new ArrayList<>();
                        for (int i = 1; i <= colCount; i++) {
                            row.add(rs.getObject(i));
                        }
                        data.add(row);
                    }

                    result.put("type", "query");
                    result.put("columns", columns);
                    result.put("rows", data);
                    result.put("rowCount", data.size());
                }
            }
        } else {
            try (PreparedStatement stmt = conn.prepareStatement(sql)) {
                setParameters(stmt, params);
                int affectedRows = stmt.executeUpdate();

                result.put("type", "update");
                result.put("affectedRows", affectedRows);
            }
        }

        return result;
    }

    /**
     * Export ResultSet to CSV format.
     */
    private void exportToCsv(ResultSet rs, List<String> columnNames, PrintWriter writer) throws SQLException {
        // Write header
        writer.println(String.join(",", columnNames.stream()
                .map(this::escapeCsvField)
                .toArray(String[]::new)));

        // Write data rows
        int colCount = columnNames.size();
        while (rs.next()) {
            StringBuilder row = new StringBuilder();
            for (int i = 1; i <= colCount; i++) {
                if (i > 1) row.append(",");
                Object value = rs.getObject(i);
                row.append(escapeCsvField(value == null ? "" : value.toString()));
            }
            writer.println(row);
        }
    }

    /**
     * Escape a field for CSV output.
     */
    private String escapeCsvField(String field) {
        if (field == null) return "";
        if (field.contains(",") || field.contains("\"") || field.contains("\n") || field.contains("\r")) {
            return "\"" + field.replace("\"", "\"\"") + "\"";
        }
        return field;
    }

    /**
     * Export ResultSet to JSON format.
     */
    private void exportToJson(ResultSet rs, List<String> columnNames, PrintWriter writer) throws SQLException {
        writer.println("[");
        boolean first = true;
        int colCount = columnNames.size();

        while (rs.next()) {
            if (!first) {
                writer.println(",");
            }
            first = false;

            writer.print("  {");
            for (int i = 0; i < colCount; i++) {
                if (i > 0) writer.print(", ");
                String columnName = columnNames.get(i);
                Object value = rs.getObject(i + 1);
                writer.print("\"" + escapeJsonString(columnName) + "\": " + jsonValue(value));
            }
            writer.print("}");
        }

        writer.println();
        writer.println("]");
    }

    /**
     * Convert a value to JSON representation.
     */
    private String jsonValue(Object value) {
        if (value == null) {
            return "null";
        } else if (value instanceof Number) {
            return value.toString();
        } else if (value instanceof Boolean) {
            return value.toString();
        } else {
            return "\"" + escapeJsonString(value.toString()) + "\"";
        }
    }

    /**
     * Escape special characters for JSON string.
     */
    private String escapeJsonString(String str) {
        if (str == null) return "";
        StringBuilder sb = new StringBuilder();
        for (char c : str.toCharArray()) {
            switch (c) {
                case '"': sb.append("\\\""); break;
                case '\\': sb.append("\\\\"); break;
                case '\b': sb.append("\\b"); break;
                case '\f': sb.append("\\f"); break;
                case '\n': sb.append("\\n"); break;
                case '\r': sb.append("\\r"); break;
                case '\t': sb.append("\\t"); break;
                default:
                    if (c < ' ') {
                        sb.append(String.format("\\u%04x", (int) c));
                    } else {
                        sb.append(c);
                    }
            }
        }
        return sb.toString();
    }
}
