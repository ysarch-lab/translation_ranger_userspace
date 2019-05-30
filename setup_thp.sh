#!/bin/bash

THP_NEVER=`cat /proc/cmdline | grep -o transparent_hugepage=never`

if [[ "x${THP_NEVER}" != "x" ]]; then
	DISABLE_THP=yes
else
	DISABLE_THP=no
fi

#if [[ "x${DISABLE_THP}" != "xyes" ]]; then
	#echo always | sudo tee /sys/kernel/mm/transparent_hugepage/enabled
#else
	#echo never | sudo tee /sys/kernel/mm/transparent_hugepage/enabled
#fi

case $1 in
	no_compact_no_daemon_defrag)
		echo never | sudo tee /sys/kernel/mm/transparent_hugepage/defrag
		echo 0 | sudo tee /sys/kernel/mm/transparent_hugepage/khugepaged/defrag
		echo 999999 |sudo tee /sys/kernel/mm/transparent_hugepage/khugepaged/alloc_sleep_millisecs
		echo 999999 |sudo tee /sys/kernel/mm/transparent_hugepage/khugepaged/scan_sleep_millisecs
		;;
	defrag*)
		echo never | sudo tee /sys/kernel/mm/transparent_hugepage/defrag
		echo 1 | sudo tee /sys/kernel/mm/transparent_hugepage/khugepaged/defrag
		echo 999999 |sudo tee /sys/kernel/mm/transparent_hugepage/khugepaged/alloc_sleep_millisecs
		echo 999999 |sudo tee /sys/kernel/mm/transparent_hugepage/khugepaged/scan_sleep_millisecs
		;;
	no_compact_no_daemon)
		echo never | sudo tee /sys/kernel/mm/transparent_hugepage/defrag
		echo 0 | sudo tee /sys/kernel/mm/transparent_hugepage/khugepaged/defrag
		echo 999999 |sudo tee /sys/kernel/mm/transparent_hugepage/khugepaged/alloc_sleep_millisecs
		echo 999999 |sudo tee /sys/kernel/mm/transparent_hugepage/khugepaged/scan_sleep_millisecs
		;;
	compact_default)
		echo never | sudo tee /sys/kernel/mm/transparent_hugepage/defrag
		echo 1 | sudo tee /sys/kernel/mm/transparent_hugepage/khugepaged/defrag
		echo 60000 |sudo tee /sys/kernel/mm/transparent_hugepage/khugepaged/alloc_sleep_millisecs
		echo 10000 |sudo tee /sys/kernel/mm/transparent_hugepage/khugepaged/scan_sleep_millisecs
		;;
	no_compact_default)
		echo never | sudo tee /sys/kernel/mm/transparent_hugepage/defrag
		echo 0 | sudo tee /sys/kernel/mm/transparent_hugepage/khugepaged/defrag
		echo 60000 |sudo tee /sys/kernel/mm/transparent_hugepage/khugepaged/alloc_sleep_millisecs
		echo 10000 |sudo tee /sys/kernel/mm/transparent_hugepage/khugepaged/scan_sleep_millisecs
		;;
	no_compact_freq)
		echo never | sudo tee /sys/kernel/mm/transparent_hugepage/defrag
		echo 0 | sudo tee /sys/kernel/mm/transparent_hugepage/khugepaged/defrag

		echo 10 |sudo tee /sys/kernel/mm/transparent_hugepage/khugepaged/alloc_sleep_millisecs
		echo 10 |sudo tee /sys/kernel/mm/transparent_hugepage/khugepaged/scan_sleep_millisecs
		;;
	compact_freq)
		echo never | sudo tee /sys/kernel/mm/transparent_hugepage/defrag
		echo 1 | sudo tee /sys/kernel/mm/transparent_hugepage/khugepaged/defrag

		echo 999999 |sudo tee /sys/kernel/mm/transparent_hugepage/khugepaged/alloc_sleep_millisecs
		echo 999999 |sudo tee /sys/kernel/mm/transparent_hugepage/khugepaged/scan_sleep_millisecs
		#echo 10 |sudo tee /sys/kernel/mm/transparent_hugepage/khugepaged/alloc_sleep_millisecs
		#echo 10 |sudo tee /sys/kernel/mm/transparent_hugepage/khugepaged/scan_sleep_millisecs
		;;
	*)
		;;

esac

#echo always | sudo tee /sys/kernel/mm/transparent_hugepage/enabled

#echo never | sudo tee /sys/kernel/mm/transparent_hugepage/defrag
#echo 1 | sudo tee /sys/kernel/mm/transparent_hugepage/khugepaged/defrag

#echo 10 |sudo tee /sys/kernel/mm/transparent_hugepage/khugepaged/alloc_sleep_millisecs
#echo 10 |sudo tee /sys/kernel/mm/transparent_hugepage/khugepaged/scan_sleep_millisecs

# echo 999999|sudo tee /sys/kernel/mm/transparent_hugepage/khugepaged/alloc_sleep_millisecs
# echo 999999|sudo tee /sys/kernel/mm/transparent_hugepage/khugepaged/scan_sleep_millisecs