// =============================================================
//  Diehard Craps Test
//  Reads unsigned 32-bit integers from "output.dat" (binary)
//  Runs 200,000 craps games and reports:
//    1. Win count + p-value  (normal approximation)
//    2. Throws distribution  + p-value  (chi-square)
// =============================================================

#include <iostream>
#include <fstream>
#include <vector>
#include <cmath>
#include <iomanip>
#include <string>
#include <numeric>
#include <cstdint>

// ------------------------------------------------------------
// 1.  DATA READER
//     Reads all uint32 values from output.dat into a buffer.
//     Each value is treated as a uniform random in [0, 2^32).
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

    // Return next value as double in [0, 1)
    double next() {
        if (pos >= data.size()) {
            std::cerr << "ERROR: Ran out of random data at position "
                      << pos << "\n";
            std::exit(1);
        }
        return data[pos++] / 4294967296.0;   // divide by 2^32
    }

    size_t remaining() const { return data.size() - pos; }
};

// ------------------------------------------------------------
// 2.  DICE ROLL
//     Takes one double in [0,1) → die face 1..6
// ------------------------------------------------------------
static int roll_die(double r) {
    return static_cast<int>(r * 6.0) + 1;   // 1 to 6
}

// ------------------------------------------------------------
// 3.  ONE CRAPS GAME
//     Returns {win(1) or loss(0),  number of throws used}
//     Each throw consumes 2 random numbers (one per die).
// ------------------------------------------------------------
struct GameResult {
    int win;        // 1 = win, 0 = loss
    int throws;     // how many rolls the game lasted
};

static GameResult play_craps(RandomBuffer& rng) {
    int throws = 0;

    // --- first roll ---
    int d1 = roll_die(rng.next());
    int d2 = roll_die(rng.next());
    int sum = d1 + d2;
    throws++;

    // Instant win
    if (sum == 7 || sum == 11) return {1, throws};
    // Instant loss
    if (sum == 2 || sum == 3 || sum == 12) return {0, throws};

    // Point established
    int point = sum;

    // --- subsequent rolls ---
    while (true) {
        d1 = roll_die(rng.next());
        d2 = roll_die(rng.next());
        sum = d1 + d2;
        throws++;

        if (sum == point) return {1, throws};   // made the point → win
        if (sum == 7)     return {0, throws};   // seven out → loss
        // otherwise keep rolling
    }
}

// ------------------------------------------------------------
// 4.  NORMAL DISTRIBUTION  (for wins p-value)
//     erfc from <cmath> gives us the complementary error function.
//     P(Z > z) = erfc(z / sqrt(2)) / 2
// ------------------------------------------------------------
static double normal_pvalue(double z) {
    // two-tailed: probability of seeing |Z| >= |z|
    return std::erfc(std::fabs(z) / std::sqrt(2.0));
}

// ------------------------------------------------------------
// 5.  CHI-SQUARE  p-value  (for throws distribution)
//     Uses regularized incomplete gamma function approximation.
//     P(chi2 > x, df) = 1 - regularized_gamma(df/2, x/2)
//
//     We implement the continued-fraction form of the
//     incomplete gamma function (Numerical Recipes style).
// ------------------------------------------------------------
static double log_gamma(double x) {
    // Lanczos approximation
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

// Continued-fraction incomplete gamma (upper tail)
static double gamma_cf(double a, double x) {
    const int ITMAX = 200;
    const double EPS = 3.0e-7, FPMIN = 1.0e-30;
    double b = x + 1.0 - a, c = 1.0 / FPMIN, d = 1.0 / b;
    double h = d;
    for (int i = 1; i <= ITMAX; i++) {
        double an = -i * (i - a);
        b += 2.0;
        d = an * d + b;  if (std::fabs(d) < FPMIN) d = FPMIN;
        c = b + an / c;  if (std::fabs(c) < FPMIN) c = FPMIN;
        d = 1.0 / d;
        double del = d * c;
        h *= del;
        if (std::fabs(del - 1.0) < EPS) break;
    }
    return std::exp(-x + a * std::log(x) - log_gamma(a)) * h;
}

// Series incomplete gamma (lower tail)
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

// P(chi2 > chi2_val  |  df degrees of freedom)
static double chi2_pvalue(double chi2_val, int df) {
    double a = df / 2.0, x = chi2_val / 2.0;
    if (x < 0 || a <= 0) return 1.0;
    if (x < a + 1.0)
        return 1.0 - gamma_series(a, x);   // upper tail
    else
        return gamma_cf(a, x);
}

// ------------------------------------------------------------
// 6.  THEORETICAL THROW DISTRIBUTION
//     P(game ends on throw k) has known closed form.
//     We group k >= MAX_THROW_BIN into one tail bin.
// ------------------------------------------------------------
static const int MAX_THROW_BIN = 21;   // bins 1..20, then 21+

// Probability of winning/losing on exactly throw k
// (derived from the craps probability theory)
static std::vector<double> theoretical_throw_probs(int num_games) {
    // P(sum = s) for two dice
    double ps[13] = {0};
    for (int d1 = 1; d1 <= 6; d1++)
        for (int d2 = 1; d2 <= 6; d2++)
            ps[d1 + d2] += 1.0 / 36.0;

    // For each point p, P(resolve on throw k | point=p)
    // P(roll p) + P(roll 7) = q_p
    // P(end on throw 1 given point p) = 0  (throw 1 set the point)
    // P(end on throw k | point p) = (1-q_p)^(k-2) * q_p  for k>=2

    // Overall P(game ends in exactly k throws):
    //   k=1: P(7)+P(11)+P(2)+P(3)+P(12)
    //   k>=2: sum over points p of P(point=p) * (1-q_p)^(k-2) * q_p

    double p_end_first = ps[7] + ps[11] + ps[2] + ps[3] + ps[12];

    // points: 4,5,6,8,9,10
    int points[] = {4, 5, 6, 8, 9, 10};
    double q[11] = {0};   // q[p] = P(7) + P(p)
    for (int p : points)
        q[p] = ps[7] + ps[p];

    std::vector<double> prob(MAX_THROW_BIN + 1, 0.0);
    prob[1] = p_end_first;

    double tail = 1.0 - p_end_first;
    for (int k = 2; k < MAX_THROW_BIN; k++) {
        double pk = 0.0;
        for (int p : points)
            pk += ps[p] * std::pow(1.0 - q[p], k - 2) * q[p];
        prob[k] = pk;
        tail -= pk;
    }
    prob[MAX_THROW_BIN] = (tail > 0 ? tail : 0.0);  // tail bin

    // Convert to expected counts
    std::vector<double> expected(MAX_THROW_BIN + 1);
    for (int i = 1; i <= MAX_THROW_BIN; i++)
        expected[i] = prob[i] * num_games;

    return expected;
}

// ------------------------------------------------------------
// 7.  MAIN
// ------------------------------------------------------------
int main(int argc, char* argv[]) {

    std::string filename = (argc > 1) ? argv[1] : "output.dat";

    std::cout << "================================================\n";
    std::cout << "        DIEHARD  —  CRAPS  TEST\n";
    std::cout << "================================================\n\n";

    // --- Load data ---
    RandomBuffer rng;
    if (!rng.load(filename)) return 1;

    const int NUM_GAMES = 200000;

    // Check we have enough data (worst case ~30 throws × 2 per game)
    size_t needed = (size_t)NUM_GAMES * 2 * 5;   // conservative estimate
    if (rng.data.size() < needed) {
        std::cout << "WARNING: File has " << rng.data.size()
                  << " values. Recommended >= " << needed
                  << " for 200,000 games.\n"
                  << "Will attempt anyway (program exits if data runs out).\n\n";
    }

    // --- Run games ---
    std::cout << "Running " << NUM_GAMES << " craps games...\n\n";

    int total_wins = 0;
    std::vector<int> throw_counts(MAX_THROW_BIN + 1, 0);

    for (int g = 0; g < NUM_GAMES; g++) {
        GameResult r = play_craps(rng);
        total_wins += r.win;
        int bin = (r.throws >= MAX_THROW_BIN) ? MAX_THROW_BIN : r.throws;
        throw_counts[bin]++;
    }

    size_t total_rng_used = rng.pos;

    // -------------------------------------------------------
    // 8. REPORT: WINS
    // -------------------------------------------------------
    // Theoretical: p_win = 244/495 ≈ 0.492929...
    double p_win      = 244.0 / 495.0;
    double mean_wins  = NUM_GAMES * p_win;
    double std_wins   = std::sqrt(NUM_GAMES * p_win * (1.0 - p_win));
    double z_score    = (total_wins - mean_wins) / std_wins;
    double p_wins     = normal_pvalue(z_score);

    std::cout << "------------------------------------------------\n";
    std::cout << "  TEST 1: WIN COUNT\n";
    std::cout << "------------------------------------------------\n";
    std::cout << std::fixed << std::setprecision(4);
    std::cout << "  Games played          : " << NUM_GAMES      << "\n";
    std::cout << "  Observed wins         : " << total_wins     << "\n";
    std::cout << "  Expected wins (mean)  : " << mean_wins      << "\n";
    std::cout << "  Std deviation         : " << std_wins       << "\n";
    std::cout << "  Z-score               : " << z_score        << "\n";
    std::cout << "  p-value (two-tailed)  : " << p_wins         << "\n";
    std::cout << "  Result                : ";
    if (p_wins < 0.01 || p_wins > 0.99)
        std::cout << "*** FAIL *** (p-value outside [0.01, 0.99])\n";
    else
        std::cout << "PASS\n";

    // -------------------------------------------------------
    // 9. REPORT: THROWS DISTRIBUTION (Chi-square)
    // -------------------------------------------------------
    std::vector<double> expected = theoretical_throw_probs(NUM_GAMES);

    // Merge bins with expected < 5 into neighbors (standard chi-sq rule)
    // For simplicity, we just group the tail and report as-is.
    // The tail bin (21+) is large enough in practice.

    double chi2 = 0.0;
    int df = 0;

    std::cout << "\n------------------------------------------------\n";
    std::cout << "  TEST 2: THROWS-PER-GAME DISTRIBUTION\n";
    std::cout << "------------------------------------------------\n";
    std::cout << std::setw(10) << "Throws"
              << std::setw(12) << "Observed"
              << std::setw(12) << "Expected"
              << std::setw(12) << "Chi-sq\n";
    std::cout << "  " << std::string(44, '-') << "\n";

    for (int k = 1; k <= MAX_THROW_BIN; k++) {
        double obs = throw_counts[k];
        double exp = expected[k];
        if (exp < 1.0) continue;   // skip negligible bins
        double c = (obs - exp) * (obs - exp) / exp;
        chi2 += c;
        df++;

        std::string label = (k == MAX_THROW_BIN)
            ? std::to_string(k) + "+"
            : std::to_string(k);
        std::cout << std::setw(10) << label
                  << std::setw(12) << (int)obs
                  << std::setw(12) << std::setprecision(1) << exp
                  << std::setw(12) << std::setprecision(4) << c << "\n";
    }

    df--;   // degrees of freedom = bins - 1
    double p_throws = chi2_pvalue(chi2, df);

    std::cout << "\n  Chi-square value      : " << std::setprecision(4)
              << chi2 << "\n";
    std::cout << "  Degrees of freedom    : " << df         << "\n";
    std::cout << "  p-value               : " << p_throws   << "\n";
    std::cout << "  Result                : ";
    if (p_throws < 0.01 || p_throws > 0.99)
        std::cout << "*** FAIL *** (p-value outside [0.01, 0.99])\n";
    else
        std::cout << "PASS\n";

    // -------------------------------------------------------
    // 10. SUMMARY
    // -------------------------------------------------------
    std::cout << "\n================================================\n";
    std::cout << "  SUMMARY\n";
    std::cout << "================================================\n";
    std::cout << "  Random numbers consumed : " << total_rng_used  << "\n";
    std::cout << "  Remaining in buffer     : " << rng.remaining() << "\n\n";

    bool pass1 = (p_wins   >= 0.01 && p_wins   <= 0.99);
    bool pass2 = (p_throws >= 0.01 && p_throws <= 0.99);

    std::cout << "  Wins p-value    : " << std::setprecision(6) << p_wins
              << "  ->  " << (pass1 ? "PASS" : "FAIL") << "\n";
    std::cout << "  Throws p-value  : " << std::setprecision(6) << p_throws
              << "  ->  " << (pass2 ? "PASS" : "FAIL") << "\n\n";

    if (pass1 && pass2)
        std::cout << "  OVERALL: PASS — RNG appears random.\n";
    else
        std::cout << "  OVERALL: FAIL — RNG shows non-random behavior.\n";

    std::cout << "================================================\n";
    return 0;
}
