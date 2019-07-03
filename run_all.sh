#!/bin/bash


#CONFIG_LIST="no_compact_no_daemon compact_default compact_freq defrag_and_thp_compact"
CONFIG_LIST="no_compact_no_daemon_defrag"
#CONFIG_LIST="compact_default compact_freq"
#CONFIG_LIST="compact_freq"
#CONFIG_LIST="no_compact_no_daemon_defrag"
#CONFIG_LIST="compact_default compact_freq"

NUM_ITER=1

#BENCH_LIST="503.postencil 551.ppalm 553.pclvrleaf 555.pseismic 556.psp 559.pmniGhost 560.pilbdc 563.pswim 570.pbt graph500-omp"
#BENCH_LIST="503.postencil 551.ppalm 553.pclvrleaf 555.pseismic 556.psp 559.pmniGhost 560.pilbdc 563.pswim 570.pbt graph500-omp gups"

#BENCH_LIST="bc-kron bc-road bc-twitter bc-urand bfs-urand cc-twitter pr-twitter sssp-web tc-urand"

BENCH_LIST="503.postencil"


#THREAD_NUM_LIST="2 4 8 16"
THREAD_NUM_LIST="2"

#WORKLOAD_SIZE="8GB 16GB 32GB 40GB"
#WORKLOAD_SIZE="8GB 16GB 32GB 128GB"
#WORKLOAD_SIZE="8GB 16GB 32GB"
#WORKLOAD_SIZE="8GB 16GB 32GB 64GB 128GB"
WORKLOAD_SIZE="8GB"


#THP_SIZE_LIST="4kb 2mb 1gb"
THP_SIZE_LIST="2mb"

#DEFRAG_FREQ_FACTOR_LIST="$(seq 1 10) 20"
DEFRAG_FREQ_FACTOR_LIST="1"

export PREFER_MEM_MODE=yes
#export PREFER_MEM_MODE=no

export STATS_PERIOD=5
export CONTIG_STATS=yes
export ONLINE_STATS=no
export PROMOTE_1GB_MAP=no
export NO_REPEAT_DEFRAG=no
export CHILD_PROC_STAT=no

sudo sysctl vm.break_1gb_allocation=1

if [[ "x${NO_REPEAT_DEFRAG}" == "xyes" ]]; then
	DEFRAG_OPT=_no_repeat
fi
# for 128GB memory, scanning results take noticeable memory, it may kick out benchmark memory
# this option put scanning results on node 0
export RELOCATE_AGENT_MEM=yes

if [[ "x${PROMOTE_1GB_MAP}" == "xyes" ]]; then
        sudo sysctl vm.mem_defrag_promote_thp=15
        PROMOTION="_promotion"
else
        sudo sysctl vm.mem_defrag_promote_thp=12
        PROMOTION="_no_promotion"
fi

if [[ "x${NO_REPEAT_DEFRAG}" == "xyes" ]]; then
        sudo sysctl vm.vma_no_repeat_defrag=1
else
        sudo sysctl vm.vma_no_repeat_defrag=0
fi

for I in $(seq 1 ${NUM_ITER}); do
	for THP_SIZE in ${THP_SIZE_LIST}; do

	if [[ "x${THP_SIZE}" == "x4kb" ]]; then
		echo "never" |  sudo tee /sys/kernel/mm/transparent_hugepage/enabled
		echo "never" |  sudo tee /sys/kernel/mm/transparent_hugepage/enabled_1gb
	elif [[ "x${THP_SIZE}" == "x2mb" ]]; then
		echo "always" |  sudo tee /sys/kernel/mm/transparent_hugepage/enabled
		echo "never" |  sudo tee /sys/kernel/mm/transparent_hugepage/enabled_1gb
	else
		echo "always" |  sudo tee /sys/kernel/mm/transparent_hugepage/enabled
		echo "always" |  sudo tee /sys/kernel/mm/transparent_hugepage/enabled_1gb
	fi

		export THP_SIZE=${THP_SIZE}


		for FACTOR in ${DEFRAG_FREQ_FACTOR_LIST}; do
			export DEFRAG_FREQ_FACTOR=${FACTOR}
			for SIZE in ${WORKLOAD_SIZE}; do

				export BENCH_SIZE=${SIZE}

				for BENCH in ${BENCH_LIST}; do
					export BENCH=${BENCH}

					echo "${BENCH}, ${BENCH_SIZE}"


					for CONFIG in ${CONFIG_LIST}; do
						if [[ "x${CONFIG}" == "xcompact_default" ]]; then
							if [[ "x${FACTOR}" != "x1" ]]; then
								continue
							fi
						fi
						export DEFRAG_CONFIG=${CONFIG}
						export CONFIG_FOLDER="mem_${CONFIG}_${THP_SIZE}${PROMOTION}${DEFRAG_OPT}"
						./setup_thp.sh ${CONFIG}

						if [ ! -d "${CONFIG_FOLDER}" ]; then
							mkdir ${CONFIG_FOLDER}
						fi

						echo ${CONFIG}


						if [[ "${CONFIG}x" == "no_compact_no_daemon_defragx" ]]; then
							export DO_DEFRAG=yes
						else
							export DO_DEFRAG=no
						fi

						if [[ "${CONFIG}x" == "compact_freqx" ]]; then
							export DO_THP_COMPACT=yes
						else
							export DO_THP_COMPACT=no
						fi
						if [[ "${CONFIG}x" == "defrag_after_thp_compactx" ]]; then
							export DO_DEFRAG_AFTER_THP_COMPACT=yes
						else
							export DO_DEFRAG_AFTER_THP_COMPACT=no
						fi
						if [[ "${CONFIG}x" == "defrag_and_thp_compactx" ]]; then
							export DO_DEFRAG_AND_THP_COMPACT=yes
						else
							export DO_DEFRAG_AND_THP_COMPACT=no
						fi

						for THREAD_NUM in ${THREAD_NUM_LIST}; do

							echo "Run ${I}, Thread ${THREAD_NUM}, THP size ${THP_SIZE}, config: ${CONFIG}, stats period: ${STATS_PERIOD}, defrag period: $((STATS_PERIOD*DEFRAG_FREQ_FACTOR))"

							./run_bench.sh ${THREAD_NUM};

							USED_SWAP=$(free -m | grep -i swap | awk '{print($3)}')
							if (( ${USED_SWAP} != 0  )); then
								sudo swapoff -a
								sudo swapon -a
							fi
							sleep 15;
							#numastat -m
						done


						mv result-${BENCH}* ${CONFIG_FOLDER}

					done #CONFIG

					unset DO_DEFRAG
					unset DO_THP_COMPACT
					unset DO_DEFRAG_AFTER_THP_COMPACT
					unset DO_DEFRAG_AND_THP_COMPACT


				done #BENCH

			done #SIZE
		done # DEFRAG_FREQ
	done #THP_SIZE
done #I
