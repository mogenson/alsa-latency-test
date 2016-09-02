#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <unistd.h>

#include "ftrace.h"

/* ftrace fd variables */
static int trace_fd = -1;
static int marker_fd = -1;

void trace_print_usage(void)
{
    printf("---------------------------------------------------------------\n");
    printf("To use ftrace:\n");
    printf("\n");
    printf("sudo sh -c \"chmod 777 /sys/kernel/debug/\"\n");
    printf("sudo sh -c \"chmod 777 /sys/kernel/debug/tracing/tracing_on\"\n");
    printf("sudo sh -c \"chmod 777 /sys/kernel/debug/tracing/trace_marker\"\n");
    printf("---------------------------------------------------------------\n");
}

int trace_start(void)
{
    if (trace_fd >= 0)
        write(trace_fd, "1", 1);
    if (marker_fd >= 0)
        write(marker_fd, "START_TRACE\n", 12);

    return 0;
}

int trace_stop(void)
{
    if (marker_fd >= 0)
        write(marker_fd, "STOP_TRACE\n", 11);
    if (trace_fd >= 0)
        write(trace_fd, "0", 1);
    return 0;
}

int trace_init(void)
{
    trace_fd = open("/sys/kernel/debug/tracing/tracing_on", O_WRONLY);
    if (trace_fd < 0) {
        trace_print_usage();
        return -1;
    }

    marker_fd = open("/sys/kernel/debug/tracing/trace_marker", O_WRONLY);
    if (marker_fd < 0) {
        trace_print_usage();
        return -1;
    }

    return 0;
}
