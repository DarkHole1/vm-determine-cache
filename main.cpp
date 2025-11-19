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

uint64_t detect_jump(uint64_t H, int iterations)
{
    // cout << "H=" << H << "\n";
    uint64_t zero = microbench(H, 1, iterations);
    uint64_t jump = 0;
    for (uint64_t S = 1; S <= 1024; S *= 2)
    {
        uint64_t SL = S | (S / 2);
        uint64_t cur = microbench(H, SL, iterations);
        double diff = (cur / (double)(zero));
        if (diff > 1.3 && jump == 0)
        {
            jump = S;
            return jump;
        }
        // cout << "  " << "S=" << S << "  S+=" << SL << "  Tr=" << diff << "\n";
    }
    return 0;
}

uint64_t precise_detect_jump(uint64_t H, int iterations)
{
    // cout << "H=" << H << "\n";
    uint64_t zero = microbench(H, 1, iterations);
    uint64_t jump = 0;
    for (uint64_t S = 1; S <= 65; S++)
    {
        uint64_t cur = microbench(H, S, iterations);
        double diff = (cur / (double)(zero));
        if (diff > 1.3 && jump == 0)
        {
            jump = S - 1;
            return jump;
        }
        // cout << "  " << "S=" << S << "  S+=" << SL << "  Tr=" << diff << "\n";
    }
    return 0;
}

pair<uint64_t, uint64_t> detect_associativity_size(int iterations)
{
    uint64_t prev_jump = 0;
    uint64_t prev_H = 16;

    for (uint64_t H = 16; H <= 1024 * 1024; H *= 2)
    {
        uint64_t jump = precise_detect_jump(H, iterations);
        if (prev_jump == jump && jump != 0)
        {
            jump = precise_detect_jump(H, iterations * 2);
            // Double recheck
            if (prev_jump == jump && jump != 0)
            {
                return pair(prev_jump, prev_jump * prev_H);
            }
        }
        prev_jump = jump;
        prev_H = H;
    }

    return pair(0, 0);
}

bool check_line_size(uint64_t H, uint64_t jump, uint64_t iterations)
{
    const uint64_t HL = H | (H / 2);
    uint64_t zero = microbench(H, 1, iterations * 2);
    uint64_t one = microbench(HL, 1, iterations * 2);
    uint64_t jump_time = microbench(H, jump, iterations * 2);
    uint64_t two = microbench(HL, jump, iterations * 2);
    return two < jump_time * 1.1L && two > jump_time * 0.9L;
}

uint64_t detect_line_size(uint64_t size, uint64_t iterations)
{
    for (uint64_t H = 16; H <= 512; H *= 2)
    {
        // cout << "H=" << H << "\n";
        uint64_t zero = microbench(H, 1, iterations);
        uint64_t jump = 0;
        uint64_t jump_time = 0;
        for (uint64_t S = (size / H); S < 4096; S++)
        {
            uint64_t cur = microbench(H, S, iterations);
            double diff = (cur / (double)(zero));
            // cout << "  " << "S=" << S << "  Tr=" << diff << "\n";
            if (diff > 1.3)
            {
                jump_time = cur;
                jump = S;
                break;
            }
        }

        if (jump == 0)
        {
            // Jump point to far away
            continue;
        }

        const uint64_t HL = H + (H / 2);
        // cout << "HL=" << HL << "\n";
        uint64_t one = microbench(HL, 1, iterations);
        uint64_t two = microbench(HL, jump, iterations);

        if (one * (1.1L) > two)
        {
            // cout << "jump2 > jump1\n";
            // Overjump
            const uint64_t line_size = H / 2;
            if (check_line_size(line_size, jump, iterations))
            {
                return line_size;
            }
            // Failed check, start again
            H = 8;
            continue;
        }
        if (two < jump_time * 1.1L && two > jump_time * 0.9L)
        {
            // cout << "jump2 ~ jump1\n";
            const uint64_t line_size = H;
            if (check_line_size(line_size, jump, iterations))
            {
                return line_size;
            }
            // Failed check, start again
            H = 8;
            continue;
        }
    }
    return 0;
}

tuple<uint64_t, uint64_t, uint64_t> detect(int iterations)
{
    auto res = detect_associativity_size(iterations);
    uint64_t assoc = res.first;
    uint64_t size = res.second;
    uint64_t line_size = detect_line_size(size, iterations);

    return tuple(assoc, size, line_size);
}

int main()
{
    const int iterations = 20000000;

    // Warmup
    for (int i = 0; i < 10; i++)
    {
        const uint64_t H = 2 << 8;
        const uint64_t S = 2 << 8;
        microbench(H, S, iterations);
    }

    for(uint64_t tries = 1;; tries++)
    {
        tuple<uint64_t, uint64_t, uint64_t> res[3];
        for (int i = 0; i < 3; i++)
        {
            res[i] = detect(iterations);
        }

        // Choose two of three. If all three different, start again
        tuple<uint64_t, uint64_t, uint64_t> final_res;
        if (get<0>(res[0]) == get<0>(res[1]) || get<0>(res[1]) == get<0>(res[2]))
        {
            get<0>(final_res) = get<0>(res[1]);
        }
        else if (get<0>(res[0]) == get<0>(res[2]))
        {
            get<0>(final_res) = get<0>(res[0]);
        }
        else
        {
            continue;
        }

        if (get<1>(res[0]) == get<1>(res[1]) || get<1>(res[1]) == get<1>(res[2]))
        {
            get<1>(final_res) = get<1>(res[1]);
        }
        else if (get<1>(res[0]) == get<1>(res[2]))
        {
            get<1>(final_res) = get<1>(res[0]);
        }
        else
        {
            continue;
        }

        if (get<2>(res[0]) == get<2>(res[1]) || get<2>(res[1]) == get<2>(res[2]))
        {
            get<2>(final_res) = get<2>(res[1]);
        }
        else if (get<2>(res[0]) == get<2>(res[2]))
        {
            get<2>(final_res) = get<2>(res[0]);
        }
        else
        {
            continue;
        }

        uint64_t assoc = get<0>(final_res);
        uint64_t size = get<1>(final_res);
        uint64_t line_size = get<2>(final_res);

        cout << "{\n";
        cout << "  \"associativity\": " << assoc << ",\n";
        cout << "  \"size\": " << size << ",\n";
        cout << "  \"size_KB\": " << (size / 1024) << ",\n";
        cout << "  \"line_size\": " << line_size << ",\n";
        cout << "  \"tries\": " << tries << "\n";
        cout << "}\n";
        return 0;
    }
}