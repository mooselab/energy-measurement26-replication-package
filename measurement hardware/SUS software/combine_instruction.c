// combine two instruction type for each core. threads are pinned to the core. 

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>
#include <immintrin.h>
#include <string.h>
#include <sched.h>

// ---------------------- Thread Data ----------------------

typedef struct {
    void* (*workload)(void*);
    double duration;
    int core_id;
} thread_data_t;

// ---------------------- Workloads ------------------------

void* int32_workload(void* arg) {
    double duration = ((thread_data_t*)arg)->duration;
    time_t start = time(NULL);
    volatile int32_t x = 1;

    while (difftime(time(NULL), start) < duration) {
        for (int i = 0; i < 200000; i++) {
            x += i;
            x ^= (x << 1);
        }
    }
    return NULL;
}

void* int64_workload(void* arg) {
    double duration = ((thread_data_t*)arg)->duration;
    time_t start = time(NULL);
    volatile int64_t x = 1;

    while (difftime(time(NULL), start) < duration) {
        for (int i = 0; i < 200000; i++) {
            x += i;
            x ^= (x << 1);
        }
    }
    return NULL;
}

void* int128_workload(void* arg) {
    double duration = ((thread_data_t*)arg)->duration;
    time_t start = time(NULL);
    volatile __int128 x = 1;

    while (difftime(time(NULL), start) < duration) {
        for (int i = 0; i < 200000; i++) {
            x += i;
            x ^= (x << 1);
        }
    }
    return NULL;
}

void* fp32_workload(void* arg) {
    double duration = ((thread_data_t*)arg)->duration;
    time_t start = time(NULL);
    volatile float x = 1.001f;

    while (difftime(time(NULL), start) < duration) {
        for (int i = 0; i < 200000; i++) {
            x = x * 1.00001f + 0.000001f;
        }
    }
    return NULL;
}

void* fp64_workload(void* arg) {
    double duration = ((thread_data_t*)arg)->duration;
    time_t start = time(NULL);
    volatile double x = 1.0000001;

    while (difftime(time(NULL), start) < duration) {
        for (int i = 0; i < 200000; i++) {
            x = x * 1.00000001 + 0.00000001;
        }
    }
    return NULL;
}

#include <mmintrin.h>

void* mmx_workload(void* arg) {
    double duration = ((thread_data_t*)arg)->duration;
    time_t start = time(NULL);
    __m64 a = _mm_set1_pi16(1);
    __m64 b = _mm_set1_pi16(2);

    while (difftime(time(NULL), start) < duration) {
        for (int i = 0; i < 100000; i++) {
            a = _mm_add_pi16(a, b);
            a = _mm_mullo_pi16(a, b);
        }
    }

    _mm_empty();  // important for MMX
    return NULL;
}

#include <xmmintrin.h>
#include <emmintrin.h>

void* sse_workload(void* arg) {
    double duration = ((thread_data_t*)arg)->duration;
    time_t start = time(NULL);
    __m128 a = _mm_set1_ps(1.1f);
    __m128 b = _mm_set1_ps(2.2f);

    while (difftime(time(NULL), start) < duration) {
        for (int i = 0; i < 50000; i++) {
            a = _mm_add_ps(a, b);
            a = _mm_mul_ps(a, b);
        }
    }
    return NULL;
}

void* sse2_workload(void* arg) {
    double duration = ((thread_data_t*)arg)->duration;
    time_t start = time(NULL);
    __m128i a = _mm_set1_epi32(1);
    __m128i b = _mm_set1_epi32(2);

    while (difftime(time(NULL), start) < duration) {
        for (int i = 0; i < 50000; i++) {
            a = _mm_add_epi32(a, b);
            a = _mm_slli_epi32(a, 1);
        }
    }
    return NULL;
}

#include <immintrin.h>

void* avx_workload(void* arg) {
    double duration = ((thread_data_t*)arg)->duration;
    time_t start = time(NULL);
    __m256 a = _mm256_set1_ps(1.1f);
    __m256 b = _mm256_set1_ps(2.2f);

    while (difftime(time(NULL), start) < duration) {
        for (int i = 0; i < 50000; i++) {
            a = _mm256_add_ps(a, b);
            a = _mm256_mul_ps(a, b);
        }
    }
    return NULL;
}

void* avx2_workload(void* arg) {
    double duration = ((thread_data_t*)arg)->duration;
    time_t start = time(NULL);
    __m256i a = _mm256_set1_epi32(1);
    __m256i b = _mm256_set1_epi32(2);

    while (difftime(time(NULL), start) < duration) {
        for (int i = 0; i < 50000; i++) {
            a = _mm256_add_epi32(a, b);
            a = _mm256_mullo_epi32(a, b);
        }
    }
    return NULL;
}

#ifdef __AVX512F__
#include <immintrin.h>

void* avx512_workload(void* arg) {
    double duration = ((thread_data_t*)arg)->duration;
    time_t start = time(NULL);
    __m512 a = _mm512_set1_ps(1.1f);
    __m512 b = _mm512_set1_ps(2.2f);

    while (difftime(time(NULL), start) < duration) {
        for (int i = 0; i < 30000; i++) {
            a = _mm512_add_ps(a, b);
            a = _mm512_mul_ps(a, b);
        }
    }
    return NULL;
}
#endif

void* idle_workload(void* arg) {
    double duration = ((thread_data_t*)arg)->duration;
    sleep((int)duration);
    return NULL;
}


// ---------------------- Workload Table -------------------

typedef struct {
    const char* name;
    void* (*func)(void*);
} workload_opt;

workload_opt workloads[] = {
    {"int32",   int32_workload},
    {"int64",   int64_workload},
    {"int128",  int128_workload},
    {"fp32",    fp32_workload},
    {"fp64",    fp64_workload},
    {"MMX",     mmx_workload},
    {"SSE",     sse_workload},
    {"SSE2",    sse2_workload},
    {"AVX",     avx_workload},
    {"AVX2",    avx2_workload},
#ifdef __AVX512F__
    {"AVX-512", avx512_workload},
#endif
    {"idle",    idle_workload},
};

// ---------------------- Helpers --------------------------

void pin_thread_to_core(pthread_t thread, int core_id) {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core_id, &cpuset);
    pthread_setaffinity_np(thread, sizeof(cpu_set_t), &cpuset);
}

// ---------------------- Benchmark Logic ------------------

void run_dual_workload_per_core(
    int cores,
    void* (*workload1)(void*),
    void* (*workload2)(void*),
    double duration
) {
    pthread_t threads[cores][2];
    thread_data_t data[cores][2];

    for (int c = 0; c < cores; c++) {

        data[c][0] = (thread_data_t){workload1, duration, c};
        data[c][1] = (thread_data_t){workload2, duration, c};

        pthread_create(&threads[c][0], NULL, workload1, &data[c][0]);
        pthread_create(&threads[c][1], NULL, workload2, &data[c][1]);

        pin_thread_to_core(threads[c][0], c);
        pin_thread_to_core(threads[c][1], c);
    }

    for (int c = 0; c < cores; c++) {
        pthread_join(threads[c][0], NULL);
        pthread_join(threads[c][1], NULL);
    }
}

// ---------------------- Main -----------------------------

int main(int argc, char *argv[]) {

    const char *workload1_arg = NULL;
    const char *workload2_arg = NULL;
    int cores = -1;
    int duration = -1;

    for (int i = 1; i < argc; i++) {

        if (!strcmp(argv[i], "--cores") && i + 1 < argc)
            cores = atoi(argv[++i]);

        else if (!strcmp(argv[i], "--workload1") && i + 1 < argc)
            workload1_arg = argv[++i];

        else if (!strcmp(argv[i], "--workload2") && i + 1 < argc)
            workload2_arg = argv[++i];

        else if (!strcmp(argv[i], "--duration") && i + 1 < argc)
            duration = atoi(argv[++i]);
    }

    if (!workload1_arg || !workload2_arg || cores <= 0 || duration <= 0) {
        fprintf(stderr,
            "Usage:\n"
            "./bench --cores <n> --workload1 <AVX|X86_64|IPC|idle> "
            "--workload2 <AVX|X86_64|IPC|idle> --duration <seconds>\n"
        );
        return 1;
    }

    void* (*w1)(void*) = NULL;
    void* (*w2)(void*) = NULL;

    for (int i = 0; i < 11; i++) {
        if (strcmp(workload1_arg, workloads[i].name) == 0)
            w1 = workloads[i].func;
        if (strcmp(workload2_arg, workloads[i].name) == 0)
            w2 = workloads[i].func;
    }

    if (!w1 || !w2) {
        fprintf(stderr, "Invalid workload(s)\n");
        return 1;
    }

    run_dual_workload_per_core(cores, w1, w2, duration);

    return 0;
}
