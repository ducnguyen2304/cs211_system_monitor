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

/* ── Shared state between sampler thread and UI thread ── */
typedef struct {
    CpuInfo  cpu;
    MemInfo  mem;
    ProcList procs;
    int      ready;     /* 1 when new data is waiting */
    int      quit;      /* signal thread to exit */
    pthread_mutex_t lock;
    pthread_cond_t  cond;
} SharedData;

static SharedData g;

/*
 * Background sampler thread.
 * Runs cpu/proc sampling in a 1-second loop so the UI thread never blocks on I/O.
 * Demonstrates: threads, synchronization (mutex + condvar), concurrent data access.
 */
static void *sampler_fn(void *arg) {
    (void)arg;

    /* Take the initial baseline outside the lock — no UI is up yet */
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

        /* Refresh baseline for the next interval */
        cpu_sample();
        proc_sample();

        /* Publish new data under the lock and wake the UI */
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

/* ── Process control actions (feature 4) ── */

static void action_kill(int pid) {
    if (pid <= 0) return;
    if (display_prompt_confirm("Send SIGKILL?")) {
        kill(pid, SIGKILL);
    }
}

static void action_signal(int pid) {
    if (pid <= 0) return;
    int sig = 0;
    if (display_prompt_int("Signal number (9=KILL 15=TERM 2=INT)", &sig) == 0) {
        if (sig > 0 && sig < 65)
            kill(pid, sig);
    }
}

static void action_renice(int pid) {
    if (pid <= 0) return;
    int nice_val = 0;
    if (display_prompt_int("New nice value (-20..19)", &nice_val) == 0) {
        if (nice_val >= -20 && nice_val <= 19)
            setpriority(PRIO_PROCESS, (id_t)pid, nice_val);
    }
}

/* ── Main ── */

int main(void) {
    pthread_mutex_init(&g.lock, NULL);
    pthread_cond_init(&g.cond, NULL);

    /* Start background sampler before display_init so first sample is ready sooner */
    pthread_t sampler;
    pthread_create(&sampler, NULL, sampler_fn, NULL);

    display_init();   /* sets up ncurses + halfdelay(2) */

    int selected     = 0;
    int show_threads = 0;
    int selected_pid = -1;

    /* Snapshot of last-rendered data (protected by lock when copying) */
    CpuInfo  last_cpu   = {0};
    MemInfo  last_mem   = {0};
    ProcList last_procs = {0};

    while (1) {
        int ch = getch();   /* returns ERR after 200 ms (halfdelay) */

        /* ── Input handling ── */
        if (ch == 'q' || ch == 'Q') break;

        if (ch == KEY_UP)
            selected = (selected > 0) ? selected - 1 : 0;
        else if (ch == KEY_DOWN)
            selected++;
        else if (ch == '\n' || ch == 't' || ch == 'T')
            show_threads ^= 1;
        else if (ch == 'k' || ch == 'K')
            action_kill(selected_pid);
        else if (ch == 's' || ch == 'S')
            action_signal(selected_pid);
        else if (ch == 'r' || ch == 'R')
            action_renice(selected_pid);

        /* ── Pick up new data from sampler if available ── */
        pthread_mutex_lock(&g.lock);
        if (g.ready) {
            last_cpu   = g.cpu;
            last_mem   = g.mem;
            last_procs = g.procs;
            g.ready    = 0;
        }
        pthread_mutex_unlock(&g.lock);

        /* Redraw on every tick (input event or data update) */
        display_update(&last_cpu, &last_mem, &last_procs,
                       selected, show_threads, &selected_pid);
    }

    /* Shut down sampler thread */
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
