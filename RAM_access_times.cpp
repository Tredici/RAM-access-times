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

using datatype = unsigned long long;
using datatype_safearrptr = std::unique_ptr<datatype[]>;
constexpr std::size_t datatype_sz = sizeof(datatype);

[[noreturn]] void help(const std::string& program_name) {
    std::cout << "Usage: " << program_name << " <bytes_to_allocate> <ops_count>\n";
    std::cout << "Description: Enter a positive integer to perform <ops_count> operations on memory block of such size.\n";

    std::exit(EXIT_FAILURE); // Terminate the program
}


struct parsed_args
{
    std::size_t byte_sz = 0;
    std::size_t array_sz = 0;
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

        if (args.size() < 3) {
            help(args[0]);
        }

        ans.byte_sz = parse_sizet(args[1], args[0]);
        // round it
        ans.byte_sz = ((ans.byte_sz + datatype_sz - 1) / datatype_sz) * datatype_sz;
        // number of items to allocate
        ans.array_sz = ans.byte_sz / datatype_sz;

        ans.operation_count = parse_sizet(args[2], args[0]);

        return ans;
    }
};



datatype_safearrptr allocate_array(std::size_t N)
{
    datatype_safearrptr ans;

    // Dynamically get system page size
    // cachelines are page aligned
    size_t page_size = sysconf(_SC_PAGESIZE);

    {   // allocating array
        std::cout << "Allocating an array of " << std::format("{:L}", (N * datatype_sz)) << " bytes (" << datatype_sz << "-bytes aligned)..." << std::endl;
        auto start = std::chrono::steady_clock::now();
        ans = datatype_safearrptr(new(std::align_val_t(page_size)) datatype[N]);
        auto end = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
        std::cout << "Time elapsed: " << std::format("{:L}", elapsed.count()) << " ns" << std::endl;
    }
    {   // allocating array
        std::cout << "Zero-ing the array..." << std::endl;
        auto start = std::chrono::steady_clock::now();
        memset(ans.get(), 0, N * datatype_sz);
        auto end = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
        std::cout << "Time elapsed: " << std::format("{:L}", elapsed.count()) << " ns" << std::endl;
    }

    return ans;
}




// results will be 
constexpr std::size_t result_grous = 32;

std::chrono::nanoseconds test_time_sampling_overhead()
{
    auto start = std::chrono::steady_clock::now();
    std::atomic_thread_fence(std::memory_order_acquire); 
    std::atomic_thread_fence(std::memory_order_release);
    auto end = std::chrono::steady_clock::now();

    auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
    return elapsed;
}

std::chrono::nanoseconds estimate_time_sampling_overhead(std::size_t reps = 1024)
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


std::vector<std::size_t> operate(std::size_t N, datatype_safearrptr& safe_ptr, std::size_t ops2perform)
{
    auto ptr = safe_ptr.get();

    auto estimated_timing_bias = estimate_time_sampling_overhead();
    std::cout << "Estimated time sampling overhead of: " << std::format("{:L}", estimated_timing_bias.count()) << " ns" << std::endl;

    std::vector<std::size_t> log2time_counter(result_grous);

    // to generate sparse random numbers rely on Mersenne Twister engine
    std::random_device rd;
    // initialize engine
    std::mt19937 gen(rd());
    // avoid %N issue
    std::uniform_int_distribution<> distr(0, N-1);

    auto test_start = std::chrono::steady_clock::now();

    for (decltype(ops2perform) i{}; i != ops2perform; ++i)
    {
        // get index of array item to modify
        auto idx2modify = distr(gen);

        auto start = std::chrono::steady_clock::now();

        // prevent reordering
        std::atomic_thread_fence(std::memory_order_acquire); 
        {
            // perform update operation on memory - just to trigger
            // memory (either cache or RAM) accesses
            ++ptr[idx2modify];
        }
        // ensure operation has been completed
        std::atomic_thread_fence(std::memory_order_release);

        auto end = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);

        if (elapsed >= estimated_timing_bias)
        {
            // try to remove sampling overhead from value
            elapsed -= estimated_timing_bias;
        }
        else
        {
            // type-independent zero-ing method
            elapsed -= elapsed;
        }

        auto val = elapsed.count();
        // should never overflow - otherwise measures are broken
        auto log2time_idx_floor = std::bit_width((unsigned)val);
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
        std::cerr << "Failed to lock memory: " << std::strerror(errno) << std::endl;
        // Handle error (e.g., exit or throw exception)
    }

    std::unique_ptr<datatype[]> test_memory = allocate_array(args.array_sz);

    // run all operation
    auto test_times = operate(args.array_sz, test_memory, args.operation_count);

    std::cout << "Print memory access time distribution" << std::endl;
    std::cout << "Interval are [a, b), with nanosecond accuracy and logarithmic scaled-bins" << std::endl;

    // print result
    for (auto idx : std::views::iota((decltype(test_times.size()))0, test_times.size()))
    {
        // upper is given by integer
        auto upper = 1ULL << idx;
        // lower is half the upper
        auto lower = upper >> 1;
        //std::cout << '[' << std::setw(10) << lower << ", " << std::setw(10) << upper << ")\t" << std::setw(10) << test_times[idx] << std::endl;
        std::cout << '[' << std::setw(10) << lower << ", " << std::setw(10) << upper << ")\t" << std::setw(12) << std::format("{:L}", test_times[idx]) << std::endl;
    }

    return 0;
}


