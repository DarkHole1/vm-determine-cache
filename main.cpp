#include <iostream>
#include <chrono>
#include <random>
#include <algorithm>
using namespace std;

uint64_t microbench(int H, int S, const int iterations)
{
    // Sanity check
    if (H < static_cast<int>(sizeof(void *)))
    {
        cerr << "Stride H must be >= sizeof(void*)\n";
        exit(1);
    }

    // Initialization
    char *raw = static_cast<char *>(aligned_alloc(4096, H * S));
    if (raw == nullptr)
    {
        cerr << "Failed to allocate memory for H = " << H << ", S = " << S << "\n";
        exit(1);
    }

    // Random pointer chain
    vector<void *> blocks(S);
    for (int i = 0; i < S; ++i)
    {
        blocks[i] = raw + i * H;
    }

    random_device rd;
    mt19937 g(rd());
    shuffle(blocks.begin(), blocks.end(), g);

    for (int i = 0; i < S; ++i)
    {
        void **cur = reinterpret_cast<void **>(blocks[i]);
        void *next = blocks[(i + 1) % S];
        *cur = next;
    }

    // Microbench
    auto t1 = chrono::steady_clock::now();

    void *ptr = blocks[0];
    for (int i = 0; i < iterations; i++)
    {
        ptr = *reinterpret_cast<void **>(ptr);
    }

    auto t2 = chrono::steady_clock::now();
    auto delta = t2 - t1;

    // Prevent optimizations
    if (ptr == nullptr)
    {
        cerr << "Sanity check failed\n";
        exit(1);
    }

    // Finalization
    free(raw);
    return delta.count();
}

const double JUMP_THRESHOLD = 1.3;

struct JumpPoint
{
    int H;
    int S;
};

std::vector<JumpPoint> jump_history;

bool isJump(uint64_t prev_time, uint64_t curr_time)
{
    if (prev_time == 0)
        return false;
    return static_cast<double>(curr_time) / static_cast<double>(prev_time) >= JUMP_THRESHOLD;
}

int getLastJumpSpotForStride(int H)
{
    for (auto it = jump_history.rbegin(); it != jump_history.rend(); ++it)
    {
        if (it->H == H)
            return it->S;
    }
    return -1;
}

bool isMovement(int H)
{
    if (H <= 16)
        return true;

    int prev_H = H / 2;
    int curr_jump = getLastJumpSpotForStride(H);
    int prev_jump = getLastJumpSpotForStride(prev_H);

    if (curr_jump == -1 || prev_jump == -1)
        return true;

    double expected = prev_jump / 2.0;
    return std::abs(curr_jump - expected) <= 2;
}

int find_jump_spot(uint64_t H, uint64_t S_max, uint64_t expected, int iterations)
{
    uint64_t prev_time = microbench(H, 1, iterations);
    cout << expected << "\n";

    // for (uint64_t S = max(2UL, expected - 350); S < min(S_max, expected + 350); S++)
    for (uint64_t S = 2; S < S_max; S++)
    {
        uint64_t curr_time = microbench(H, S, iterations);

        if (static_cast<double>(curr_time) / static_cast<double>(prev_time) >= JUMP_THRESHOLD)
        {
            return S;
        }
    }

    return S_max;
}

int detect_cache_line_size(uint64_t H_detected, uint64_t size, int iterations)
{
    // cout << "cache line" << "\n";
    int prev_pattern = 0;

    for (uint64_t H = 16; H <= H_detected; H *= 2)
    {
        uint64_t L = 8;
        int S_base = find_jump_spot(H, 4096, size/H, iterations * 10);
        int S_mod = find_jump_spot(H + L, 4096, size/H, iterations * 10);

        cout << "H=" << H << ", L=" << L << ", S_mod=" << S_mod << ", S_base=" << S_base << "\n";

        uint64_t pattern;
        if (S_mod < S_base - 50)
        {
            pattern = 1;
        }
        else if (S_mod > S_base + 50)
        {
            pattern = 2;
        }
        else
        {
            pattern = 3;
        }


        if (prev_pattern == 1 && pattern == 2)
        {
            return H;
        }

        if (prev_pattern == 3 && pattern == 2)
        {
            return H;
        }

        if (prev_pattern == 3 && pattern == 2)
        {
            return H / 2;
        }

        prev_pattern = pattern;
    }

    return -1;
}

struct Result
{
    uint64_t associativity, stable_H, capacity_bytes;
    int line_size;
};

Result detect_capacity_associativity_line(int Z, int N, int M, int iterations)
{
    jump_history.clear();
    int H = 16;

    int stable_H = -1;
    int associativity = -1;

    while (H <= M && H * N <= Z)
    {
        cout << "H=" << H << "\n";
        uint64_t prev_time = 0;
        int jump_spot = -1;

        for (int S = 1; S <= N; ++S)
        {
            uint64_t curr_time = microbench(H, S, iterations);

            if (isJump(prev_time, curr_time))
            {
                jump_spot = S;
                jump_history.push_back({H, S});
                break;
            }
            prev_time = curr_time;
        }

        if (jump_spot == -1)
        {
            H *= 2;
            continue;
        }

        if (!isMovement(H))
        {
            stable_H = H / 2;
            associativity = jump_spot - 1;
            break;
        }

        H *= 2;
    }

    if (associativity == -1)
    {
        std::cerr << "Failed to detect L1 cache associativity.\n";
        return {0};
    }

    int capacity_bytes = associativity * stable_H;
    int line_size = detect_cache_line_size(stable_H, capacity_bytes, iterations);

    Result res = {0};
    res.associativity = associativity;
    res.stable_H = stable_H;
    res.capacity_bytes = capacity_bytes;
    res.line_size = line_size;

    return res;
}

int detect_stable_iterations_count()
{
    int res = 1000000;
    do
    {
        uint64_t min, max;
        min = max = microbench(64, 10, res);
        for (int i = 1; i < 100; i++)
        {
            uint64_t time = microbench(128, 128, res);
            if (time < min)
                min = time;
            if (time > max)
                max = time;
        }
        // Max difference 5%
        // cout << max << "/" << min << " " << (max * 1.0 / min) << "\n";
        if (min * 1.05 > max)
        {
            return res;
        }
        res *= 2;
    } while (true);
}

int main()
{
    int iterations = detect_stable_iterations_count();
    cout << "Stable iterations count: " << iterations << "\n";
    do {
        Result res1 = detect_capacity_associativity_line(10 * 1024 * 1024, 128, 1024 * 1024 * 1024, iterations);
        cout << "=== L1 Data Cache Detection ===\n";
        cout << "Associativity: " << res1.associativity << "\n";
        cout << "Stride at stability: " << res1.stable_H << " bytes\n";
        cout << "Estimated capacity: " << res1.capacity_bytes << " bytes (" << (res1.capacity_bytes / 1024) << " KB)\n";
        cout << "Line size: " << res1.line_size << " bytes\n";

        Result res2 = detect_capacity_associativity_line(10 * 1024 * 1024, 128, 1024 * 1024 * 1024, iterations);
        cout << "=== L1 Data Cache Sanity Check ===\n";
        cout << "Associativity: " << res2.associativity << "\n";
        cout << "Stride at stability: " << res2.stable_H << " bytes\n";
        cout << "Estimated capacity: " << res2.capacity_bytes << " bytes (" << (res2.capacity_bytes / 1024) << " KB)\n";
        cout << "Line size: " << res2.line_size << " bytes\n";

        if (res1.associativity != res2.associativity || res1.capacity_bytes != res2.capacity_bytes || res1.line_size != res2.line_size || res1.stable_H != res2.stable_H) {
            cout << "!!! Sanity Check Failed !!!\n\n";
            iterations *= 2;
        } else {
            return 0;
        }
    } while(true);
}