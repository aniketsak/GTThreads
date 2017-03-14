cmake_minimum_required(VERSION 3.6)
project(gtthreads)

set(CMAKE_CXX_STANDARD 11)

set(SOURCE_FILES
        src/gt_bitops.h
        src/gt_include.h
        src/gt_kthread.c
        src/gt_kthread.h
        src/gt_matrix.c
        src/gt_pq.c
        src/gt_pq.h
        src/gt_signal.c
        src/gt_signal.h
        src/gt_spinlock.c
        src/gt_spinlock.h
        src/gt_tailq.h
        src/gt_uthread.c
        src/gt_uthread.h
        Makefile
        README.txt)

include_directories(.)
include_directories(bin)
include_directories(src)

add_executable(gtthreads ${SOURCE_FILES})