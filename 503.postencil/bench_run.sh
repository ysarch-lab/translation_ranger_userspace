#!/bin/bash

#export OMP_NUM_THREADS=${NTHREADS}

BENCH_NAME="postencil"

if [[ "x${BENCH_SIZE}" == "x4GB"  ]]; then
BENCH_RUN="./stencil_exe_base.compsys -- 2048 2048 125 50" # 7.6GB, 3 mins
elif [[ "x${BENCH_SIZE}" == "x8GB"  ]]; then
BENCH_RUN="./stencil_exe_base.compsys -- 2048 2048 250 50" # 7.6GB, 3 mins
elif [[ "x${BENCH_SIZE}" == "x16GB"  ]]; then
BENCH_RUN="./stencil_exe_base.compsys -- 2048 2048 500 30" # 15.4GB, 3 mins
elif [[ "x${BENCH_SIZE}" == "x30GB"  ]]; then
BENCH_RUN="./stencil_exe_base.compsys -- 2048 2048 930 30" # 15.4GB, 3 mins
elif [[ "x${BENCH_SIZE}" == "x30GB_SMALL"  ]]; then
BENCH_RUN="./stencil_exe_base.compsys -- 2048 2048 930 30" # 15.4GB, 3 mins
elif [[ "x${BENCH_SIZE}" == "x30GB_LONG"  ]]; then
BENCH_RUN="./stencil_exe_base.compsys -- 2048 2048 930 60" # 15.4GB, 3 mins
elif [[ "x${BENCH_SIZE}" == "x32GB"  ]]; then
BENCH_RUN="./stencil_exe_base.compsys -- 2048 2048 1000 30" # 15.4GB, 3 mins
elif [[ "x${BENCH_SIZE}" == "x40GB"  ]]; then
BENCH_RUN="./stencil_exe_base.compsys -- 2048 2048 1200 50" # 37.4GB, 3 mins
elif [[ "x${BENCH_SIZE}" == "x16GB_SB"  ]]; then
BENCH_RUN="./stencil_exe_base.compsys -- 2048 2048 500 30" # 15.4GB, 3 mins
elif [[ "x${BENCH_SIZE}" == "x128GB"  ]]; then
BENCH_RUN="./stencil_exe_base.compsys -- 4096 4096 970 30" # 15.4GB, 3 mins
elif [[ "x${BENCH_SIZE}" == "x128GB_LONG"  ]]; then
BENCH_RUN="./stencil_exe_base.compsys -- 4096 4096 970 30" # 15.4GB, 3 mins
else
BENCH_RUN="./stencil_exe_base.compsys -- 2048 2048 1200 50" # 37.4GB, 3 mins
fi

#${BENCH_RUN} &>/dev/null
#echo ${CPUID} ${BENCH_NAME}
