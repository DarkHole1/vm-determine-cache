#include <iostream>
#include <chrono>
#include <random>
#include <algorithm>
using namespace std;

#define ITERATIONS 2000000

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

void detect(int Z, int N, int M)
{
    cout << "H,S,t" << endl;
    int H = 16;
    while (H * N < Z)
    {
        int S = 1;
        while (S < N)
        {
            uint64_t current_time = microbench(H, S);

            // TODO

            S += 1;
        }

        H *= 2;
    }

    // TODO
}

int main()
{
    detect(10 * 1024 * 1024, 128, 1024);
    return 0;
}