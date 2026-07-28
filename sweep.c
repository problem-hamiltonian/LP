/* sweep.c - MT19937 one-time-pad seed sweep for the Liber Primus
 *
 * Replicates CPython's random module EXACTLY:
 *   - init_by_array seeding (what random.seed(int) does)
 *   - randrange(29) via _randbelow: 5 bits, reject >= 29
 *   - random() via the 53-bit double construction
 * Also supports init_genrand seeding (C++ std::mt19937, older code).
 *
 * Build:  cc -O3 -march=native -o sweep sweep.c
 * Run:    ./sweep corpus.txt START END          # seeds [START,END)
 *
 * corpus.txt = one integer 0..28 per line (unsolved rune values, in order)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define N 624
#define M 397
#define MATRIX_A 0x9908b0dfUL
#define UPPER 0x80000000UL
#define LOWER 0x7fffffffUL

static uint32_t mt[N];
static int mti = N + 1;

static void init_genrand(uint32_t s) {
    mt[0] = s;
    for (mti = 1; mti < N; mti++)
        mt[mti] = (1812433253UL * (mt[mti-1] ^ (mt[mti-1] >> 30)) + mti);
}
/* CPython: random.seed(int) -> init_by_array over the int's 32-bit words */
static void init_by_array(uint32_t *key, int klen) {
    int i = 1, j = 0, k = (N > klen ? N : klen);
    init_genrand(19650218UL);
    for (; k; k--) {
        mt[i] = (mt[i] ^ ((mt[i-1] ^ (mt[i-1] >> 30)) * 1664525UL)) + key[j] + j;
        i++; j++;
        if (i >= N) { mt[0] = mt[N-1]; i = 1; }
        if (j >= klen) j = 0;
    }
    for (k = N - 1; k; k--) {
        mt[i] = (mt[i] ^ ((mt[i-1] ^ (mt[i-1] >> 30)) * 1566083941UL)) - i;
        i++;
        if (i >= N) { mt[0] = mt[N-1]; i = 1; }
    }
    mt[0] = 0x80000000UL;
}
static uint32_t genrand(void) {
    uint32_t y;
    static const uint32_t mag01[2] = {0x0UL, MATRIX_A};
    if (mti >= N) {
        int kk;
        for (kk = 0; kk < N-M; kk++) {
            y = (mt[kk] & UPPER) | (mt[kk+1] & LOWER);
            mt[kk] = mt[kk+M] ^ (y >> 1) ^ mag01[y & 0x1UL];
        }
        for (; kk < N-1; kk++) {
            y = (mt[kk] & UPPER) | (mt[kk+(M-N)] & LOWER);
            mt[kk] = mt[kk+(M-N)] ^ (y >> 1) ^ mag01[y & 0x1UL];
        }
        y = (mt[N-1] & UPPER) | (mt[0] & LOWER);
        mt[N-1] = mt[M-1] ^ (y >> 1) ^ mag01[y & 0x1UL];
        mti = 0;
    }
    y = mt[mti++];
    y ^= (y >> 11);
    y ^= (y << 7)  & 0x9d2c5680UL;
    y ^= (y << 15) & 0xefc60000UL;
    y ^= (y >> 18);
    return y;
}
/* CPython randrange(29): getrandbits(5) with rejection */
static inline int next_randrange29(void) {
    uint32_t r;
    do { r = genrand() >> 27; } while (r >= 29);
    return (int)r;
}
/* CPython int(random()*29) */
static inline int next_float29(void) {
    uint32_t a = genrand() >> 5, b = genrand() >> 6;
    double d = (a * 67108864.0 + b) * (1.0 / 9007199254740992.0);
    return (int)(d * 29.0);
}
/* getrandbits(8) % 29 */
static inline int next_bits29(void) { return (int)((genrand() >> 24) % 29); }

#define SCREEN 96      /* cheap first pass */
#define CONFIRM 600    /* second pass on survivors */

static int *corpus; static int clen;

static double ioc(const int *v, int n) {
    int c[29] = {0};
    for (int i = 0; i < n; i++) c[v[i]]++;
    double s = 0;
    for (int i = 0; i < 29; i++) s += (double)c[i] * (c[i] - 1);
    return s / ((double)n * (n - 1)) * 29.0;
}

int main(int argc, char **argv) {
    if (argc < 4) { fprintf(stderr, "usage: %s corpus.txt START END\n", argv[0]); return 1; }
    FILE *f = fopen(argv[1], "r");
    if (!f) { perror("corpus"); return 1; }
    corpus = malloc(sizeof(int) * 200000); clen = 0;
    while (fscanf(f, "%d", &corpus[clen]) == 1) clen++;
    fclose(f);
    uint64_t start = strtoull(argv[2], NULL, 10), end = strtoull(argv[3], NULL, 10);
    fprintf(stderr, "corpus %d runes; seeds [%llu,%llu)\n", clen,
            (unsigned long long)start, (unsigned long long)end);

    int buf[CONFIRM]; int hits = 0;
    for (uint64_t seed = start; seed < end; seed++) {
        for (int mode = 0; mode < 6; mode++) {       /* 3 call patterns x 2 seedings */
            uint32_t key[2] = { (uint32_t)(seed & 0xffffffffUL), (uint32_t)(seed >> 32) };
            if (mode & 1) init_by_array(key, seed >> 32 ? 2 : 1);
            else { init_genrand((uint32_t)seed); mti = N; }
            int pat = mode >> 1;
            for (int i = 0; i < SCREEN; i++) {
                int k = pat == 0 ? next_randrange29() : pat == 1 ? next_float29() : next_bits29();
                buf[i] = ((corpus[i] - k) % 29 + 29) % 29;
            }
            if (ioc(buf, SCREEN) < 1.30) continue;          /* reject ~all */
            for (int i = SCREEN; i < CONFIRM && i < clen; i++) {
                int k = pat == 0 ? next_randrange29() : pat == 1 ? next_float29() : next_bits29();
                buf[i] = ((corpus[i] - k) % 29 + 29) % 29;
            }
            double v = ioc(buf, CONFIRM < clen ? CONFIRM : clen);
            if (v > 1.40) {
                printf("HIT seed=%llu mode=%d ioc=%.4f\n",
                       (unsigned long long)seed, mode, v);
                fflush(stdout); hits++;
            }
        }
    }
    fprintf(stderr, "done; %d hits\n", hits);
    return 0;
}
