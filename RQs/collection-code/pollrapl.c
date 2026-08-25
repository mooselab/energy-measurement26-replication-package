// jan 21 to use this with the automation you need to give this file both cap_dac_override,cap_sys_rawio
// nov 5 added dram and psys domains. psys isnt available on coffeelake
// oct 20 premier rapl logger (time-bounded version)
#define _GNU_SOURCE
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <math.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <time.h>
#include <signal.h>
#include <sched.h>
#include "uthash.h"
#include <sys/stat.h>
#include <sys/types.h>
#include <stdio.h>
#include <time.h>
#include <errno.h>

#define MSR_RAPL_POWER_UNIT     0x606
#define MSR_PKG_ENERGY_STATUS   0x611
#define MSR_DRAM_ENERGY_STATUS  0x619
#define MSR_PLATFORM_ENERGY_COUNTER 0x64D

// ====== USER SETTINGS ======
#define CPU_INDEX   0                // CPU core to read from
#define SAMP_FREQ   1000             // Hz (samples per second)
#define SAMP_PATH   "./results"      // Output directory (must exist)
#define BUF_SIZE    10000             // Observations per file
// ============================

typedef struct {
    off_t   key;     // MSR address or similar
    uint32_t   value;   // int
    UT_hash_handle hh;
} msr_entry;
static msr_entry *prev_raw_dict = NULL;


// Global multipliers
double PKG_ENERGY_UNIT = 0.0;  // Joules per raw unit

// Persistent arrays
static int64_t ts_buf[BUF_SIZE];
static float   e_buf_pkg[BUF_SIZE];   //cpu 
static float   e_buf_dram[BUF_SIZE];  //dram
static float   e_buf_psys[BUF_SIZE]; //platform, a vague collection of mobo components
static int sample_index = 0;
static int file_index = 0;
static int64_t total_samples = 0;
static int64_t target_samples = 0;

static int fd_msr = -1;
static timer_t timerid;

// ---------------------- Helper Functions ----------------------

msr_entry *get_entry(off_t key) {
    msr_entry *e;

    HASH_FIND(hh, prev_raw_dict, &key, sizeof(off_t), e);
    if (!e) {
        e = malloc(sizeof(*e));
        e->key   = key;
        e->value = 0;   // default
        HASH_ADD(hh, prev_raw_dict, key, sizeof(off_t), e);
    }
    return e;
}

static inline uint64_t read_msr(off_t msr)
{
    uint64_t val = 0;
    if (pread(fd_msr, &val, sizeof(val), msr) != sizeof(val)) {
        perror("pread(MSR)");
        return 0;
    }
    return val;
}

// Read MSR_RAPL_POWER_UNIT and set PKG_ENERGY_UNIT
void init_rapl_units(int cpu)
{
    char msr_path[64];
    snprintf(msr_path, sizeof(msr_path), "/dev/cpu/%d/msr", cpu);
    fd_msr = open(msr_path, O_RDONLY);
    if (fd_msr < 0) {
        perror("open(/dev/cpu/x/msr)");
        exit(1);
    }

    uint64_t val = read_msr(MSR_RAPL_POWER_UNIT);
    unsigned int esu = (val >> 8) & 0x1F;  // bits 12:8
    PKG_ENERGY_UNIT = 1.0 / (1<< esu);

    printf("RAPL Energy Unit = %.9f J/unit (ESU=%u)\n", PKG_ENERGY_UNIT, esu);
}

// Write buffer to binary file
void flush_to_file(void)
{
    /* 1) Ensure directory exists */
    struct stat st;
    if (stat(SAMP_PATH, &st) == -1) {   /* Does NOT exist */
        if (mkdir(SAMP_PATH, 0755) != 0) {
            perror("mkdir");
            return;
        }
    }

    /* 2) Build filename */
    char fname[256];
    time_t now = time(NULL);
    struct tm *tm_now = localtime(&now);

    snprintf(fname, sizeof(fname),
             "%s/rapl_%04d%02d%02d_%02d%02d%02d_%03d.bin",
             SAMP_PATH,
             tm_now->tm_year + 1900,
             tm_now->tm_mon + 1,
             tm_now->tm_mday,
             tm_now->tm_hour,
             tm_now->tm_min,
             tm_now->tm_sec,
             file_index++);

    /* 3) Open file */
    FILE *f = fopen(fname, "wb");
    if (!f) {
        perror("fopen");
        return;
    }

    /* 4) Write buffers */
    fwrite(ts_buf,      sizeof(int64_t), sample_index, f);
    fwrite(e_buf_pkg,   sizeof(float),   sample_index, f);
    fwrite(e_buf_dram,  sizeof(float),   sample_index, f);
    fwrite(e_buf_psys,  sizeof(float),   sample_index, f);

    fclose(f);

    printf("Flushed %d samples to %s\n", sample_index, fname);
}


// ---------------------- Timer Callback ----------------------

static float get_energy_msr (off_t msr){
    uint32_t curr_raw = (uint32_t) read_msr(msr);
    uint32_t prev_raw = get_entry(msr)->value;
    uint32_t delta_raw = (curr_raw >= prev_raw)
        ? (curr_raw - prev_raw)
        : (0xFFFFFFFFu - prev_raw + curr_raw + 1u);

    float energy_j = (float)(delta_raw * PKG_ENERGY_UNIT);
    get_entry(msr)->value = curr_raw;
    return energy_j;
}

static void sample_handler(union sigval sv)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    int64_t now_ns = ts.tv_sec * 1000000000LL + ts.tv_nsec;

    //uint32_t curr_raw = (uint32_t) read_msr(MSR_PKG_ENERGY_STATUS);
    //uint32_t delta_raw = (curr_raw >= prev_raw)
    //    ? (curr_raw - prev_raw)
    //    : (0xFFFFFFFFu - prev_raw + curr_raw + 1u);

    //float energy_j = (float)(delta_raw * PKG_ENERGY_UNIT);
    
    //#define MSR_PKG_ENERGY_STATUS   0x611
    //#define MSR_DRAM_ENERGY_STATUS  0x619
    //#define MSR_PLATFORM_ENERGY_COUNTER 0x64D

    ts_buf[sample_index] = now_ns;
    e_buf_pkg[sample_index]  = get_energy_msr(MSR_PKG_ENERGY_STATUS);
    e_buf_dram[sample_index]  = get_energy_msr(MSR_DRAM_ENERGY_STATUS);
    e_buf_psys[sample_index]  = get_energy_msr(MSR_PLATFORM_ENERGY_COUNTER);
    
    sample_index++;
    total_samples++;

    if (sample_index >= BUF_SIZE) {
        flush_to_file();
        sample_index = 0;
    }

    // Stop after desired number of samples
    if (total_samples >= target_samples) {
        printf("Reached target duration (%.2f s, %ld samples)\n",
               (double)target_samples / SAMP_FREQ, target_samples);
        timer_delete(timerid);

        if (sample_index > 0)
            flush_to_file();

        close(fd_msr);
        printf("Logging complete. Exiting.\n");
        exit(0);
    }
}

// ---------------------- Main ----------------------

int main(int argc, char *argv[])
{
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <duration_seconds>\n", argv[0]);
        return 1;
    }
    double duration_s = atof(argv[1]);
    if (duration_s <= 0.0) {
        fprintf(stderr, "Invalid duration.\n");
        return 1;
    }

    target_samples = (int64_t)(duration_s * SAMP_FREQ);

    struct sched_param param;
    param.sched_priority = 99;
    if (sched_setscheduler(0, SCHED_FIFO, &param) == 0)
        printf("Priority set: %d\n", param.sched_priority);

    printf("Initializing RAPL logger...\n");
    init_rapl_units(CPU_INDEX);
    //prev_raw = (uint32_t) read_msr(MSR_PKG_ENERGY_STATUS);
    get_entry(MSR_PKG_ENERGY_STATUS)->value = (uint32_t) read_msr(MSR_PKG_ENERGY_STATUS);
    get_entry(MSR_DRAM_ENERGY_STATUS)->value = (uint32_t) read_msr(MSR_DRAM_ENERGY_STATUS);
    get_entry(MSR_PLATFORM_ENERGY_COUNTER)->value = (uint32_t) read_msr(MSR_PLATFORM_ENERGY_COUNTER);

    // Set up POSIX timer
    struct sigevent sev = {0};
    sev.sigev_notify = SIGEV_THREAD;
    sev.sigev_notify_function = sample_handler;

    if (timer_create(CLOCK_MONOTONIC, &sev, &timerid) != 0) {
        perror("timer_create");
        return 1;
    }

    struct itimerspec its;
    its.it_value.tv_sec = 0;
    its.it_value.tv_nsec = 1000000000 / SAMP_FREQ;
    its.it_interval = its.it_value;

    if (timer_settime(timerid, 0, &its, NULL) != 0) {
        perror("timer_settime");
        return 1;
    }

    printf("Sampling every %.6f s (%d Hz) for %.2f seconds (%ld total samples)\n",
           1.0 / SAMP_FREQ, SAMP_FREQ, duration_s, target_samples);
    printf("Output path: %s\n", SAMP_PATH);
    printf("Press Ctrl+C to stop early.\n");

    while (1)
        pause();

    return 0;
}
