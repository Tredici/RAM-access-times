CXX := g++
CXXFLAGS := -std=c++20 -O3 -Wall -Wextra -Wpedantic


all: RAM_access_times APP_noise



RAM_access_times: RAM_access_times.cpp

APP_noise: APP_noise.cpp



clean:
	rm -f RAM_access_times APP_noise
