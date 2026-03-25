// =============================================================
//  Diehard Squeeze Test
//  Reads unsigned 32-bit integers from "output.dat" (binary)
//
//  Algorithm:
//    Start with K = 2^31
//    Repeatedly multiply K by a uniform random in (0,1)
//    Count steps until K <= 1
//    Repeat 100,000 times
//    Chi-square test of step-count distribution vs theoretical
// =============================================================

#include <iostream>
#include <fstream>
#include <vector>
#include <cmath>
#include <iomanip>
#include <string>
#include <cstdint>

// ------------------------------------------------------------
// 1.  DATA READER
//     Reads all uint32 values from output.dat into a buffer.
// ------------------------------------------------------------
class RandomBuffer {
public:
    std::vector<uint32_t> data;
    size_t pos = 0;

    bool load(const std::string& filename) {
        std::ifstream f(filename, std::ios::binary);
        if (!f) {
            std::cerr << "ERROR: Cannot open " << filename << "\n";
            return false;
        }
        uint32_t val;
        while (f.read(reinterpret_cast<char*>(&val), sizeof(val)))
            data.push_back(val);

        std::cout << "Loaded " << data.size()
                  << " uint32 values from " << filename << "\n\n";
        return !data.empty();
    }

    // Return next value as double in (0, 1)
    // We avoid exact 0 by using (val + 0.5) / 2^32
    double next() {
        if (pos >= data.size()) {
            std::cerr << "ERROR: Ran out of random data at position "
                      << pos << "\n";
            std::exit(1);
        }
        return (data[pos++] + 0.5) / 4294967296.0;
    }

    size_t remaining() const { return data.size() - pos; }
    size_t used()      const { return pos; }
};

// ------------------------------------------------------------
// 2.  THEORETICAL STEP-COUNT PROBABILITIES
//
//  The number of steps N to reduce 2^31 to <= 1 by repeated
//  multiplication by Uniform(0,1) satisfies:
//
//    P(N = k) = (ln 2^31)^k  *  e^(-ln 2^31)  /  k!
//             = (31 ln 2)^k  *  e^(-31 ln 2)  /  k!
//
//  This is a POISSON distribution with lambda = 31 * ln(2).
//  lambda = 31 * 0.693147... = 21.487...
//
//  Wait — that's the Poisson mean if each step multiplies by
//  e^(-Exp(1)).  For Uniform(0,1) multipliers the correct
//  derivation gives lambda = ln(2^31) = 31*ln2 ≈ 21.49 only
//  for the Poisson approximation.
//
//  The EXACT distribution:
//    P(N >= k) = (ln M)^(k-1) / (k-1)!  *  e^{-ln M}   ... 
//  is actually Poisson(lambda = ln M) where M = 2^31.
//
//  So lambda = ln(2^31) = 31 * ln(2) ≈ 21.4875...
//
//  We compute Poisson PMF carefully to avoid overflow.
// ------------------------------------------------------------
static const double LAMBDA = 31.0 * std::log(2.0);   // ~21.4875

// log of Poisson PMF:  log P(N=k) = k*log(lambda) - lambda - log(k!)
static double poisson_log_pmf(int k) {
    if (k < 0) return -1e300;
    double logp = k * std::log(LAMBDA) - LAMBDA;
    for (int i = 1; i <= k; i++) logp -= std::log((double)i);
    return logp;
}

static double poisson_pmf(int k) {
    return std::exp(poisson_log_pmf(k));
}

// ------------------------------------------------------------
// 3.  CHI-SQUARE P-VALUE
//     Same Lanczos + incomplete gamma as craps_test.cpp
// ------------------------------------------------------------
static double log_gamma(double x) {
    static const double c[] = {
        76.18009172947146, -86.50532032941677,
        24.01409824083091, -1.231739572450155,
         0.1208650973866179e-2, -0.5395239384953e-5
    };
    double y = x, tmp = x + 5.5;
    tmp -= (x + 0.5) * std::log(tmp);
    double ser = 1.000000000190015;
    for (int j = 0; j < 6; j++) ser += c[j] / ++y;
    return -tmp + std::log(2.5066282746310005 * ser / x);
}

static double gamma_cf(double a, double x) {
    const int ITMAX = 200;
    const double EPS = 3.0e-7, FPMIN = 1.0e-30;
    double b = x + 1.0 - a, c = 1.0 / FPMIN, d = 1.0 / b, h = d;
    for (int i = 1; i <= ITMAX; i++) {
        double an = -i * (i - a);
        b += 2.0;
        d = an * d + b; if (std::fabs(d) < FPMIN) d = FPMIN;
        c = b + an / c; if (std::fabs(c) < FPMIN) c = FPMIN;
        d = 1.0 / d;
        double del = d * c;
        h *= del;
        if (std::fabs(del - 1.0) < EPS) break;
    }
    return std::exp(-x + a * std::log(x) - log_gamma(a)) * h;
}

static double gamma_series(double a, double x) {
    const int ITMAX = 200;
    const double EPS = 3.0e-7;
    double ap = a, sum = 1.0 / a, del = sum;
    for (int i = 0; i < ITMAX; i++) {
        ap++; del *= x / ap; sum += del;
        if (std::fabs(del) < std::fabs(sum) * EPS) break;
    }
    return sum * std::exp(-x + a * std::log(x) - log_gamma(a));
}

// P(chi2 > chi2_val | df degrees of freedom)
static double chi2_pvalue(double chi2_val, int df) {
    double a = df / 2.0, x = chi2_val / 2.0;
    if (x <= 0 || a <= 0) return 1.0;
    if (x < a + 1.0)
        return 1.0 - gamma_series(a, x);
    else
        return gamma_cf(a, x);
}

// ------------------------------------------------------------
// 4.  BUILD BINS
//     We group steps into bins ensuring every bin has
//     expected count >= 5  (standard chi-square requirement).
//     Tail bins are merged automatically.
// ------------------------------------------------------------
struct Bin {
    int  lo, hi;          // inclusive step range (hi=-1 means open tail)
    double expected;      // expected count
    int    observed;      // observed count
};

static std::vector<Bin> build_bins(int num_runs) {
    // Raw Poisson probabilities for steps 0..120
    // (essentially zero outside that range for lambda~21.5)
    const int KMAX = 120;
    std::vector<double> prob(KMAX + 1);
    for (int k = 0; k <= KMAX; k++)
        prob[k] = poisson_pmf(k);

    // Merge into bins with expected >= 5
    std::vector<Bin> bins;
    int lo = 0;
    double acc = 0.0;

    for (int k = 0; k <= KMAX; k++) {
        acc += prob[k] * num_runs;
        if (acc >= 5.0) {
            bins.push_back({lo, k, acc, 0});
            lo = k + 1;
            acc = 0.0;
        }
    }
    // Absorb any remaining probability into last bin as open tail
    if (acc > 0 && !bins.empty()) {
        bins.back().hi      = -1;   // open-ended
        bins.back().expected += acc;
    } else if (acc > 0) {
        bins.push_back({lo, -1, acc, 0});
    }

    return bins;
}

// Return which bin index a step count belongs to
static int find_bin(const std::vector<Bin>& bins, int steps) {
    for (int i = 0; i < (int)bins.size(); i++) {
        if (bins[i].hi == -1 || steps <= bins[i].hi)
            return i;
    }
    return (int)bins.size() - 1;   // overflow into last bin
}

// ------------------------------------------------------------
// 5.  MAIN
// ------------------------------------------------------------
int main(int argc, char* argv[]) {

    std::string filename = (argc > 1) ? argv[1] : "output.dat";

    std::cout << "================================================\n";
    std::cout << "        DIEHARD  —  SQUEEZE  TEST\n";
    std::cout << "================================================\n\n";

    // --- Load data ---
    RandomBuffer rng;
    if (!rng.load(filename)) return 1;

    const int NUM_RUNS = 100000;
    const double K_START = (double)(1UL << 31);   // 2^31

    // Rough data check: avg ~47 numbers per squeeze
    size_t recommended = (size_t)NUM_RUNS * 60;
    if (rng.data.size() < recommended) {
        std::cout << "WARNING: File has " << rng.data.size()
                  << " values. Recommended >= " << recommended
                  << " for " << NUM_RUNS << " squeezes.\n"
                  << "Will attempt anyway.\n\n";
    }

    // --- Build theoretical bins ---
    std::vector<Bin> bins = build_bins(NUM_RUNS);

    // --- Run squeezes ---
    std::cout << "Running " << NUM_RUNS << " squeeze runs...\n\n";

    long long total_steps = 0;
    int min_steps = INT32_MAX, max_steps = 0;

    for (int run = 0; run < NUM_RUNS; run++) {
        double K = K_START;
        int steps = 0;

        while (K > 1.0) {
            K *= rng.next();
            steps++;
        }

        total_steps += steps;
        if (steps < min_steps) min_steps = steps;
        if (steps > max_steps) max_steps = steps;

        int b = find_bin(bins, steps);
        bins[b].observed++;
    }

    size_t rng_used = rng.used();
    double avg_steps = (double)total_steps / NUM_RUNS;

    // --- Print step-count statistics ---
    std::cout << "------------------------------------------------\n";
    std::cout << "  STEP COUNT STATISTICS\n";
    std::cout << "------------------------------------------------\n";
    std::cout << std::fixed << std::setprecision(3);
    std::cout << "  Total squeezes      : " << NUM_RUNS    << "\n";
    std::cout << "  Total steps used    : " << total_steps << "\n";
    std::cout << "  Average steps/run   : " << avg_steps   << "\n";
    std::cout << "  Theoretical average : " << LAMBDA      << "\n";
    std::cout << "  Min steps seen      : " << min_steps   << "\n";
    std::cout << "  Max steps seen      : " << max_steps   << "\n";

    // --- Print bin table ---
    std::cout << "\n------------------------------------------------\n";
    std::cout << "  DISTRIBUTION TABLE\n";
    std::cout << "------------------------------------------------\n";
    std::cout << std::setw(12) << "Steps"
              << std::setw(12) << "Observed"
              << std::setw(12) << "Expected"
              << std::setw(12) << "Chi-sq\n";
    std::cout << "  " << std::string(46, '-') << "\n";

    double chi2 = 0.0;

    for (const auto& b : bins) {
        double obs = b.observed;
        double exp = b.expected;
        double c   = (obs - exp) * (obs - exp) / exp;
        chi2 += c;

        // Format range label
        std::string label;
        if (b.hi == -1)
            label = std::to_string(b.lo) + "+";
        else if (b.lo == b.hi)
            label = std::to_string(b.lo);
        else
            label = std::to_string(b.lo) + "-" + std::to_string(b.hi);

        std::cout << std::setw(12) << label
                  << std::setw(12) << (int)obs
                  << std::setw(12) << std::setprecision(2) << exp
                  << std::setw(12) << std::setprecision(4) << c << "\n";
    }

    // --- Chi-square result ---
    int df = (int)bins.size() - 1;
    double pval = chi2_pvalue(chi2, df);

    std::cout << "\n------------------------------------------------\n";
    std::cout << "  CHI-SQUARE RESULT\n";
    std::cout << "------------------------------------------------\n";
    std::cout << std::setprecision(4);
    std::cout << "  Number of bins        : " << bins.size() << "\n";
    std::cout << "  Degrees of freedom    : " << df          << "\n";
    std::cout << "  Chi-square value      : " << chi2        << "\n";
    std::cout << "  p-value               : " << pval        << "\n";
    std::cout << "  Result                : ";
    if (pval < 0.01 || pval > 0.99)
        std::cout << "*** FAIL *** (p-value outside [0.01, 0.99])\n";
    else
        std::cout << "PASS\n";

    // --- Resource summary ---
    std::cout << "\n================================================\n";
    std::cout << "  RESOURCE SUMMARY\n";
    std::cout << "================================================\n";
    std::cout << "  Random numbers consumed : " << rng_used        << "\n";
    std::cout << "  Remaining in buffer     : " << rng.remaining() << "\n";
    std::cout << "  Avg randoms per squeeze : "
              << std::setprecision(2)
              << (double)rng_used / NUM_RUNS << "\n";

    std::cout << "\n================================================\n";
    bool pass = (pval >= 0.01 && pval <= 0.99);
    std::cout << "  OVERALL: " << (pass ? "PASS — RNG appears random."
                                        : "FAIL — RNG shows non-random behavior.")
              << "\n";
    std::cout << "================================================\n";

    return 0;
}
