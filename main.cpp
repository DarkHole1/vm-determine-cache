#include <iostream>
#include <chrono>
#include <random>
#include <algorithm>
using namespace std;

#define ITERATIONS 50000000

uint64_t microbench(int H, int S)
{
    // Sanity check
    if (H < static_cast<int>(sizeof(void *)))
    {
        cerr << "Stride H must be >= sizeof(void*)" << endl;
        exit(1);
    }

    // Initialization
    char *raw = static_cast<char *>(aligned_alloc(4096, H * S));
    if (raw == nullptr)
    {
        cerr << "Failed to allocate memory for H = " << H << ", S = " << S << endl;
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
    for (int i = 0; i < ITERATIONS; i++)
    {
        ptr = *reinterpret_cast<void **>(ptr);
    }

    auto t2 = chrono::steady_clock::now();
    auto delta = t2 - t1;

    // Prevent optimizations
    if (ptr == nullptr)
    {
        cerr << "Sanity check failed" << endl;
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

int find_jump_spot(uint64_t H, uint64_t S_min, uint64_t S_max)
{
    uint64_t prev_time = microbench(H, S_min);

    for(uint64_t S = S_min + 1; S < S_max; S++) {
        uint64_t curr_time = microbench(H, S);

        if(static_cast<double>(curr_time) / static_cast<double>(prev_time) >= JUMP_THRESHOLD) {
            return S;
        }
    }

    return S_max;
}

int detect_cache_line_size(uint64_t H_detected)
{
    // cout << "cache line" << endl;
    int prev_pattern = 0;

    for (uint64_t H = 16; H <= H_detected; H *= 2)
    {
        uint64_t L = H / 2;
        int S_base = find_jump_spot(H, 1, 2048);

        int t_base = microbench(H + L, 1);
        int t_s_base = microbench(H + L, S_base);

        uint64_t pattern;
        if (static_cast<double>(t_s_base) / static_cast<double>(t_base) < 1.1) {
            // We didn't observed jump before S_base => S_base < S_mod
            cout << "Bailout\n";
            pattern = 2;
        } else {
            int S_mod = find_jump_spot(H + L, 1, 2048);
    
            cout << "H=" << H << ", L=" << L << ", S_mod=" << S_mod << ", S_base=" << S_base << endl;
    
            if (static_cast<double>(S_mod) * 1.1 < static_cast<double>(S_base))
            {
                pattern = 1;
            }
            else if (static_cast<double>(S_mod) > static_cast<double>(S_base) * 1.1)
            {
                pattern = 2;
            }
            else
            {
                pattern = 3;
                continue;
            }
        }


        if (prev_pattern == 1 && pattern == 2)
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

void detect_capacity_associativity_line(int Z, int N, int M)
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
            uint64_t curr_time = microbench(H, S);

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
        return;
    }

    int line_size = detect_cache_line_size(stable_H);
    int capacity_bytes = associativity * stable_H;

    std::cout << "=== L1 Data Cache Detection ===\n";
    std::cout << "Associativity: " << associativity << "\n";
    std::cout << "Stride at stability: " << stable_H << " bytes\n";
    std::cout << "Estimated capacity: " << capacity_bytes << " bytes ("
              << (capacity_bytes / 1024) << " KB)\n";
    std::cout << "Line size: " << line_size << " bytes\n";
}

int main()
{
    detect_capacity_associativity_line(10 * 1024 * 1024, 128, 1024 * 1024 * 1024);
    return 0;
}