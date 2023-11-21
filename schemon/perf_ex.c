#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <asm/unistd.h>
#include <linux/perf_event.h>

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

static long perf_event_open(struct perf_event_attr *hw_event, pid_t pid,
                            int cpu, int group_fd, unsigned long flags) {
    return syscall(__NR_perf_event_open, hw_event, pid, cpu, group_fd, flags);
}

int main() {
    struct perf_event_attr pe;
    int fd[8];

    memset(&pe, 0, sizeof(struct perf_event_attr));
    pe.type = PERF_TYPE_RAW;
    pe.config = 0x17; // Count CPU cycles
    pe.size = sizeof(struct perf_event_attr);
    pe.disabled = 1; // start in disabled state
    pe.exclude_kernel = 0; // exclude kernel space
    pe.exclude_hv = 1; // exclude hypervisor space

    for(auto i = 0; i <= 7; i++) {
    fd[i] = perf_event_open(&pe, -1, 0, -1, 0);
    if (fd[i] == -1) {
        perror("Error opening perf event");
        exit(EXIT_FAILURE);
    }
    

    // Enable the counter
    ioctl(fd[i], PERF_EVENT_IOC_RESET, 0);
    ioctl(fd[i], PERF_EVENT_IOC_ENABLE, 0);
    }
    // Perform some workload here
    sleep(5);
    // Stop counting
    long long count;
    for(auto i = 0; i <= 7; i++) {
    ioctl(fd[i], PERF_EVENT_IOC_DISABLE, 0);

    // Read the counter value

    read(fd[i], &count, sizeof(long long));

    printf("L2 Cache Misses on CPU %d: %lld \n", i, count);
    close(fd[i]);
    }

    return 0;
}