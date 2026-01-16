/*
 * memvid_repl.c - Interactive REPL for testing memvid files
 *
 * Usage: memvid_repl [path/to/memory.mv2]
 *
 * Commands:
 *   put <entity> <slot> <value> [kind] [relation]  - Store a memory
 *   get <entity> <slot>                            - Get current value for entity:slot
 *   search <query> [top_k]                         - Search memories
 *   entity <name>                                  - Get all memories for entity
 *   list [top_k]                                   - List recent memories
 *   commit                                         - Commit changes to disk
 *   help                                           - Show help
 *   quit/exit                                      - Exit REPL
 *
 * Build:
 *   make memvid-repl
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>

#include <bsd/string.h>

#include "../src/memvid.h"

/* ANSI color codes */
#define COLOR_RESET   "\033[0m"
#define COLOR_RED     "\033[31m"
#define COLOR_GREEN   "\033[32m"
#define COLOR_YELLOW  "\033[33m"
#define COLOR_BLUE    "\033[34m"
#define COLOR_MAGENTA "\033[35m"
#define COLOR_CYAN    "\033[36m"
#define COLOR_BOLD    "\033[1m"
#define COLOR_DIM     "\033[2m"

/* Max input line length */
#define MAX_LINE 4096
#define MAX_ARGS 16

/* Forward declarations */
static void print_help(void);
static void print_json_pretty(const char *json);
static int parse_kind(const char *kind_str);
static int parse_relation(const char *rel_str);
static const char *kind_to_str(int kind);
static const char *relation_to_str(int rel);

/* Command handlers */
static void cmd_put(MemvidHandle *handle, int argc, char **argv);
static void cmd_get(MemvidHandle *handle, int argc, char **argv);
static void cmd_search(MemvidHandle *handle, int argc, char **argv);
static void cmd_entity(MemvidHandle *handle, int argc, char **argv);
static void cmd_list(MemvidHandle *handle, int argc, char **argv);
static void cmd_commit(MemvidHandle *handle);

/*
 * Trim leading and trailing whitespace
 */
static char *trim(char *str) {
    char *end = NULL;

    /* Trim leading whitespace */
    while (isspace((unsigned char)*str)) {
        str++;
    }

    if (*str == '\0') {
        return str;
    }

    /* Trim trailing whitespace */
    end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) {
        end--;
    }
    end[1] = '\0';

    return str;
}

/*
 * Parse input line into arguments
 * Handles quoted strings for values with spaces
 */
static int parse_args(char *line, char **argv, int max_args) {
    int argc = 0;
    char *p = line;
    int in_quotes = 0;
    char *arg_start = NULL;

    while (*p != '\0' && argc < max_args) {
        /* Skip leading whitespace */
        while (*p != '\0' && isspace((unsigned char)*p) && !in_quotes) {
            p++;
        }

        if (*p == '\0') {
            break;
        }

        /* Handle quoted strings */
        if (*p == '"') {
            in_quotes = 1;
            p++;
            arg_start = p;

            /* Find closing quote */
            while (*p != '\0' && *p != '"') {
                p++;
            }

            if (*p == '"') {
                *p = '\0';
                p++;
            }

            argv[argc++] = arg_start;
            in_quotes = 0;
        } else {
            /* Regular argument */
            arg_start = p;

            while (*p != '\0' && !isspace((unsigned char)*p)) {
                p++;
            }

            if (*p != '\0') {
                *p = '\0';
                p++;
            }

            argv[argc++] = arg_start;
        }
    }

    return argc;
}

/*
 * Print help message
 */
static void print_help(void) {
    printf("\n%sMemvid REPL Commands:%s\n\n", COLOR_BOLD, COLOR_RESET);

    printf("  %sput%s <entity> <slot> <value> [kind] [relation]\n",
           COLOR_CYAN, COLOR_RESET);
    printf("      Store a memory. Use quotes for values with spaces.\n");
    printf("      kind: fact, preference, event, profile, relationship, goal\n");
    printf("      relation: sets (default), updates, extends, retracts\n\n");

    printf("  %sget%s <entity> <slot>\n", COLOR_CYAN, COLOR_RESET);
    printf("      Get the current value for an entity:slot pair\n\n");

    printf("  %ssearch%s <query> [top_k]\n", COLOR_CYAN, COLOR_RESET);
    printf("      Search memories by text query (default top_k: 10)\n\n");

    printf("  %sentity%s <name>\n", COLOR_CYAN, COLOR_RESET);
    printf("      Get all memories for a specific entity\n\n");

    printf("  %slist%s [top_k]\n", COLOR_CYAN, COLOR_RESET);
    printf("      List recent memories (default top_k: 20)\n\n");

    printf("  %scommit%s\n", COLOR_CYAN, COLOR_RESET);
    printf("      Commit pending changes to disk\n\n");

    printf("  %shelp%s\n", COLOR_CYAN, COLOR_RESET);
    printf("      Show this help message\n\n");

    printf("  %squit%s / %sexit%s\n", COLOR_CYAN, COLOR_RESET, COLOR_CYAN, COLOR_RESET);
    printf("      Exit the REPL\n\n");

    printf("%sExamples:%s\n", COLOR_BOLD, COLOR_RESET);
    printf("  put user coding_style \"prefers explicit error handling\" preference\n");
    printf("  put project.klawed language C11 fact\n");
    printf("  get user coding_style\n");
    printf("  search \"error handling\"\n");
    printf("  entity user\n\n");
}

/*
 * Parse kind string to enum value
 */
static int parse_kind(const char *kind_str) {
    if (kind_str == NULL) {
        return MEMVID_KIND_FACT;  /* Default */
    }

    if (strcmp(kind_str, "fact") == 0) {
        return MEMVID_KIND_FACT;
    }
    if (strcmp(kind_str, "preference") == 0) {
        return MEMVID_KIND_PREFERENCE;
    }
    if (strcmp(kind_str, "event") == 0) {
        return MEMVID_KIND_EVENT;
    }
    if (strcmp(kind_str, "profile") == 0) {
        return MEMVID_KIND_PROFILE;
    }
    if (strcmp(kind_str, "relationship") == 0) {
        return MEMVID_KIND_RELATIONSHIP;
    }
    if (strcmp(kind_str, "goal") == 0) {
        return MEMVID_KIND_GOAL;
    }

    return MEMVID_KIND_FACT;  /* Default */
}

/*
 * Parse relation string to enum value
 */
static int parse_relation(const char *rel_str) {
    if (rel_str == NULL) {
        return MEMVID_RELATION_SETS;  /* Default */
    }

    if (strcmp(rel_str, "sets") == 0) {
        return MEMVID_RELATION_SETS;
    }
    if (strcmp(rel_str, "updates") == 0) {
        return MEMVID_RELATION_UPDATES;
    }
    if (strcmp(rel_str, "extends") == 0) {
        return MEMVID_RELATION_EXTENDS;
    }
    if (strcmp(rel_str, "retracts") == 0) {
        return MEMVID_RELATION_RETRACTS;
    }

    return MEMVID_RELATION_SETS;  /* Default */
}

/*
 * Convert kind enum to string
 */
static const char *kind_to_str(int kind) {
    switch (kind) {
        case MEMVID_KIND_FACT:         return "fact";
        case MEMVID_KIND_PREFERENCE:   return "preference";
        case MEMVID_KIND_EVENT:        return "event";
        case MEMVID_KIND_PROFILE:      return "profile";
        case MEMVID_KIND_RELATIONSHIP: return "relationship";
        case MEMVID_KIND_GOAL:         return "goal";
        default:                       return "unknown";
    }
}

/*
 * Convert relation enum to string
 */
static const char *relation_to_str(int rel) {
    switch (rel) {
        case MEMVID_RELATION_SETS:     return "sets";
        case MEMVID_RELATION_UPDATES:  return "updates";
        case MEMVID_RELATION_EXTENDS:  return "extends";
        case MEMVID_RELATION_RETRACTS: return "retracts";
        default:                       return "unknown";
    }
}

/*
 * Print JSON with basic pretty-printing and colors
 */
static void print_json_pretty(const char *json) {
    int indent = 0;
    int in_string = 0;
    int escape = 0;

    for (const char *p = json; *p != '\0'; p++) {
        if (escape) {
            putchar(*p);
            escape = 0;
            continue;
        }

        if (*p == '\\') {
            putchar(*p);
            escape = 1;
            continue;
        }

        if (*p == '"') {
            in_string = !in_string;
            printf("%s\"", in_string ? COLOR_GREEN : "");
            if (!in_string) {
                printf("%s", COLOR_RESET);
            }
            continue;
        }

        if (in_string) {
            putchar(*p);
            continue;
        }

        switch (*p) {
            case '{':
            case '[':
                printf("%s%c%s\n", COLOR_YELLOW, *p, COLOR_RESET);
                indent += 2;
                for (int i = 0; i < indent; i++) putchar(' ');
                break;

            case '}':
            case ']':
                putchar('\n');
                indent -= 2;
                for (int i = 0; i < indent; i++) putchar(' ');
                printf("%s%c%s", COLOR_YELLOW, *p, COLOR_RESET);
                break;

            case ',':
                printf(",\n");
                for (int i = 0; i < indent; i++) putchar(' ');
                break;

            case ':':
                printf("%s:%s ", COLOR_DIM, COLOR_RESET);
                break;

            default:
                if (!isspace((unsigned char)*p) || (*p == ' ' && *(p-1) == ':')) {
                    putchar(*p);
                }
                break;
        }
    }
    putchar('\n');
}

/*
 * Command: put - Store a memory
 */
static void cmd_put(MemvidHandle *handle, int argc, char **argv) {
    if (argc < 4) {
        printf("%sUsage: put <entity> <slot> <value> [kind] [relation]%s\n",
               COLOR_RED, COLOR_RESET);
        return;
    }

    const char *entity = argv[1];
    const char *slot = argv[2];
    const char *value = argv[3];
    int kind = (argc > 4) ? parse_kind(argv[4]) : MEMVID_KIND_FACT;
    int relation = (argc > 5) ? parse_relation(argv[5]) : MEMVID_RELATION_SETS;

    int64_t id = memvid_put_memory(handle, entity, slot, value,
                                   (uint8_t)kind, (uint8_t)relation);

    if (id >= 0) {
        printf("%s✓ Stored memory (id=%lld)%s\n", COLOR_GREEN, (long long)id, COLOR_RESET);
        printf("  entity: %s%s%s\n", COLOR_CYAN, entity, COLOR_RESET);
        printf("  slot: %s%s%s\n", COLOR_CYAN, slot, COLOR_RESET);
        printf("  value: %s%s%s\n", COLOR_GREEN, value, COLOR_RESET);
        printf("  kind: %s\n", kind_to_str(kind));
        printf("  relation: %s\n", relation_to_str(relation));
    } else {
        const char *err = memvid_last_error();
        printf("%s✗ Failed to store memory: %s%s\n",
               COLOR_RED, err ? err : "unknown error", COLOR_RESET);
    }
}

/*
 * Command: get - Get current value for entity:slot
 */
static void cmd_get(MemvidHandle *handle, int argc, char **argv) {
    if (argc < 3) {
        printf("%sUsage: get <entity> <slot>%s\n", COLOR_RED, COLOR_RESET);
        return;
    }

    const char *entity = argv[1];
    const char *slot = argv[2];

    char *result = memvid_get_current(handle, entity, slot);

    if (result != NULL) {
        printf("%s%s:%s → ", COLOR_CYAN, entity, COLOR_RESET);
        printf("%s%s%s\n", COLOR_MAGENTA, slot, COLOR_RESET);
        print_json_pretty(result);
        memvid_free_string(result);
    } else {
        printf("%sNo value found for %s:%s%s\n",
               COLOR_YELLOW, entity, slot, COLOR_RESET);
    }
}

/*
 * Command: search - Search memories
 */
static void cmd_search(MemvidHandle *handle, int argc, char **argv) {
    if (argc < 2) {
        printf("%sUsage: search <query> [top_k]%s\n", COLOR_RED, COLOR_RESET);
        return;
    }

    const char *query = argv[1];
    uint32_t top_k = (argc > 2) ? (uint32_t)atoi(argv[2]) : 10;

    printf("%sSearching for: \"%s\" (top %u)%s\n\n",
           COLOR_DIM, query, top_k, COLOR_RESET);

    char *result = memvid_search(handle, query, top_k);

    if (result != NULL) {
        if (strcmp(result, "[]") == 0) {
            printf("%sNo results found%s\n", COLOR_YELLOW, COLOR_RESET);
        } else {
            print_json_pretty(result);
        }
        memvid_free_string(result);
    } else {
        const char *err = memvid_last_error();
        printf("%s✗ Search failed: %s%s\n",
               COLOR_RED, err ? err : "unknown error", COLOR_RESET);
    }
}

/*
 * Command: entity - Get all memories for entity
 */
static void cmd_entity(MemvidHandle *handle, int argc, char **argv) {
    if (argc < 2) {
        printf("%sUsage: entity <name>%s\n", COLOR_RED, COLOR_RESET);
        return;
    }

    const char *entity = argv[1];

    printf("%sMemories for entity: %s%s\n\n", COLOR_DIM, entity, COLOR_RESET);

    char *result = memvid_get_entity_memories(handle, entity);

    if (result != NULL) {
        if (strcmp(result, "[]") == 0) {
            printf("%sNo memories found for entity '%s'%s\n",
                   COLOR_YELLOW, entity, COLOR_RESET);
        } else {
            print_json_pretty(result);
        }
        memvid_free_string(result);
    } else {
        const char *err = memvid_last_error();
        printf("%s✗ Query failed: %s%s\n",
               COLOR_RED, err ? err : "unknown error", COLOR_RESET);
    }
}

/*
 * Command: list - List recent memories
 */
static void cmd_list(MemvidHandle *handle, int argc, char **argv) {
    uint32_t top_k = (argc > 1) ? (uint32_t)atoi(argv[1]) : 20;

    printf("%sListing recent memories (top %u)%s\n\n", COLOR_DIM, top_k, COLOR_RESET);

    /* Use empty search to get all */
    char *result = memvid_search(handle, "", top_k);

    if (result != NULL) {
        if (strcmp(result, "[]") == 0) {
            printf("%sNo memories found%s\n", COLOR_YELLOW, COLOR_RESET);
        } else {
            print_json_pretty(result);
        }
        memvid_free_string(result);
    } else {
        const char *err = memvid_last_error();
        printf("%s✗ List failed: %s%s\n",
               COLOR_RED, err ? err : "unknown error", COLOR_RESET);
    }
}

/*
 * Command: commit - Commit changes to disk
 */
static void cmd_commit(MemvidHandle *handle) {
    int ret = memvid_commit(handle);

    if (ret == 0) {
        printf("%s✓ Changes committed to disk%s\n", COLOR_GREEN, COLOR_RESET);
    } else {
        const char *err = memvid_last_error();
        printf("%s✗ Commit failed: %s%s\n",
               COLOR_RED, err ? err : "unknown error", COLOR_RESET);
    }
}

/*
 * Main REPL loop
 */
int main(int argc, char **argv) {
    const char *path = ".klawed/memory.mv2";
    char line[MAX_LINE];
    char *cmd_argv[MAX_ARGS];
    int cmd_argc = 0;

    /* Parse command line arguments */
    if (argc > 1) {
        if (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
            printf("Usage: %s [path/to/memory.mv2]\n", argv[0]);
            printf("\nInteractive REPL for testing memvid memory files.\n");
            printf("Default path: .klawed/memory.mv2\n");
            return 0;
        }
        path = argv[1];
    }

    /* Check if memvid is available */
    if (!memvid_is_available()) {
        fprintf(stderr, "%sError: memvid support not available%s\n",
                COLOR_RED, COLOR_RESET);
        fprintf(stderr, "Build with: make MEMVID=1\n");
        return 1;
    }

    /* Open memvid database */
    printf("%sOpening memvid database: %s%s\n", COLOR_DIM, path, COLOR_RESET);

    MemvidHandle *handle = memvid_open(path);
    if (handle == NULL) {
        const char *err = memvid_last_error();
        fprintf(stderr, "%sError opening memvid: %s%s\n",
                COLOR_RED, err ? err : "unknown error", COLOR_RESET);
        return 1;
    }

    printf("%s✓ Database opened successfully%s\n\n", COLOR_GREEN, COLOR_RESET);
    printf("Type %shelp%s for available commands, %squit%s to exit.\n\n",
           COLOR_CYAN, COLOR_RESET, COLOR_CYAN, COLOR_RESET);

    /* Main REPL loop */
    while (1) {
        printf("%smemvid>%s ", COLOR_BOLD, COLOR_RESET);
        fflush(stdout);

        if (fgets(line, sizeof(line), stdin) == NULL) {
            printf("\n");
            break;
        }

        /* Trim and parse */
        char *trimmed = trim(line);
        if (*trimmed == '\0') {
            continue;
        }

        /* Parse arguments */
        cmd_argc = parse_args(trimmed, cmd_argv, MAX_ARGS);
        if (cmd_argc == 0) {
            continue;
        }

        const char *cmd = cmd_argv[0];

        /* Dispatch commands */
        if (strcmp(cmd, "quit") == 0 || strcmp(cmd, "exit") == 0) {
            break;
        } else if (strcmp(cmd, "help") == 0 || strcmp(cmd, "?") == 0) {
            print_help();
        } else if (strcmp(cmd, "put") == 0) {
            cmd_put(handle, cmd_argc, cmd_argv);
        } else if (strcmp(cmd, "get") == 0) {
            cmd_get(handle, cmd_argc, cmd_argv);
        } else if (strcmp(cmd, "search") == 0) {
            cmd_search(handle, cmd_argc, cmd_argv);
        } else if (strcmp(cmd, "entity") == 0) {
            cmd_entity(handle, cmd_argc, cmd_argv);
        } else if (strcmp(cmd, "list") == 0) {
            cmd_list(handle, cmd_argc, cmd_argv);
        } else if (strcmp(cmd, "commit") == 0) {
            cmd_commit(handle);
        } else {
            printf("%sUnknown command: %s%s\n", COLOR_RED, cmd, COLOR_RESET);
            printf("Type %shelp%s for available commands.\n",
                   COLOR_CYAN, COLOR_RESET);
        }

        printf("\n");
    }

    /* Cleanup */
    printf("%sClosing database (auto-commit)...%s\n", COLOR_DIM, COLOR_RESET);
    memvid_close(handle);
    printf("%s✓ Goodbye!%s\n", COLOR_GREEN, COLOR_RESET);

    return 0;
}
