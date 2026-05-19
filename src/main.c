/**
 * @file main.c
 * @brief Entry point and main event loop for the system monitor.
 *
 * Architecture overview:
 *
 *   ┌─────────────────────┐       mutex + condvar       ┌──────────────────────┐
 *   │   sampler_fn thread │  ─────────────────────────► │   UI / main thread   │
 *   │  (background I/O)   │        SharedData g          │  (input + rendering) │
 *   └─────────────────────┘                             └──────────────────────┘
 *
 * The background sampler thread reads /proc every second and publishes a
 * fresh (CpuInfo, MemInfo, ProcList) triple under g.lock.  The UI thread
 * blocks on getch() with a 200 ms timeout, consumes any pending data, and
 * redraws the screen.  The separation ensures I/O latency never causes the
 * UI to feel sluggish.
 */

#include "cpu.h"
#include "memory.h"
#include "process.h"
#include "display.h"
#include <ncurses.h>
#include <pthread.h>
#include <signal.h>
#include <sys/resource.h>
#include <unistd.h>
#include <string.h>

/**
 * @brief Data shared between the sampler thread and the UI thread.
 *
 * Access to all members (except the synchronisation primitives themselves)
 * must be guarded by @c lock.
 *
 * @var SharedData::cpu    Latest CPU snapshot published by the sampler.
 * @var SharedData::mem    Latest memory snapshot published by the sampler.
 * @var SharedData::procs  Latest process list published by the sampler.
 * @var SharedData::ready  Set to 1 by the sampler when new data is available;
 *                         cleared to 0 by the UI thread after consumption.
 * @var SharedData::quit   Set to 1 by the UI thread to signal sampler exit.
 * @var SharedData::lock   Mutex protecting all fields above.
 * @var SharedData::cond   Condition variable used to wake the UI thread (unused
 *                         in the current getch-polling design; reserved for future use).
 */
typedef struct {
    CpuInfo  cpu;
    MemInfo  mem;
    ProcList procs;
    int      ready;
    int      quit;
    pthread_mutex_t lock;
    pthread_cond_t  cond;
} SharedData;

static SharedData g;

/**
 * @brief Background sampler thread function.
 *
 * Establishes an initial baseline outside the lock (before the UI is up),
 * then loops: sleeps 1 second, reads fresh CPU/memory/process data,
 * refreshes the baseline, and publishes the results under g.lock.
 *
 * Exits cleanly when g.quit is set by the UI thread.
 *
 * @param arg  Unused; required by pthread_create signature.
 * @return     Always NULL.
 */
static void *sampler_fn(void *arg) {
    (void)arg;

    /* Initial baseline is taken before the UI starts so the first displayed
     * sample has a valid delta rather than showing 0% everywhere. */
    cpu_sample();
    proc_sample();

    while (1) {
        sleep(1);

        pthread_mutex_lock(&g.lock);
        if (g.quit) { pthread_mutex_unlock(&g.lock); break; }
        pthread_mutex_unlock(&g.lock);

        CpuInfo  cpu   = {0};
        MemInfo  mem   = {0};
        ProcList procs = {0};

        cpu_get_info(&cpu);
        mem_get_info(&mem);
        proc_get_list(&procs);

        /* Advance baselines so the next iteration computes correct deltas. */
        cpu_sample();
        proc_sample();

        /* Publish under the lock and signal the UI thread. */
        pthread_mutex_lock(&g.lock);
        g.cpu   = cpu;
        g.mem   = mem;
        g.procs = procs;
        g.ready = 1;
        pthread_cond_signal(&g.cond);
        pthread_mutex_unlock(&g.lock);
    }
    return NULL;
}

/* ── Process control actions ───────────────────────────────────────────── */

/**
 * @brief Prompt the user and send SIGKILL to @p pid on confirmation.
 *
 * @param pid  Target process ID; ignored if ≤ 0.
 */
static void action_kill(int pid) {
    if (pid <= 0) return;
    if (display_prompt_confirm("Send SIGKILL?")) {
        kill(pid, SIGKILL);
    }
}

/**
 * @brief Prompt the user for a signal number and deliver it to @p pid.
 *
 * Accepts any signal in [1, 64].  The prompt displays common values
 * (9=KILL, 15=TERM, 2=INT) as a reminder.
 *
 * @param pid  Target process ID; ignored if ≤ 0.
 */
static void action_signal(int pid) {
    if (pid <= 0) return;
    int sig = 0;
    if (display_prompt_int("Signal number (9=KILL 15=TERM 2=INT)", &sig) == 0) {
        if (sig > 0 && sig < 65)
            kill(pid, sig);
    }
}

/**
 * @brief Prompt the user for a nice value and apply it to @p pid.
 *
 * Calls setpriority(2) which requires the calling process to have
 * CAP_SYS_NICE or to own the target process when lowering the value.
 *
 * @param pid  Target process ID; ignored if ≤ 0.
 */
static void action_renice(int pid) {
    if (pid <= 0) return;
    int nice_val = 0;
    if (display_prompt_int("New nice value (-20..19)", &nice_val) == 0) {
        if (nice_val >= -20 && nice_val <= 19)
            setpriority(PRIO_PROCESS, (id_t)pid, nice_val);
    }
}

/* ── Entry point ───────────────────────────────────────────────────────── */

int main(void) {
    pthread_mutex_init(&g.lock, NULL);
    pthread_cond_init(&g.cond, NULL);

    /* Start the sampler before display_init so the first sample is ready
     * sooner and the UI doesn't display a blank frame on launch. */
    pthread_t sampler;
    pthread_create(&sampler, NULL, sampler_fn, NULL);

    display_init();

    int selected     = 0;   /* cursor row within the visible process list */
    int show_threads = 0;   /* toggle: expand per-thread rows             */
    int selected_pid = -1;  /* PID of the currently highlighted process   */

    /* Local copies of the last rendered data; updated when new data arrives. */
    CpuInfo  last_cpu   = {0};
    MemInfo  last_mem   = {0};
    ProcList last_procs = {0};

    while (1) {
        /* getch() returns ERR after 200 ms due to halfdelay(2) in display_init. */
        int ch = getch();

        /* ── Keyboard input ── */
        if (ch == 'q' || ch == 'Q') break;

        if (ch == KEY_UP)
            selected = (selected > 0) ? selected - 1 : 0;
        else if (ch == KEY_DOWN)
            selected++;
        else if (ch == '\n' || ch == 't' || ch == 'T')
            show_threads ^= 1;   /* toggle thread expansion */
        else if (ch == 'k' || ch == 'K')
            action_kill(selected_pid);
        else if (ch == 's' || ch == 'S')
            action_signal(selected_pid);
        else if (ch == 'r' || ch == 'R')
            action_renice(selected_pid);

        /* ── Consume any fresh sample from the background thread ── */
        int new_data = 0;
        pthread_mutex_lock(&g.lock);
        if (g.ready) {
            last_cpu   = g.cpu;
            last_mem   = g.mem;
            last_procs = g.procs;
            g.ready    = 0;
            new_data   = 1;
        }
        pthread_mutex_unlock(&g.lock);

        /* Redraw only when something changed to avoid unnecessary flicker. */
        if (new_data || (ch != ERR)) {
            display_update(&last_cpu, &last_mem, &last_procs,
                           selected, show_threads, &selected_pid);
        }
    }

    /* Signal the sampler to exit, cancel it in case it is sleeping, then join. */
    pthread_mutex_lock(&g.lock);
    g.quit = 1;
    pthread_cond_signal(&g.cond);
    pthread_mutex_unlock(&g.lock);
    pthread_cancel(sampler);
    pthread_join(sampler, NULL);

    display_cleanup();
    pthread_mutex_destroy(&g.lock);
    pthread_cond_destroy(&g.cond);
    return 0;
}
