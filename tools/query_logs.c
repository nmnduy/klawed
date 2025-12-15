/*
 * Query Tool for API Call Logs
 *
 * A simple utility to query and display API calls from the persistence database.
 *
 * Usage:
 *   ./query_logs                    - Show last 10 API calls
 *   ./query_logs --all              - Show all API calls
 *   ./query_logs --errors           - Show only failed API calls
 *   ./query_logs --stats            - Show statistics
 *   ./query_logs --db /path/to/db   - Use specific database file
 */

#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sqlite3.h>
#include "../src/persistence.h"

void print_usage(const char *prog_name) {
    printf("API Call Log Query Tool\n\n");
    printf("Usage:\n");
    printf("  %s                    Show last 10 API calls\n", prog_name);
    printf("  %s --all              Show all API calls\n", prog_name);
    printf("  %s --errors           Show only failed API calls\n", prog_name);
    printf("  %s --stats            Show statistics\n", prog_name);
    printf("  %s --db /path/to/db   Use specific database file\n\n", prog_name);
}

void print_call(sqlite3_stmt *stmt) {
    int id = sqlite3_column_int(stmt, 0);
    const char *timestamp = (const char*)sqlite3_column_text(stmt, 1);
    const char *api_base_url = (const char*)sqlite3_column_text(stmt, 2);
    const char *model = (const char*)sqlite3_column_text(stmt, 3);
    const char *status = (const char*)sqlite3_column_text(stmt, 4);
    int http_status = sqlite3_column_int(stmt, 5);
    const char *error_message = (const char*)sqlite3_column_text(stmt, 6);
    int duration_ms = sqlite3_column_int(stmt, 7);
    int tool_count = sqlite3_column_int(stmt, 8);

    printf("\n[ID: %d] %s\n", id, timestamp);
    printf("  Provider: %s\n", api_base_url);
    printf("  Model: %s\n", model);
    printf("  Status: %s", status);
    if (strcmp(status, "success") == 0) {
        printf(" (HTTP %d)\n", http_status);
    } else {
        printf(" (HTTP %d) - %s\n", http_status, error_message ? error_message : "Unknown error");
    }
    printf("  Duration: %d ms\n", duration_ms);
    printf("  Tools: %d\n", tool_count);
}

void show_calls(sqlite3 *db, int limit, int errors_only) {
    const char *sql;
    if (errors_only) {
        sql = "SELECT id, timestamp, api_base_url, model, status, http_status, error_message, "
              "duration_ms, tool_count FROM api_calls WHERE status='error' "
              "ORDER BY created_at DESC";
    } else if (limit > 0) {
        sql = "SELECT id, timestamp, api_base_url, model, status, http_status, error_message, "
              "duration_ms, tool_count FROM api_calls ORDER BY created_at DESC LIMIT ?";
    } else {
        sql = "SELECT id, timestamp, api_base_url, model, status, http_status, error_message, "
              "duration_ms, tool_count FROM api_calls ORDER BY created_at DESC";
    }

    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Failed to prepare statement: %s\n", sqlite3_errmsg(db));
        return;
    }

    if (limit > 0 && !errors_only) {
        sqlite3_bind_int(stmt, 1, limit);
    }

    int count = 0;
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        print_call(stmt);
        count++;
    }

    if (count == 0) {
        printf("No API calls found.\n");
    } else {
        printf("\nTotal: %d calls\n", count);
    }

    sqlite3_finalize(stmt);
}

void show_stats(sqlite3 *db) {
    const char *sql =
        "SELECT "
        "  COUNT(*) as total_calls, "
        "  SUM(CASE WHEN status='success' THEN 1 ELSE 0 END) as success_count, "
        "  SUM(CASE WHEN status='error' THEN 1 ELSE 0 END) as error_count, "
        "  AVG(duration_ms) as avg_duration, "
        "  MIN(duration_ms) as min_duration, "
        "  MAX(duration_ms) as max_duration, "
        "  SUM(tool_count) as total_tools "
        "FROM api_calls";

    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Failed to prepare statement: %s\n", sqlite3_errmsg(db));
        return;
    }

    if (sqlite3_step(stmt) == SQLITE_ROW) {
        int total_calls = sqlite3_column_int(stmt, 0);
        int success_count = sqlite3_column_int(stmt, 1);
        int error_count = sqlite3_column_int(stmt, 2);
        double avg_duration = sqlite3_column_double(stmt, 3);
        int min_duration = sqlite3_column_int(stmt, 4);
        int max_duration = sqlite3_column_int(stmt, 5);
        int total_tools = sqlite3_column_int(stmt, 6);

        printf("\n=== API Call Statistics ===\n");
        printf("Total API calls: %d\n", total_calls);
        printf("  Successful: %d (%.1f%%)\n", success_count,
               total_calls > 0 ? (success_count * 100.0 / total_calls) : 0);
        printf("  Failed: %d (%.1f%%)\n", error_count,
               total_calls > 0 ? (error_count * 100.0 / total_calls) : 0);
        printf("\nDuration:\n");
        printf("  Average: %.1f ms\n", avg_duration);
        printf("  Min: %d ms\n", min_duration);
        printf("  Max: %d ms\n", max_duration);
        printf("\nTotal tool invocations: %d\n", total_tools);

        if (total_calls > 0) {
            printf("Average tools per call: %.2f\n", (double)total_tools / total_calls);
        }
    }

    sqlite3_finalize(stmt);

    // Show model breakdown
    const char *model_sql =
        "SELECT model, COUNT(*) as count FROM api_calls GROUP BY model ORDER BY count DESC";

    rc = sqlite3_prepare_v2(db, model_sql, -1, &stmt, NULL);
    if (rc == SQLITE_OK) {
        printf("\n=== Models Used ===\n");
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const char *model = (const char*)sqlite3_column_text(stmt, 0);
            int count = sqlite3_column_int(stmt, 1);
            printf("  %s: %d calls\n", model, count);
        }
        sqlite3_finalize(stmt);
    }
}

int main(int argc, char *argv[]) {
    const char *db_path = NULL;
    int show_all = 0;
    int show_errors = 0;
    int show_statistics = 0;

    // Parse arguments
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print_usage(argv[0]);
            return 0;
        } else if (strcmp(argv[i], "--all") == 0) {
            show_all = 1;
        } else if (strcmp(argv[i], "--errors") == 0) {
            show_errors = 1;
        } else if (strcmp(argv[i], "--stats") == 0) {
            show_statistics = 1;
        } else if (strcmp(argv[i], "--db") == 0) {
            if (i + 1 < argc) {
                db_path = argv[++i];
            } else {
                fprintf(stderr, "Error: --db requires a path argument\n");
                return 1;
            }
        } else {
            fprintf(stderr, "Unknown argument: %s\n", argv[i]);
            print_usage(argv[0]);
            return 1;
        }
    }

    // Get database path
    char *default_path = NULL;
    if (!db_path) {
        default_path = persistence_get_default_path();
        db_path = default_path;
    }

    printf("Database: %s\n", db_path);

    // Open database
    sqlite3 *db;
    int rc = sqlite3_open(db_path, &db);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Failed to open database: %s\n", sqlite3_errmsg(db));
        if (default_path) free(default_path);
        return 1;
    }

    // Execute appropriate query
    if (show_statistics) {
        show_stats(db);
    } else if (show_errors) {
        show_calls(db, 0, 1);
    } else if (show_all) {
        show_calls(db, 0, 0);
    } else {
        show_calls(db, 10, 0);
    }

    sqlite3_close(db);
    if (default_path) free(default_path);

    return 0;
}
