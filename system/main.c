/*
 * QEMU System Emulator - Main Entry Point
 *
 * Optimized for multi-threading and BQL management.
 * Copyright (c) 2003-2026 Fabrice Bellard and Contributors.
 */

#include "qemu/osdep.h"
#include "qemu-main.h"
#include "qemu/main-loop.h"
#include "system/replay.h"
#include "system/system.h"

#ifdef CONFIG_SDL
/*
 * SDL platforms may wrap main() via macros. 
 * Include required to ensure proper symbol redirection.
 */
#include <SDL.h>
#endif

#ifdef CONFIG_DARWIN
#include <CoreFoundation/CoreFoundation.h>
#endif

/**
 * qemu_default_main - Standard execution path for the main loop
 * @opaque: Unused parameter for thread compatibility
 */
static void *qemu_default_main(void *opaque)
{
    int status;

    /* * Re-acquire locks released by the initial setup.
     * BQL (Big QEMU Lock) is mandatory for core emulator state.
     */
    replay_mutex_lock();
    bql_lock();

    status = qemu_main_loop();
    
    /* Cleanup resources before thread termination */
    qemu_cleanup(status);

    bql_unlock();
    replay_mutex_unlock();

    exit(status);
}

/* Function pointer to override the main execution logic if needed */
int (*qemu_main)(void);

#ifdef CONFIG_DARWIN
/**
 * os_darwin_cfrunloop_main - MacOS specific UI event loop
 * Keeps the main thread responsive for the Cocoa UI.
 */
static int os_darwin_cfrunloop_main(void)
{
    CFRunLoopRun();
    g_assert_not_reached();
}

/* On Darwin, we default to the CoreFoundation runloop */
int (*qemu_main)(void) = os_darwin_cfrunloop_main;
#endif

int main(int argc, char **argv)
{
    /* Initialize QEMU internal state, CPUs, and accelerators */
    qemu_init(argc, argv);

    /*
     * qemu_init() acquires the BQL/Replay locks. We release them here
     * to allow the main_loop_thread (or the subsequent call) to
     * take ownership and avoid deadlocks during the transition.
     */
    bql_unlock();
    replay_mutex_unlock();

    if (qemu_main) {
        QemuThread main_loop_thread;

        /* * Spawn the emulator loop in a background thread.
         * Detached mode is used as the thread manages its own exit.
         */
        qemu_thread_create(&main_loop_thread, "qemu_main_loop",
                           qemu_default_main, NULL, QEMU_THREAD_DETACHED);
        
        /* Run the platform-specific main (e.g., UI event loop) */
        return qemu_main();
    } 
    
    /* Fallback to default execution if no override is set */
    qemu_default_main(NULL);
    
    /* Safety check: qemu_default_main should call exit() */
    g_assert_not_reached();
}
