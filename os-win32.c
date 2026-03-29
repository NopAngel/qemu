/*
 * OS specific functions for Windows platforms
 *
 * Optimized for high-resolution timing and signal handling.
 * Copyright (c) 2003-2026 Fabrice Bellard and Contributors.
 */

#include "qemu/osdep.h"
#include <windows.h>
#include <mmsystem.h>
#include "system/runstate.h"

/**
 * qemu_ctrl_handler - Windows Console Control Handler
 * @type: Type of control signal received (e.g., CTRL_C_EVENT)
 * * Handles host signals to ensure QEMU attempts a graceful shutdown.
 */
static BOOL WINAPI qemu_ctrl_handler(DWORD type)
{
    /* Request a system shutdown with host signal cause */
    qemu_system_shutdown_request(SHUTDOWN_CAUSE_HOST_SIGNAL);

    /* * Windows 7+ terminates the application once this handler returns.
     * We sleep for 10 seconds to grant QEMU's cleanup threads time 
     * to flush disk buffers and close resources. 
     */
    Sleep(10000);

    return TRUE;
}

/* Structure to store multimedia timer capabilities */
static TIMECAPS mm_tc;

/**
 * os_undo_timer_resolution - Restore default system timer period
 * Called automatically at exit to prevent system-wide power drain.
 */
static void os_undo_timer_resolution(void)
{
    timeEndPeriod(mm_tc.wPeriodMin);
}

/**
 * os_setup_early_signal_handling - Initial OS-specific setup
 * Configures signal traps and boosts timer precision for emulation.
 */
void os_setup_early_signal_handling(void)
{
    /* Register the console control handler for shutdown signals */
    SetConsoleCtrlHandler(qemu_ctrl_handler, TRUE);

    /* * Increase timer resolution to the minimum supported period.
     * This is critical for guest OS clock accuracy and TCG performance.
     */
    if (timeGetDevCaps(&mm_tc, sizeof(mm_tc)) == MMSYSERR_NOERROR) {
        timeBeginPeriod(mm_tc.wPeriodMin);
        atexit(os_undo_timer_resolution);
    }
}

/**
 * os_set_line_buffering - Disable I/O buffering for standard streams
 * Ensures logs and errors are visible immediately in the Windows console.
 */
void os_set_line_buffering(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);
}