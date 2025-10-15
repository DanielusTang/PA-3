#include <threading.h>

/**
 * This vector holds all the stored contexts
 */
struct worker_context contexts[NUM_CTX];

/**
 * The index to the current context
 */
uint8_t current_context_idx = NUM_CTX; // Initialize to garbage

/* Index of the reaper context (we reserve the last slot for the reaper)
 * main_or_reaper_idx is provided because user code referenced it; point it
 * at the reaper index so existing code can continue to use that name.
 */
uint8_t reaper_idx = NUM_CTX - 1;
uint8_t main_or_reaper_idx = NUM_CTX - 1;

/* Per-context stack pointers. We cannot change the worker_context struct in
 * the header, so keep the allocated stack pointers here for freeing later.
 */
void *stacks[NUM_CTX] = { NULL };
