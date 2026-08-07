// Checks AsciiRenderer's nearest-coverage breakpoint table (the step-function
// lookup that replaced an O(nChars) scan per cell) against that scan, over
// random charsets — including ones with duplicate coverages and probes landing
// exactly on a breakpoint, which is where the first version got it wrong: the
// midpoints are rounded to float, so one can sit on the wrong side of a value
// that is genuinely closer to the other bucket.
//
// Pure float/int logic, no Qt, no framework. Run it after touching
// chooseGlyph / the nearEdge-nearIdx tables in AsciiRenderer.cpp:
//
//   g++ -std=c++17 -O2 -o nearest_glyph_check tests/nearest_glyph_check.cpp
//   ./nearest_glyph_check          # exits non-zero and prints the mismatches
//
// KEEP IN SYNC with AsciiRenderer::render's copy of the same logic.
#include <algorithm>
#include <cstdio>
#include <random>
#include <vector>

static int bruteForce(const std::vector<float>& cov, float darkness)
{
    int idx = 0; float best = 2.0f;
    for (int i = 0; i < int(cov.size()); ++i) {
        const float d = std::fabs(cov[size_t(i)] - darkness);
        if (d < best) { best = d; idx = i; }
    }
    return idx;
}

int main()
{
    std::mt19937 rng(12345);
    long long checked = 0, mismatches = 0;

    for (int trial = 0; trial < 400; ++trial) {
        // Mix of sizes, plus deliberate duplicate coverages (a real charset
        // easily has two glyphs measuring the same ink) and coarse quantised
        // values so exact ties actually occur.
        const int n = 2 + int(rng() % 127);
        const size_t nn = size_t(n);
        std::vector<float> cov(nn);
        const int buckets = (trial % 3 == 0) ? 5 : 1000;
        for (int i = 0; i < n; ++i)
            cov[size_t(i)] = float(rng() % (buckets + 1)) / float(buckets);

        // --- the code under test (mirrors AsciiRenderer::render) ---
        std::vector<int> byCoverage(nn);
        for (int i = 0; i < n; ++i) byCoverage[size_t(i)] = i;
        std::sort(byCoverage.begin(), byCoverage.end(),
                  [&](int a, int b) { return cov[size_t(a)] < cov[size_t(b)]; });

        std::vector<float> nearEdge;
        std::vector<int>   nearIdx;
        {
            std::vector<int> rep;
            for (int i : byCoverage) {
                if (!rep.empty() && cov[size_t(rep.back())] == cov[size_t(i)]) {
                    if (i < rep.back()) rep.back() = i;
                    continue;
                }
                rep.push_back(i);
            }
            nearIdx.push_back(rep[0]);
            for (size_t k = 1; k < rep.size(); ++k) {
                nearEdge.push_back((cov[size_t(rep[k - 1])] + cov[size_t(rep[k])]) * 0.5f);
                nearIdx.push_back(rep[k]);
            }
        }
        auto nearestIdx = [&](float darkness) -> int {
            const size_t cnt = nearIdx.size();
            const size_t k = size_t(std::upper_bound(nearEdge.begin(), nearEdge.end(), darkness)
                                    - nearEdge.begin());
            const size_t lo = (k > 0) ? k - 1 : 0;
            const size_t hi = (k + 1 < cnt) ? k + 1 : cnt - 1;
            int   best  = nearIdx[lo];
            float bestD = std::fabs(cov[size_t(best)] - darkness);
            for (size_t c = lo + 1; c <= hi; ++c) {
                const int i = nearIdx[c];
                const float d = std::fabs(cov[size_t(i)] - darkness);
                if (d < bestD || (d == bestD && i < best)) { best = i; bestD = d; }
            }
            return best;
        };
        // --- end code under test ---

        // Probe every breakpoint exactly (the tie case), plus either side of
        // it, plus a sweep and the endpoints.
        std::vector<float> probes = { 0.0f, 1.0f, -0.5f, 1.5f };
        for (float e : nearEdge) {
            probes.push_back(e);
            probes.push_back(std::nextafter(e, -1.0f));
            probes.push_back(std::nextafter(e, 2.0f));
        }
        for (int s = 0; s <= 200; ++s) probes.push_back(float(s) / 200.0f);
        for (float c : cov) probes.push_back(c);

        for (float d : probes) {
            const int want = bruteForce(cov, d);
            const int got  = nearestIdx(d);
            ++checked;
            // Different index is only OK if it picks identical ink coverage
            // AND the scan's lowest-index tie rule still holds.
            if (got != want) {
                ++mismatches;
                if (mismatches <= 5)
                    std::printf("MISMATCH n=%d d=%.9g want=%d(cov %.9g) got=%d(cov %.9g)\n",
                                n, d, want, cov[size_t(want)], got, cov[size_t(got)]);
            }
        }
    }

    std::printf("checked=%lld mismatches=%lld\n", checked, mismatches);
    return mismatches == 0 ? 0 : 1;
}
