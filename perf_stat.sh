#!/bin/bash

sudo perf stat -dd -B -I 1000 -p $(ps -C RAM_access_times -o pid=) |& grep -v '<not '