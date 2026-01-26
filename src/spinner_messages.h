/*
 * spinner_messages.h - Creative status messages for spinner animations
 *
 * Provides 100+ varied status messages for different contexts:
 * - API calls (waiting for LLM response)
 * - Tool execution (running various tools)
 * - Processing operations
 * - General waiting states
 *
 * Messages are randomly selected to add variety and personality to the UI.
 */

#ifndef SPINNER_MESSAGES_H
#define SPINNER_MESSAGES_H

#include <stdlib.h>
#include <time.h>

// Context types for spinner messages
typedef enum {
    SPINNER_CONTEXT_API_CALL,      // Waiting for API response
    SPINNER_CONTEXT_TOOL_RUNNING,  // Tool execution in progress
    SPINNER_CONTEXT_PROCESSING,    // Processing data/results
    SPINNER_CONTEXT_INITIALIZING,  // Initialization/setup
    SPINNER_CONTEXT_WAITING,       // Generic waiting
    SPINNER_CONTEXT_COUNT
} SpinnerMessageContext;

// API call messages (waiting for LLM response)
static const char *SPINNER_MSG_API_CALL[] = {
    "Waiting for API response...",
    "Consulting the AI oracle...",
    "Channeling artificial wisdom...",
    "Brewing some intelligence...",
    "Asking the machine spirits...",
    "Summoning digital insight...",
    "Loading neural pathways...",
    "Querying the cloud mind...",
    "Awakening the algorithm...",
    "Invoking GPT magic...",
    "Streaming tokens of wisdom...",
    "Decoding AI thoughts...",
    "Fetching bits of brilliance...",
    "Mining the data mines...",
    "Consulting the silicon sage...",
    "Downloading enlightenment...",
    "Waiting for the machine to think...",
    "Processing quantum uncertainty...",
    "Calibrating language models...",
    "Pondering the prompt..."
};

// Tool execution messages (generic)
static const char *SPINNER_MSG_TOOL_RUNNING[] = {
    "Executing tool...",
    "Running command...",
    "Turning the gears...",
    "Spinning up the engines...",
    "Crunching the numbers...",
    "Working the magic...",
    "Processing your request...",
    "Executing operations...",
    "Running the machinery...",
    "Performing dark rituals...",
    "Invoking system calls...",
    "Manipulating bits and bytes...",
    "Engaging hyperdrive...",
    "Activating tool protocol...",
    "Deploying code ninjas...",
    "Launching the missiles...",
    "Executing order 66...",
    "Running hamster wheel...",
    "Summoning subprocess...",
    "Forking reality..."
};

// Specific tool execution messages
static const char *SPINNER_MSG_BASH[] = {
    "Running bash command...",
    "Executing in shell...",
    "Bash is bashing...",
    "Commanding the terminal...",
    "Invoking the shell spirits...",
    "Running wild in the shell...",
    "Executing terminal wizardry...",
    "Bash scripting in progress...",
    "Shell shocked and running...",
    "Terminal velocity achieved..."
};

static const char *SPINNER_MSG_READ[] = {
    "Reading file...",
    "Scanning document...",
    "Absorbing text...",
    "Ingesting bytes...",
    "Devouring content...",
    "Speed reading enabled...",
    "Parsing the literature...",
    "Reading between the lines...",
    "Consuming file content...",
    "Scanning the pages..."
};

static const char *SPINNER_MSG_WRITE[] = {
    "Writing file...",
    "Inscribing to disk...",
    "Committing bytes...",
    "Etching data...",
    "Burning to storage...",
    "Scribbling furiously...",
    "Carving into filesystem...",
    "Stamping the bits...",
    "Persisting changes...",
    "Flushing to disk..."
};

static const char *SPINNER_MSG_EDIT[] = {
    "Editing file...",
    "Modifying content...",
    "Applying changes...",
    "Refactoring reality...",
    "Tweaking the source...",
    "Performing surgery...",
    "Massaging the code...",
    "Editing with precision...",
    "Applying text alchemy...",
    "Transforming content..."
};

static const char *SPINNER_MSG_GREP[] = {
    "Searching files...",
    "Grepping through code...",
    "Pattern hunting...",
    "Scanning the codebase...",
    "Looking for needles...",
    "Searching high and low...",
    "Following the trail...",
    "Sifting through files...",
    "Pattern matching engaged...",
    "Hunting for matches..."
};

static const char *SPINNER_MSG_GLOB[] = {
    "Finding files...",
    "Globbing patterns...",
    "Discovering paths...",
    "Enumerating matches...",
    "Traversing directories...",
    "Exploring the filesystem...",
    "Collecting file paths...",
    "Scanning directory tree...",
    "Gathering file list...",
    "Mapping the terrain..."
};

// Processing messages
static const char *SPINNER_MSG_PROCESSING[] = {
    "Processing...",
    "Computing results...",
    "Analyzing data...",
    "Synthesizing information...",
    "Cooking up results...",
    "Grinding the data...",
    "Churning the bits...",
    "Calculating outcomes...",
    "Processing thoughts...",
    "Compiling insights...",
    "Distilling wisdom...",
    "Generating output...",
    "Building response...",
    "Assembling answer...",
    "Crafting reply..."
};

// Initialization messages
static const char *SPINNER_MSG_INITIALIZING[] = {
    "Initializing...",
    "Setting up...",
    "Warming up engines...",
    "Calibrating systems...",
    "Preparing environment...",
    "Booting subsystems...",
    "Loading modules...",
    "Priming the pumps...",
    "Spinning up services...",
    "Activating protocols..."
};

// Generic waiting messages
static const char *SPINNER_MSG_WAITING[] = {
    "Please wait...",
    "Hold tight...",
    "One moment...",
    "Just a sec...",
    "Almost there...",
    "Working on it...",
    "In progress...",
    "Hang on...",
    "Stand by...",
    "Patience, grasshopper...",
    "Loading awesomeness...",
    "Reticulating splines...",
    "Herding cats...",
    "Counting backwards from infinity...",
    "Dividing by zero..."
};

// Message array metadata
typedef struct {
    const char **messages;
    int count;
} SpinnerMessageArray;

// Get message array for a specific context
static inline SpinnerMessageArray spinner_get_messages_for_context(SpinnerMessageContext context) {
    SpinnerMessageArray result = {NULL, 0};

    switch (context) {
        case SPINNER_CONTEXT_API_CALL:
            result.messages = SPINNER_MSG_API_CALL;
            result.count = sizeof(SPINNER_MSG_API_CALL) / sizeof(SPINNER_MSG_API_CALL[0]);
            break;
        case SPINNER_CONTEXT_TOOL_RUNNING:
            result.messages = SPINNER_MSG_TOOL_RUNNING;
            result.count = sizeof(SPINNER_MSG_TOOL_RUNNING) / sizeof(SPINNER_MSG_TOOL_RUNNING[0]);
            break;
        case SPINNER_CONTEXT_PROCESSING:
            result.messages = SPINNER_MSG_PROCESSING;
            result.count = sizeof(SPINNER_MSG_PROCESSING) / sizeof(SPINNER_MSG_PROCESSING[0]);
            break;
        case SPINNER_CONTEXT_INITIALIZING:
            result.messages = SPINNER_MSG_INITIALIZING;
            result.count = sizeof(SPINNER_MSG_INITIALIZING) / sizeof(SPINNER_MSG_INITIALIZING[0]);
            break;
        case SPINNER_CONTEXT_WAITING:
            result.messages = SPINNER_MSG_WAITING;
            result.count = sizeof(SPINNER_MSG_WAITING) / sizeof(SPINNER_MSG_WAITING[0]);
            break;
        case SPINNER_CONTEXT_COUNT:
            // Sentinel value, use default
            result.messages = SPINNER_MSG_WAITING;
            result.count = sizeof(SPINNER_MSG_WAITING) / sizeof(SPINNER_MSG_WAITING[0]);
            break;
        default:
            // Unknown context, use default
            result.messages = SPINNER_MSG_WAITING;
            result.count = sizeof(SPINNER_MSG_WAITING) / sizeof(SPINNER_MSG_WAITING[0]);
            break;
    }

    return result;
}

// Get message array for a specific tool
static inline SpinnerMessageArray spinner_get_messages_for_tool(const char *tool_name) {
    SpinnerMessageArray result = {NULL, 0};

    if (!tool_name) {
        return spinner_get_messages_for_context(SPINNER_CONTEXT_TOOL_RUNNING);
    }

    // Check for specific tool names
    if (strcmp(tool_name, "Bash") == 0) {
        result.messages = SPINNER_MSG_BASH;
        result.count = sizeof(SPINNER_MSG_BASH) / sizeof(SPINNER_MSG_BASH[0]);
    } else if (strcmp(tool_name, "Read") == 0) {
        result.messages = SPINNER_MSG_READ;
        result.count = sizeof(SPINNER_MSG_READ) / sizeof(SPINNER_MSG_READ[0]);
    } else if (strcmp(tool_name, "Write") == 0) {
        result.messages = SPINNER_MSG_WRITE;
        result.count = sizeof(SPINNER_MSG_WRITE) / sizeof(SPINNER_MSG_WRITE[0]);
    } else if (strcmp(tool_name, "Edit") == 0 || strcmp(tool_name, "MultiEdit") == 0) {
        result.messages = SPINNER_MSG_EDIT;
        result.count = sizeof(SPINNER_MSG_EDIT) / sizeof(SPINNER_MSG_EDIT[0]);
    } else if (strcmp(tool_name, "Grep") == 0) {
        result.messages = SPINNER_MSG_GREP;
        result.count = sizeof(SPINNER_MSG_GREP) / sizeof(SPINNER_MSG_GREP[0]);
    } else if (strcmp(tool_name, "Glob") == 0) {
        result.messages = SPINNER_MSG_GLOB;
        result.count = sizeof(SPINNER_MSG_GLOB) / sizeof(SPINNER_MSG_GLOB[0]);
    } else {
        // Default to generic tool messages
        result.messages = SPINNER_MSG_TOOL_RUNNING;
        result.count = sizeof(SPINNER_MSG_TOOL_RUNNING) / sizeof(SPINNER_MSG_TOOL_RUNNING[0]);
    }

    return result;
}

// Get a random message from an array
static inline const char* spinner_get_random_message(const SpinnerMessageArray *array) {
    if (!array || !array->messages || array->count <= 0) {
        return "Working...";
    }

    // Initialize random seed once
    static int seeded = 0;
    if (!seeded) {
        srand((unsigned int)time(NULL));
        seeded = 1;
    }

    int index = rand() % array->count;
    return array->messages[index];
}

// Convenience function: get random message for context
static inline const char* spinner_random_msg_for_context(SpinnerMessageContext context) {
    SpinnerMessageArray array = spinner_get_messages_for_context(context);
    return spinner_get_random_message(&array);
}

// Convenience function: get random message for tool
static inline const char* spinner_random_msg_for_tool(const char *tool_name) {
    SpinnerMessageArray array = spinner_get_messages_for_tool(tool_name);
    return spinner_get_random_message(&array);
}

#endif // SPINNER_MESSAGES_H
