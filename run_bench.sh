#!/bin/bash

if [ "x$1" != "x" ]
then
	export CPUS=$1
else
	export CPUS=1
fi

if [[ "x${STATS_PERIOD}" == "x" ]]; then
	STATS_PERIOD=5
fi

PROJECT_LOC=$(pwd)

LAUNCHER="${PROJECT_LOC}/launcher --dumpstats --dumpstats_period ${STATS_PERIOD}"

if [[ "x${RELOCATE_AGENT_MEM}" == "xyes" ]]; then
	LAUNCHER="${LAUNCHER} --relocate_agent_mem"
fi


THREAD_PER_CORE=`lscpu|grep ^Thread|awk '{print $NF}'`
CORE_PER_SOCKET=`lscpu|grep ^Core|awk '{print $NF}'`
SOCKET=`lscpu|grep Socket|awk '{print $NF}'`

TOTAL_CORE=$((CORE_PER_SOCKET*SOCKET))

FAST_NUMA_NODE=1

MEM_TYPE=mem-defrag

if [[ "x${DEFRAG_FREQ_FACTOR}" != "x" ]]; then
	LAUNCHER="${LAUNCHER} --defrag_freq_factor ${DEFRAG_FREQ_FACTOR}"
	export DEFRAG_ADDON=-defrag-period-$((STATS_PERIOD*DEFRAG_FREQ_FACTOR))
fi

CONFIG_INFO=${BENCH}-"${CPUS}-cpu-${BENCH_SIZE}"-${MEM_TYPE}-scan-period-${STATS_PERIOD}${DEFRAG_ADDON}-${DEFRAG_CONFIG}-`date +%F-%T`

echo "DO_DEFRAG: ${DO_DEFRAG}"

if [[ "x${NO_MIGRATE}" != "x" ]]; then
	LAUNCHER="${LAUNCHER} --nomigration"
fi

if [[ "x${DO_DEFRAG}" == "xyes" ]]; then
	LAUNCHER="${LAUNCHER} --mem_defrag"
fi

if [[ "x${DO_THP_COMPACT}" == "xyes" ]]; then
	LAUNCHER="${LAUNCHER} --thp_compact"
fi

if [[ "x${DO_DEFRAG_AFTER_THP_COMPACT}" == "xyes" ]]; then
	LAUNCHER="${LAUNCHER} --thp_compact_after_mem_defrag"
fi

if [[ "x${DO_DEFRAG_AND_THP_COMPACT}" == "xyes" ]]; then
	LAUNCHER="${LAUNCHER} --thp_compact_and_mem_defrag"

	if [[ "x${SLEEP_MS_DEFRAG}" != "x" ]]; then
		LAUNCHER="${LAUNCHER} --sleep_ms_defrag 100"
	fi
fi

if [[ "x${CHILD_PROC_STAT}" == "xyes" ]]; then
	LAUNCHER="${LAUNCHER} --child_proc_stat"
fi


#LAUNCHER="${LAUNCHER} --vm_stats --contig_stats --defrag_online_stats"
LAUNCHER="${LAUNCHER} --vm_stats"

if [[ "x${CONTIG_STATS}" == "xyes" ]]; then
	LAUNCHER="${LAUNCHER} --contig_stats"
fi

if [[ "x${ONLINE_STATS}" == "xyes" ]]; then
	LAUNCHER="${LAUNCHER} --defrag_online_stats"
fi


FAST_NUMA_NODE_CPUS=`numactl -H| grep "node ${FAST_NUMA_NODE} cpus" | cut -d" " -f 4-`
read -a CPUS_ARRAY <<< "${FAST_NUMA_NODE_CPUS}"


if [[ "x${PREFER_MEM_MODE}" == "xyes" ]]; then
NUMACTL_CMD="${LAUNCHER} -N 1 --prefer_memnode 1"
else
NUMACTL_CMD="${LAUNCHER} -N 1 -m 1"
fi

NUMACTL_CMD_CLIENT="${PROJECT_LOC}/scan_memory/launcher -N 0 -m 0 "

#sleep 5

ALL_CPU_MASK=0
for IDX in $(seq 0 $((CPUS-1)) ); do
	CPU_IDX=$((IDX % ${#CPUS_ARRAY[@]}))
	CPU_MASK=$((1<<${CPUS_ARRAY[${CPU_IDX}]}))
	#CPU_MASK=$((1<<${CPUS_ARRAY[${CPU_IDX}]} | 1<<(${CPUS_ARRAY[${CPU_IDX}]}+${TOTAL_CORE})))

	ALL_CPU_MASK=`echo "${CPU_MASK}+${ALL_CPU_MASK}" | bc`

done

ALL_CPU_MASK=`echo "obase=16; ${ALL_CPU_MASK}" | bc`

NUMACTL_CMD="${NUMACTL_CMD} -c 0x${ALL_CPU_MASK}"

echo "begin benchmark"

RES_FOLDER=result-${CONFIG_INFO}

mkdir -p ${RES_FOLDER}

sudo sysctl vm > ${RES_FOLDER}/vm_config


export NTHREADS=${CPUS}


sudo dmesg -c >/dev/null
CUR_PWD=`pwd`
	cd ${CUR_PWD}/${BENCH}
	source ./bench_run.sh

	rm mem_frag_stats_*
	rm mem_frag_contig_stats_*
	rm vm_stats_*
	rm defrag_online_stats_*
	rm proc_stat_*
	rm ${BENCH_NAME}.out

	echo "${NUMACTL_CMD} ${LAUNCHER_OPT} -- ${BENCH_RUN}" > ${CUR_PWD}/${RES_FOLDER}/${BENCH}_cmd
	export > ${CUR_PWD}/${RES_FOLDER}/${BENCH}_env
	
	eval ${PRE_CMD}

cat /proc/meminfo  > ${CUR_PWD}/${RES_FOLDER}/${BENCH}_meminfo
cat /proc/zoneinfo > ${CUR_PWD}/${RES_FOLDER}/${BENCH}_zoneinfo
cat /proc/vmstat > ${CUR_PWD}/${RES_FOLDER}/${BENCH}_vmstat
${PROJECT_LOC}/fraganalysis-mel.pl --node ${FAST_NUMA_NODE} > ${CUR_PWD}/${RES_FOLDER}/${BENCH}_fraginfo
	${NUMACTL_CMD} ${LAUNCHER_OPT} -- ${BENCH_RUN} 2> ${CUR_PWD}/${RES_FOLDER}/${BENCH}_cycles
cat /proc/meminfo >> ${CUR_PWD}/${RES_FOLDER}/${BENCH}_meminfo
cat /proc/vmstat >> ${CUR_PWD}/${RES_FOLDER}/${BENCH}_vmstat

	eval ${POST_CMD}

	unset LAUNCHER_OPT
	unset PRE_CMD
	unset POST_CMD
	
	for STATS_FILE in `ls mem_frag_stats_*`; do
		mv ${STATS_FILE} ${CUR_PWD}/${RES_FOLDER}/${BENCH}_${STATS_FILE}
	done
	for STATS_FILE in `ls mem_frag_contig_stats_*`; do
		mv ${STATS_FILE} ${CUR_PWD}/${RES_FOLDER}/${BENCH}_${STATS_FILE}
	done
	for STATS_FILE in `ls vm_stats_*`; do
		mv ${STATS_FILE} ${CUR_PWD}/${RES_FOLDER}/${BENCH}_${STATS_FILE}
	done
	for STATS_FILE in `ls defrag_online_stats_*`; do
		mv ${STATS_FILE} ${CUR_PWD}/${RES_FOLDER}/${BENCH}_${STATS_FILE}
	done
	for STATS_FILE in `ls proc_stat_*`; do
		mv ${STATS_FILE} ${CUR_PWD}/${RES_FOLDER}/${BENCH}_${STATS_FILE}
	done

	if [[ "x${PERF_FLAMEGRAPH}" == "xyes" ]]; then
		${PERF} script -i perf_results | ${FLAMEGRAPH_LOC}/stackcollapse-perf.pl > out.perf-folded
		${FLAMEGRAPH_LOC}/flamegraph.pl out.perf-folded > flamegraph.svg
		mv perf_results ${CUR_PWD}/${RES_FOLDER}/${BENCH}_perf_results
		mv flamegraph.svg ${CUR_PWD}/${RES_FOLDER}/${BENCH}_flamegraph.svg
	fi

	if [ -f perf_results ]; then
		mv perf_results ${CUR_PWD}/${RES_FOLDER}/${BENCH}_perf_results
	fi

	if [ -f pin_output ]; then
		mv pin_output ${CUR_PWD}/${RES_FOLDER}/${BENCH}_pin_output
	fi

	if [ -f ${BENCH_NAME}.out ]; then
		mv ${BENCH_NAME}.out ${CUR_PWD}/${RES_FOLDER}/
	fi

awk '{if (!($1 in dict)) {dict[$1] = int($2) } else {print $1, int($2)-dict[$1] } }' ${CUR_PWD}/${RES_FOLDER}/${BENCH}_meminfo > ${CUR_PWD}/${RES_FOLDER}/${BENCH}_meminfo_res
awk '{if (!($1 in dict)) {dict[$1] = int($2) } else {print $1, int($2)-dict[$1] } }' ${CUR_PWD}/${RES_FOLDER}/${BENCH}_vmstat > ${CUR_PWD}/${RES_FOLDER}/${BENCH}_vmstat_res
	cd ${CUR_PWD}
	cd ${CUR_PWD}
dmesg > ${RES_FOLDER}/${BENCH}_dmesg
sleep 5
numastat -m > ${RES_FOLDER}/${BENCH}_numastat

date

