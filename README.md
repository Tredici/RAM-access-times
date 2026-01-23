# RAM-access-times
Measure memory access times and group samples into logarithmic histogram

## key features
- pointer chasing: randomized linked list traversal pattern to avoid CPU prefetchign and pipelining 
- hardware counters: use low level ASM instructions depending on architecture (Apple Silicon - Intel/AMD x86/64) eliminating syscall overhead
- extended compatibility to MacOS
