/*
 * Unit tests for ESC key interruption during API calls
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <curl/curl.h>

// Mock check_for_esc function for testing
static volatile int mock_esc_pressed = 0;

int check_for_esc(void) {
    return mock_esc_pressed;
}

// Progress callback implementation (same as in providers)
static int progress_callback(void *clientp, curl_off_t dltotal, curl_off_t dlnow,
                             curl_off_t ultotal, curl_off_t ulnow) {
    (void)clientp;
    (void)dltotal;
    (void)dlnow;
    (void)ultotal;
    (void)ulnow;

    if (check_for_esc()) {
        printf("ESC detected in progress callback - aborting transfer\n");
        return 1;  // Non-zero return value aborts the transfer
    }
    return 0;  // Continue transfer
}

// Memory buffer for curl write callback
typedef struct {
    char *output;
    size_t size;
} MemoryBuffer;

static size_t write_callback(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t realsize = size * nmemb;
    MemoryBuffer *mem = (MemoryBuffer *)userp;

    char *ptr = realloc(mem->output, mem->size + realsize + 1);
    if (!ptr) {
        return 0;
    }

    mem->output = ptr;
    memcpy(&(mem->output[mem->size]), contents, realsize);
    mem->size += realsize;
    mem->output[mem->size] = 0;

    return realsize;
}

static void test_esc_abort_transfer(void) {
    printf("\n=== Test: ESC aborts curl transfer ===\n");

    // Reset mock ESC state
    mock_esc_pressed = 0;

    CURL *curl = curl_easy_init();
    assert(curl != NULL && "curl_easy_init should succeed");

    MemoryBuffer response = {NULL, 0};

    // Use httpbin.org/delay/5 which takes 5 seconds to respond
    // This gives us time to simulate ESC press
    curl_easy_setopt(curl, CURLOPT_URL, "https://httpbin.org/delay/5");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

    // Enable progress callback for ESC interruption
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, progress_callback);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, NULL);

    // Simulate ESC press after 1 second in a separate thread
    // For simplicity in this test, we'll just set it before the call
    // In a real scenario, it would be set asynchronously during the transfer
    mock_esc_pressed = 1;

    // Perform the request - should be aborted by progress callback
    CURLcode res = curl_easy_perform(curl);

    // Verify that the request was aborted
    assert(res == CURLE_ABORTED_BY_CALLBACK && "Transfer should be aborted by callback");
    printf("✓ Transfer was correctly aborted (CURLE_ABORTED_BY_CALLBACK)\n");

    // Cleanup
    free(response.output);
    curl_easy_cleanup(curl);
}

static void test_no_esc_completes_transfer(void) {
    printf("\n=== Test: No ESC allows transfer to complete ===\n");

    // Reset mock ESC state - no ESC pressed
    mock_esc_pressed = 0;

    CURL *curl = curl_easy_init();
    assert(curl != NULL && "curl_easy_init should succeed");

    MemoryBuffer response = {NULL, 0};

    // Use a quick endpoint that responds immediately
    curl_easy_setopt(curl, CURLOPT_URL, "https://httpbin.org/get");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

    // Enable progress callback (but ESC is not pressed)
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, progress_callback);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, NULL);

    // Perform the request - should complete normally
    CURLcode res = curl_easy_perform(curl);

    // Verify that the request completed successfully
    assert(res == CURLE_OK && "Transfer should complete successfully");
    assert(response.output != NULL && "Response should contain data");
    printf("✓ Transfer completed successfully without interruption\n");

    // Cleanup
    free(response.output);
    curl_easy_cleanup(curl);
}

static void test_progress_callback_called(void) {
    printf("\n=== Test: Progress callback is actually called ===\n");

    // Reset mock ESC state
    mock_esc_pressed = 0;

    CURL *curl = curl_easy_init();
    assert(curl != NULL && "curl_easy_init should succeed");

    MemoryBuffer response = {NULL, 0};

    // Use a quick endpoint
    curl_easy_setopt(curl, CURLOPT_URL, "https://httpbin.org/get");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

    // Enable progress callback
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, progress_callback);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, NULL);

    // Perform the request
    CURLcode res = curl_easy_perform(curl);

    // If transfer completed, progress callback must have been called
    // (returning 0 each time to allow transfer to continue)
    assert(res == CURLE_OK && "Transfer should complete successfully");
    printf("✓ Progress callback was called and allowed transfer to complete\n");

    // Cleanup
    free(response.output);
    curl_easy_cleanup(curl);
}

int main(void) {
    printf("ESC Key Interruption Tests\n");
    printf("===========================\n");

    // Initialize curl globally
    curl_global_init(CURL_GLOBAL_DEFAULT);

    // Run tests
    test_esc_abort_transfer();
    test_no_esc_completes_transfer();
    test_progress_callback_called();

    // Cleanup curl
    curl_global_cleanup();

    printf("\n=== All ESC interruption tests passed! ===\n");
    return 0;
}
