#include <iostream>
#include <chrono>
#include <random>
#include <algorithm>
using namespace std;

#define ITERATIONS 10000000

uint64_t microbench(int H, int S)
{
    // Sanity check
    if (H < static_cast<int>(sizeof(void *)))
    {
        cerr << "Stride H must be >= sizeof(void*)" << endl;
        exit(1);
    }

    // Initialization
    char *raw = static_cast<char *>(aligned_alloc(H, H * S));
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

void detect_capacity_associativity(int Z, int N, int M)
{
    jump_history.clear();
    int H = 16;

    int stable_H = -1;
    int associativity = -1;

    while (H <= M && H * N <= Z)
    {
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

    int capacity_bytes = associativity * stable_H;

    std::cout << "=== L1 Data Cache Detection ===\n";
    std::cout << "Associativity: " << associativity << "\n";
    std::cout << "Stride at stability: " << stable_H << " bytes\n";
    std::cout << "Estimated capacity: " << capacity_bytes << " bytes ("
              << (capacity_bytes / 1024) << " KB)\n";
}

int main()
{
    detect_capacity_associativity(10 * 1024 * 1024, 128, 1024 * 1024 * 1024);
    return 0;
}