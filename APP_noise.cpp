#include <bit>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <format>
#include <iomanip>
#include <iostream>
#include <locale>
#include <memory>
#include <random>
#include <ranges>
#include <stdexcept> // For exception handling
#include <string>
#include <string>    // For std::stoi
#include <thread>
#include <vector>

#include <stdlib.h>
#include <unistd.h>


#include <sys/mman.h>

// [NEW] Hardware counter helper
inline uint64_t get_cycles() {
#if defined(__aarch64__)
    uint64_t val; asm volatile("mrs %0, cntvct_el0" : "=r"(val)); return val;
#elif defined(__x86_64__) || defined(_M_X64)
    unsigned int lo, hi; asm volatile ("rdtscp" : "=a" (lo), "=d" (hi) :: "rcx"); return ((uint64_t)hi << 32) | lo;
#else
    return 0;
#endif
}

using datatype = unsigned long long;
using datatype_safearrptr = std::unique_ptr<datatype[]>;
constexpr std::size_t datatype_sz = sizeof(datatype);

[[noreturn]] void help(const std::string& program_name) {
    std::cout << "Usage: " << program_name << " <ops_count>\n";
    std::cout << "Description: Enter a positive integer to perform <ops_count> operations.\n";

    std::exit(EXIT_FAILURE); // Terminate the program
}


struct parsed_args
{
    std::size_t operation_count = 0;


    parsed_args() = default;


    static std::size_t parse_sizet(const std::string& str, const std::string& prog)
    {
        std::size_t ans = 0;
        try {
            // 2. Convert the first argument (argv[1]) to an int
            ans = std::stoul(str);
            // round
            if (ans == 0)
            {
                std::cerr << "Error: " << ans << " must be a positive integer." << std::endl;
            }
        }
        catch (const std::invalid_argument& e) {
            std::cerr << "Error: '" << str << "' is not a valid integer." << std::endl;
            help(prog);
        }
        catch (const std::out_of_range& e) {
            std::cerr << "Error: '" << str << "' is out of the integer range." << std::endl;
            help(prog);
        }
        return ans;
    }

    static parsed_args parse(int argc, char* argv[])
    {
        parsed_args ans;

        // funny way discovered thanks to Google
        std::vector<std::string> args(argv, argv + argc);

        if (args.size() < 2) {
            help(args[0]);
        }

        ans.operation_count = parse_sizet(args[1], args[0]);

        return ans;
    }
};




// results will be 
constexpr std::size_t result_grous = 64; // Increased to 64 for cycle count safety

// Return uint64_t instead of chrono
uint64_t test_time_sampling_overhead()
{
    auto start = get_cycles(); // use hardware counter
    // no barriers this time
    //std::atomic_thread_fence(std::memory_order_acquire); 
    //std::atomic_thread_fence(std::memory_order_release);
    auto end = get_cycles();   // use hardware counter

    return end - start;
}

// [MOD] Return uint64_t
uint64_t estimate_time_sampling_overhead(std::size_t reps = 1024)
{
    if (!reps)
    {
        throw std::invalid_argument("estimate_time_sampling_overhead: reps must be greater than 0");
    }
    auto overhead = test_time_sampling_overhead();
    for (decltype(reps) i{1}; i != reps; ++i)
    {
        auto test = test_time_sampling_overhead();
        overhead = std::min(overhead, test);
    }
    return overhead;
}


std::vector<std::size_t> operate(std::size_t ops2perform)
{
    auto estimated_timing_bias = estimate_time_sampling_overhead();
    std::cout << "Estimated time sampling overhead of: " << std::format("{:L}", estimated_timing_bias) << " cycles" << std::endl;

    std::vector<std::size_t> log2time_counter(result_grous);


    auto test_start = std::chrono::steady_clock::now();

    for (decltype(ops2perform) i{}; i != ops2perform; ++i)
    {

        auto start = get_cycles(); // [MOD]
        // try to measure just time between consecutive OPs
        // all should be accelerated by the cache
        auto end = get_cycles();   // [MOD]
        auto elapsed = end - start; // [MOD]

        auto val = elapsed;
        // should never overflow - otherwise measures are broken
        auto log2time_idx_floor = std::bit_width((unsigned long long)val); // [MOD] safe cast
        // counter unexpected overflow
        if (std::size_t(log2time_idx_floor) >= std::size_t(log2time_counter.size()))
        {
            // last item for all time measurements
            log2time_idx_floor = log2time_counter.size() - 1;
        }

        //std::cout << "val = " << val << "\tstd::bit_width((unsigned)val) => " << log2time_idx_floor << std::endl;
        // increment count of operations in that range
        ++log2time_counter[log2time_idx_floor];
    }

    auto test_end = std::chrono::steady_clock::now();
    auto test_elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(test_end - test_start);
    std::cout << "Time elapsed for whole test: " << std::format("{:L}", test_elapsed.count()) << " ns" << std::endl;

    return log2time_counter;
}

int main(int argc, char* argv[]) {


    parsed_args args = parsed_args::parse(argc, argv);

    // Set a locale that uses thousands separators (e.g., US English)
    std::string preferred_locale = "en_US.UTF-8";
    try
    {
        std::cout << "Try setting locale to " << preferred_locale << std::endl;
        std::locale::global(std::locale(preferred_locale));
        std::cout << "Locale set!" << std::endl;
    }
    catch(const std::exception& e)
    {
        std::cerr << "Error setting locale to " << preferred_locale << "\n\treason: " << e.what() << '\n';
        std::cerr << "Using default locale\n";
    }
    


    std::cout << "Enable Unix memory locking" << std::endl;

    if (mlockall(MCL_CURRENT | MCL_FUTURE) != 0) {
        std::cerr << "Warning: Failed to lock memory (macOS?): " << std::strerror(errno) << std::endl; // [MOD] Non-fatal
        // Handle error (e.g., exit or throw exception)
    }

    // run all operation
    auto test_times = operate(args.operation_count);

    std::cout << "Print operation time distribution (Cycles)" << std::endl;
    std::cout << "Interval are [a, b), with cycle accuracy and logarithmic scaled-bins" << std::endl;

    // print result
    for (auto idx : std::views::iota((decltype(test_times.size()))0, test_times.size()))
    {
        if (test_times[idx] == 0) continue; // [MOD] skip empty
        // upper is given by integer
        auto upper = 1ULL << idx;
        // lower is half the upper
        auto lower = upper >> 1;
        //std::cout << '[' << std::setw(10) << lower << ", " << std::setw(10) << upper << ")\t" << std::setw(10) << test_times[idx] << std::endl;
        std::cout << '[' << std::setw(10) << lower << ", " << std::setw(10) << upper << ")\t" << std::setw(12) << std::format("{:L}", test_times[idx]) << std::endl;
    }

    return 0;
}
