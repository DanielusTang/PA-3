// Small cooperative threading runtime using ucontext.
#include <threading.h>

// Globals from threading_data.c
extern uint8_t reaper_idx;
extern uint8_t main_or_reaper_idx;
extern void *stacks[];

// Helpers
int find_slot_with_state(enum context_state s);
int find_next_valid_after(int cur);
void thread_start_wrapper(uintptr_t fn_ptr, uintptr_t a1, uintptr_t a2);
void reaper_loop(void);

// Free any allocated per-context stacks at exit.
static void cleanup_stacks(void) {
    for (int i = 0; i < NUM_CTX; ++i) {
        if (stacks[i]) {
            free(stacks[i]);
            stacks[i] = NULL;
        }
    }
}

// Initialize runtime: main context + reaper (reaper marked INVALID)
void t_init()
{
    for (int i = 0; i < NUM_CTX; i++) {
        contexts[i].state = INVALID;
        memset(&contexts[i].context, 0, sizeof(ucontext_t));
    }

    getcontext(&contexts[0].context);
    contexts[0].state = VALID;
    current_context_idx = 0;

    int r = reaper_idx;
    getcontext(&contexts[r].context);
    void *rstack = malloc(STK_SZ);
    if (rstack) {
        contexts[r].context.uc_stack.ss_sp = rstack;
        contexts[r].context.uc_stack.ss_size = STK_SZ;
        stacks[r] = rstack;
        contexts[r].context.uc_link = &contexts[0].context;
        makecontext(&contexts[r].context, (void(*)()) reaper_loop, 0);
        contexts[r].state = INVALID;
        (void) atexit(cleanup_stacks);
    } else {
        contexts[r].state = INVALID;
    }
}

// Create a worker context that runs 'foo(arg1,arg2)'. Returns 0 on success.
int32_t t_create(fptr foo, int32_t arg1, int32_t arg2) {
    int i = find_slot_with_state(INVALID);
    if (i < 0) return 1;

    getcontext(&contexts[i].context);
    void *stack = malloc(STK_SZ);
    if (!stack) return 1;

    contexts[i].context.uc_stack.ss_sp = stack;
    contexts[i].context.uc_stack.ss_size = STK_SZ;
    stacks[i] = stack;

    contexts[i].context.uc_link = &contexts[main_or_reaper_idx].context;

    makecontext(&contexts[i].context, (void(*)()) thread_start_wrapper, 3,
                (uintptr_t) foo, (uintptr_t) arg1, (uintptr_t) arg2);

    contexts[i].state = VALID;
    return 0;
}

// Cooperative yield: switch to next VALID context.
// Returns remaining VALID contexts (excluding caller), 0 if none, or -1 on error.
int32_t t_yield() {
    int cur = current_context_idx;

    int count = 0;
    for (int i = 0; i < NUM_CTX; ++i) {
        if (i != cur && contexts[i].state == VALID) count++;
    }

    if (count == 0) return 0;

    int next = find_next_valid_after(cur);
    if (next == -1) return -1;

    current_context_idx = (uint8_t) next;
    if (swapcontext(&contexts[cur].context, &contexts[next].context) == -1) {
        return -1;
    }

    int remaining = 0;
    for (int i = 0; i < NUM_CTX; ++i) {
        if (i != current_context_idx && contexts[i].state == VALID) remaining++;
    }
    return remaining;
}

// Mark current context DONE and transfer control to reaper to free its stack.
void t_finish() {
    int cur = current_context_idx;
    contexts[cur].state = DONE;
    swapcontext(&contexts[cur].context, &contexts[reaper_idx].context);
}

// Return first index with matching state or -1.
int find_slot_with_state(enum context_state s) {
    for (int i = 0; i < NUM_CTX; ++i) {
        if (contexts[i].state == s) return i;
    }
    return -1;
}

// Find next VALID context after `cur` (round-robin) or -1 if none.
int find_next_valid_after(int cur) {
    for (int i = 1; i < NUM_CTX; ++i) {
        int idx = (cur + i) % NUM_CTX;
        if (contexts[idx].state == VALID) return idx;
    }
    return -1;
}

// Unpack makecontext args, call user's function, then finish.
void thread_start_wrapper(uintptr_t fn_ptr, uintptr_t a1, uintptr_t a2) {
    fptr fn = (fptr) fn_ptr;
    int32_t arg1 = (int32_t) a1;
    int32_t arg2 = (int32_t) a2;
    fn(arg1, arg2);
    t_finish();
}

// Reaper: frees DONE contexts' stacks and returns control to main.
void reaper_loop(void) {
    while (1) {
        int freed_any = 0;
        for (int i = 0; i < NUM_CTX; ++i) {
            if (contexts[i].state == DONE) {
                if (stacks[i]) {
                    free(stacks[i]);
                    stacks[i] = NULL;
                }
                contexts[i].state = INVALID;
                freed_any = 1;
            }
        }
        if (freed_any) {
            swapcontext(&contexts[reaper_idx].context, &contexts[0].context);
        }
        swapcontext(&contexts[reaper_idx].context, &contexts[0].context);
    }
}
