/*
 * RTC configuration and clock read
 *
 * Optimized for precise time synchronization in virtual environments.
 * Copyright (c) 2003-2026 QEMU contributors
 */

#include "qemu/osdep.h"
#include "qemu/cutils.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "qemu/option.h"
#include "qemu/timer.h"
#include "qom/object.h"
#include "system/replay.h"
#include "system/system.h"
#include "system/rtc.h"
#include "hw/rtc/mc146818rtc.h"

/* Internal RTC base types for synchronization */
static enum {
    RTC_BASE_UTC,
    RTC_BASE_LOCALTIME,
    RTC_BASE_DATETIME,
} rtc_base_type = RTC_BASE_UTC;

static time_t rtc_ref_start_datetime;
static int rtc_realtime_clock_offset; /* Offset for QEMU_CLOCK_REALTIME */
static int rtc_host_datetime_offset = -1; /* Offset for RTC_BASE_DATETIME mode */

QEMUClockType rtc_clock;

/**
 * qemu_ref_timedate - Calculate reference time based on clock type
 * @clock: The QEMU clock type (Realtime, Virtual, or Host)
 * Returns: time_t representing the reference seconds.
 */
static time_t qemu_ref_timedate(QEMUClockType clock)
{
    time_t value = qemu_clock_get_ms(clock) / 1000;

    switch (clock) {
    case QEMU_CLOCK_REALTIME:
        value -= rtc_realtime_clock_offset;
        /* fall through to add start datetime */
    case QEMU_CLOCK_VIRTUAL:
        value += rtc_ref_start_datetime;
        break;
    case QEMU_CLOCK_HOST:
        if (rtc_base_type == RTC_BASE_DATETIME) {
            value -= rtc_host_datetime_offset;
        }
        break;
    default:
        g_assert_not_reached();
    }
    return value;
}

/**
 * qemu_get_timedate - Fill tm structure with the current RTC time
 * @tm: Destination tm structure
 * @offset: Seconds to add to the current time
 */
void qemu_get_timedate(struct tm *tm, time_t offset)
{
    time_t ti = qemu_ref_timedate(rtc_clock) + offset;

    /* Select timezone conversion based on RTC base configuration */
    if (rtc_base_type == RTC_BASE_LOCALTIME) {
        localtime_r(&ti, tm);
    } else {
        gmtime_r(&ti, tm);
    }
}

/**
 * qemu_timedate_diff - Calculate difference between a tm and host clock
 * Useful for adjusting time drift in guest machines.
 */
time_t qemu_timedate_diff(struct tm *tm)
{
    time_t seconds;

    if (rtc_base_type == RTC_BASE_LOCALTIME) {
        struct tm tmp = *tm;
        tmp.tm_isdst = -1; /* Auto-detect Daylight Saving Time */
        seconds = mktime(&tmp);
    } else {
        seconds = mktimegm(tm);
    }

    return seconds - qemu_ref_timedate(QEMU_CLOCK_HOST);
}

/**
 * configure_rtc_base_datetime - Parse custom start date for RTC
 * Supports ISO-like formats: YYYY-MM-DDThh:mm:ss or YYYY-MM-DD.
 */
static void configure_rtc_base_datetime(const char *startdate)
{
    time_t rtc_start_datetime;
    struct tm tm = {0};

    if (sscanf(startdate, "%d-%d-%dT%d:%d:%d", &tm.tm_year, &tm.tm_mon,
               &tm.tm_mday, &tm.tm_hour, &tm.tm_min, &tm.tm_sec) == 6) {
        /* Full date and time parsed */
    } else if (sscanf(startdate, "%d-%d-%d",
                      &tm.tm_year, &tm.tm_mon, &tm.tm_mday) == 3) {
        /* Date only, time defaults to 00:00:00 */
    } else {
        goto date_fail;
    }

    tm.tm_year -= 1900; /* tm_year is years since 1900 */
    tm.tm_mon--;       /* tm_mon is 0-11 */
    
    rtc_start_datetime = mktimegm(&tm);
    if (rtc_start_datetime == -1) {
    date_fail:
        error_report("rtc: invalid datetime format '%s'", startdate);
        error_printf("Valid formats: 'YYYY-MM-DDThh:mm:ss' or 'YYYY-MM-DD'\n");
        exit(1);
    }

    rtc_host_datetime_offset = rtc_ref_start_datetime - rtc_start_datetime;
    rtc_ref_start_datetime = rtc_start_datetime;
}

/**
 * configure_rtc - Main entry point for RTC command line options
 */
void configure_rtc(QemuOpts *opts)
{
    const char *value;

    /* Default initialization based on host clock */
    rtc_clock = QEMU_CLOCK_HOST;
    rtc_ref_start_datetime = qemu_clock_get_ms(QEMU_CLOCK_HOST) / 1000;
    rtc_realtime_clock_offset = qemu_clock_get_ms(QEMU_CLOCK_REALTIME) / 1000;

    /* Handle 'base' option (utc|localtime|datetime) */
    value = qemu_opt_get(opts, "base");
    if (value) {
        if (!strcmp(value, "utc")) {
            rtc_base_type = RTC_BASE_UTC;
        } else if (!strcmp(value, "localtime")) {
            rtc_base_type = RTC_BASE_LOCALTIME;
            replay_add_blocker("-rtc base=localtime");
        } else {
            rtc_base_type = RTC_BASE_DATETIME;
            configure_rtc_base_datetime(value);
        }
    }

    /* Handle 'clock' option (host|rt|vm) */
    value = qemu_opt_get(opts, "clock");
    if (value) {
        if (!strcmp(value, "host")) {
            rtc_clock = QEMU_CLOCK_HOST;
        } else if (!strcmp(value, "rt")) {
            rtc_clock = QEMU_CLOCK_REALTIME;
        } else if (!strcmp(value, "vm")) {
            rtc_clock = QEMU_CLOCK_VIRTUAL;
        } else {
            error_report("rtc: invalid clock value '%s'", value);
            exit(1);
        }
    }

    /* Handle 'driftfix' option for tick lost policies */
    value = qemu_opt_get(opts, "driftfix");
    if (value && !strcmp(value, "slew")) {
        object_register_sugar_prop(TYPE_MC146818_RTC, "lost_tick_policy", "slew", false);
        if (!object_class_by_name(TYPE_MC146818_RTC)) {
            warn_report("rtc: driftfix 'slew' not supported by this machine");
        }
    }
}
