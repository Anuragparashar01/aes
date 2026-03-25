// =============================================================
//  Diehard Overlapping Sums Test
//  Reads unsigned 32-bit integers from "output.dat" (binary)
//
//  Algorithm:
//    Generate stream of uniform(0,1) values
//    Compute 100,000 overlapping sums of window size 100
//    Each sum ~ Normal(mean=50, var=100/12)
//    Standardize → Z-scores → apply normal CDF → uniform values
//    Kolmogorov-Smirnov test on those uniform values
// =============================================================

#include <iostream>
#include <fstream>
#include <vector>
#include <cmath>
#include <iomanip>
#include <string>
#include <algorithm>
#include <cstdint>

// ------------------------------------------------------------
// 1.  DATA READER
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

    // Uniform in (0,1) — avoid exact 0 and 1
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
// 2.  NORMAL CDF  Φ(z)
//     Uses the complementary error function from <cmath>
//     Φ(z) = erfc(-z / sqrt(2)) / 2
// ------------------------------------------------------------
static double normal_cdf(double z) {
    return 0.5 * std::erfc(-z / std::sqrt(2.0));
}

// ------------------------------------------------------------
// 3.  KOLMOGOROV-SMIRNOV TEST
//
//  Given n uniform(0,1) values (already sorted),
//  compute KS statistic D = max|F_empirical - F_uniform|
//  then convert to p-value using the KS distribution.
//
//  KS p-value: P(D > d) = 2 * sum_{k=1}^{inf} (-1)^{k+1}
//                              * exp(-2 k^2 n d^2)
//  We use the standard approximation:
//    Q_KS(lambda) where lambda = (sqrt(n) + 0.12 + 0.11/sqrt(n)) * D
// ------------------------------------------------------------
static double ks_pvalue(double D, int n) {
    // Adjust D with continuity correction
    double lambda = (std::sqrt((double)n) + 0.12 + 0.11 / std::sqrt((double)n)) * D;

    // Series: Q = 2 * sum_{k=1}^{inf} (-1)^{k+1} exp(-2 k^2 lambda^2)
    double sum = 0.0;
    double prev = 0.0;
    for (int k = 1; k <= 200; k++) {
        double term = 2.0 * std::exp(-2.0 * k * k * lambda * lambda);
        if (k % 2 == 0) term = -term;
        sum += term;
        if (k > 3 && std::fabs(term) < 1e-12 * std::fabs(sum)) break;
        if (k > 3 && std::fabs(sum - prev) < 1e-12) break;
        prev = sum;
    }
    if (sum < 0.0) sum = 0.0;
    if (sum > 1.0) sum = 1.0;
    return sum;
}

// ------------------------------------------------------------
// 4.  MAIN
// ------------------------------------------------------------
int main(int argc, char* argv[]) {

    std::string filename = (argc > 1) ? argv[1] : "output.dat";

    std::cout << "================================================\n";
    std::cout << "     DIEHARD  —  OVERLAPPING SUMS TEST\n";
    std::cout << "================================================\n\n";

    // --- Load data ---
    RandomBuffer rng;
    if (!rng.load(filename)) return 1;

    // Parameters
    const int WINDOW    = 100;       // sum window size
    const int NUM_SUMS  = 100000;    // number of overlapping sums
    const int NEEDED    = NUM_SUMS + WINDOW - 1;  // = 100,099

    // Distribution parameters for sum of WINDOW Uniform(0,1)
    const double MU     = WINDOW * 0.5;                      // 50.0
    const double SIGMA  = std::sqrt(WINDOW / 12.0);          // ~2.887

    if ((int)rng.data.size() < NEEDED) {
        std::cout << "WARNING: Need " << NEEDED << " values but file has "
                  << rng.data.size() << ".\n"
                  << "Will attempt anyway.\n\n";
    }

    // -------------------------------------------------------
    // 5.  BUILD BASE STREAM
    //     Load all NEEDED values into a local array
    // -------------------------------------------------------
    std::vector<double> stream(NEEDED);
    for (int i = 0; i < NEEDED; i++)
        stream[i] = rng.next();

    size_t rng_used = rng.used();

    // -------------------------------------------------------
    // 6.  COMPUTE FIRST SUM (positions 0 to WINDOW-1)
    // -------------------------------------------------------
    double window_sum = 0.0;
    for (int i = 0; i < WINDOW; i++)
        window_sum += stream[i];

    // -------------------------------------------------------
    // 7.  SLIDE WINDOW — collect all sums and transform
    //
    //     For each sum:
    //       a) Standardize  →  Z = (sum - mu) / sigma
    //       b) Apply CDF    →  U = Φ(Z)        [should be Uniform(0,1)]
    //       c) Store U for KS test
    // -------------------------------------------------------
    std::vector<double> uniforms(NUM_SUMS);

    // First sum
    double z = (window_sum - MU) / SIGMA;
    uniforms[0] = normal_cdf(z);

    // Sliding window
    for (int i = 1; i < NUM_SUMS; i++) {
        window_sum -= stream[i - 1];          // drop oldest
        window_sum += stream[i + WINDOW - 1]; // add newest
        z = (window_sum - MU) / SIGMA;
        uniforms[i] = normal_cdf(z);
    }

    // -------------------------------------------------------
    // 8.  DESCRIPTIVE STATISTICS ON THE SUMS
    //     (for diagnostic output)
    // -------------------------------------------------------

    // Recompute raw sums for stats reporting
    double raw_sum_first = 0.0;
    for (int i = 0; i < WINDOW; i++) raw_sum_first += stream[i];

    double sum_min = 1e18, sum_max = -1e18;
    double sum_acc = 0.0;
    double running = raw_sum_first;
    sum_min = sum_max = running;
    sum_acc = running;

    for (int i = 1; i < NUM_SUMS; i++) {
        running -= stream[i - 1];
        running += stream[i + WINDOW - 1];
        sum_acc += running;
        if (running < sum_min) sum_min = running;
        if (running > sum_max) sum_max = running;
    }
    double sum_mean = sum_acc / NUM_SUMS;

    // -------------------------------------------------------
    // 9.  KS TEST
    //     Sort the uniform values, compute D statistic
    // -------------------------------------------------------
    std::vector<double> sorted_u = uniforms;
    std::sort(sorted_u.begin(), sorted_u.end());

    double D_plus  = 0.0;   // max(i/n - U_i)  upper
    double D_minus = 0.0;   // max(U_i - (i-1)/n)  lower
    int n = NUM_SUMS;

    for (int i = 0; i < n; i++) {
        double upper = (double)(i + 1) / n - sorted_u[i];
        double lower = sorted_u[i] - (double)i / n;
        if (upper > D_plus)  D_plus  = upper;
        if (lower > D_minus) D_minus = lower;
    }
    double D = std::max(D_plus, D_minus);
    double pval = ks_pvalue(D, n);

    // -------------------------------------------------------
    // 10.  UNIFORM DISTRIBUTION CHECK
    //      Bin U values into 10 equal bins [0,0.1), [0.1,0.2)...
    //      Each bin should have ~10,000 values
    // -------------------------------------------------------
    const int NBINS = 10;
    int bin_counts[NBINS] = {0};
    for (double u : uniforms) {
        int b = (int)(u * NBINS);
        if (b >= NBINS) b = NBINS - 1;
        bin_counts[b]++;
    }

    // -------------------------------------------------------
    // 11.  PRINT RESULTS
    // -------------------------------------------------------
    std::cout << "------------------------------------------------\n";
    std::cout << "  STREAM STATISTICS\n";
    std::cout << "------------------------------------------------\n";
    std::cout << std::fixed << std::setprecision(4);
    std::cout << "  Window size           : " << WINDOW          << "\n";
    std::cout << "  Number of sums        : " << NUM_SUMS        << "\n";
    std::cout << "  Total values consumed : " << rng_used        << "\n";
    std::cout << "  Theoretical mean      : " << MU              << "\n";
    std::cout << "  Theoretical std dev   : " << SIGMA           << "\n";
    std::cout << "  Observed mean of sums : " << sum_mean        << "\n";
    std::cout << "  Min sum seen          : " << sum_min         << "\n";
    std::cout << "  Max sum seen          : " << sum_max         << "\n";

    std::cout << "\n------------------------------------------------\n";
    std::cout << "  UNIFORM DISTRIBUTION OF TRANSFORMED VALUES\n";
    std::cout << "  (Each bin should have ~" << NUM_SUMS/NBINS << " values)\n";
    std::cout << "------------------------------------------------\n";
    std::cout << std::setw(14) << "Bin range"
              << std::setw(12) << "Observed"
              << std::setw(12) << "Expected"
              << std::setw(10) << "Deviation\n";
    std::cout << "  " << std::string(46, '-') << "\n";

    double expected_per_bin = (double)NUM_SUMS / NBINS;
    for (int b = 0; b < NBINS; b++) {
        std::string label = "[" + std::to_string(b * 10) + "%-"
                                + std::to_string((b+1)*10) + "%)";
        double dev = bin_counts[b] - expected_per_bin;
        std::cout << std::setw(14) << label
                  << std::setw(12) << bin_counts[b]
                  << std::setw(12) << std::setprecision(1) << expected_per_bin
                  << std::setw(10) << std::setprecision(1)
                  << std::showpos << dev << std::noshowpos << "\n";
    }

    std::cout << "\n------------------------------------------------\n";
    std::cout << "  KOLMOGOROV-SMIRNOV TEST RESULT\n";
    std::cout << "------------------------------------------------\n";
    std::cout << std::setprecision(6);
    std::cout << "  n (sample size)       : " << n        << "\n";
    std::cout << "  D+ statistic          : " << D_plus   << "\n";
    std::cout << "  D- statistic          : " << D_minus  << "\n";
    std::cout << "  D  statistic (max)    : " << D        << "\n";
    std::cout << "  p-value               : " << pval     << "\n";
    std::cout << "  Result                : ";
    if (pval < 0.01 || pval > 0.99)
        std::cout << "*** FAIL *** (p-value outside [0.01, 0.99])\n";
    else
        std::cout << "PASS\n";

    // -------------------------------------------------------
    // 12.  SUMMARY
    // -------------------------------------------------------
    std::cout << "\n================================================\n";
    std::cout << "  SUMMARY\n";
    std::cout << "================================================\n";
    std::cout << "  Random numbers consumed : " << rng_used        << "\n";
    std::cout << "  Remaining in buffer     : " << rng.remaining() << "\n\n";

    bool pass = (pval >= 0.01 && pval <= 0.99);
    std::cout << "  KS p-value  : " << std::setprecision(6) << pval
              << "  ->  " << (pass ? "PASS" : "FAIL") << "\n\n";
    std::cout << "  OVERALL: "
              << (pass ? "PASS — RNG appears random."
                       : "FAIL — RNG shows non-random behavior.")
              << "\n";
    std::cout << "================================================\n";

    return 0;
}
