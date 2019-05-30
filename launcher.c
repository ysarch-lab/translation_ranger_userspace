/*
 * =====================================================================================
 *
 *       Filename:  time_rdts.c
 *
 *    Description:
 *
 *        Version:  1.0
 *        Created:  09/21/2015 05:18:13 PM
 *       Revision:  none
 *       Compiler:  gcc
 *
 *         Author:  YOUR NAME (),
 *   Organization:
 *
 * =====================================================================================
 */

#define _GNU_SOURCE

#include <string.h>
#include <stdio.h>
#include <inttypes.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>
#include <getopt.h>
#include <numa.h>
#include <numaif.h>
#include <sched.h>
#include <errno.h>

#include <time.h>
#include <sys/time.h>
#define TV_MSEC tv_usec / 1000
#include <sys/resource.h>
#include <sys/utsname.h>


typedef struct
{
  int waitstatus;
  struct rusage ru;
  struct timeval start, elapsed; /* Wallclock time of process.  */
} RESUSE;

/*#define RUSAGE_CHILDREN -1*/

/* Avoid conflicts with ASCII code  */
enum {
	OPT_PIN_LOC = 256,
	OPT_PIN_TOOL_LOC,
	OPT_PERF_INTERV,
	OPT_TRACE_LOC,
	OPT_COLLECT_TRACE_AFTER,
	OPT_DEFRAG_FREQ_FACTOR,
	OPT_CHILD_STDIN,
	OPT_CHILD_STDOUT,
};

int syscall_mem_defrag = 332;

unsigned cycles_high, cycles_low;
unsigned cycles_high1, cycles_low1;
RESUSE time_stats;
pid_t child;
pid_t perf_pid;
pid_t pin_pid;
volatile int child_quit = 0;
volatile int info_done = 0;
int dumpstats_signal = 1;
int dumpstats_period = 1;
int mem_defrag = 0;
int vm_stats = 0;
int thp_compact = 0;
int mem_frag_full_stats = 0; /* each virtual-to-physical mapping  */
int mem_frag_contig_stats = 0; /* contig chunks in each vma */
int defrag_online_stats = 0;
int thp_compact_after_mem_defrag = 0;
int thp_compact_and_mem_defrag = 0;
unsigned int sleep_ms_defrag = 0;
int tlb_miss_tracked = 0;
int perf_flamegraph = 0;
int defrag_freq_factor = 1;
int child_proc_stat = 0;
volatile int collect_trace_after_second = 0;

long scan_process_memory(pid_t pid, char *buf, int len, int action)
{
	return syscall(syscall_mem_defrag, pid, buf, len, action);
}

static void sleep_ms(unsigned int milliseconds)
{
	struct timespec ts;

	if (!milliseconds)
		return;

	ts.tv_sec = milliseconds / 1000;
	ts.tv_nsec = (milliseconds % 1000) * 1000000;
	nanosleep(&ts, NULL);
}

static int get_new_filename(const char *filename, char **final_name)
{
	const char *template = "%s_%d";
	int len = strlen(filename) + 5; /* 1: _, 3: 0-999, 1: \n  */
	int index = 0;
	struct stat st;
	int file_not_exist;

	if (!final_name)
		return -EINVAL;

	*final_name = malloc(len);
	if (!*final_name)
		return -ENOMEM;
	memset(*final_name, 0, len);

	sprintf(*final_name, template,filename, index);

	while ((file_not_exist = stat(*final_name, &st)) == 0)
	{
		index++;
		sprintf(*final_name, template, filename, index);

		if (index >= 1000)
			break;
	}

	if (index >= 1000) {
		free(*final_name);
		*final_name = NULL;
		return -EBUSY;
	}

	return 0;
}

#define BUF_LEN 1024

static int dump_rusage(FILE *outfile, int stat_handle)
{

	unsigned cycles_now_high, cycles_now_low;
    uint64_t start = ((uint64_t)cycles_high <<32 | cycles_low);
    uint64_t now;
	char buf[BUF_LEN] = {0};
	int read_ret;

    asm volatile
        ( "RDTSCP\n\t"
          "mov %%edx, %0\n\t"
          "mov %%eax, %1\n\t"
          "CPUID\n\t"
          :
          "=r" (cycles_now_high), "=r" (cycles_now_low)
          ::
          "rax", "rbx", "rcx", "rdx"
        );
	now = ((uint64_t)cycles_now_high <<32 | cycles_now_low);
	fprintf(outfile, "%lu ", now - start);
	lseek(stat_handle, 0, SEEK_SET);
	while ((read_ret = read(stat_handle, buf, BUF_LEN)) > 0) {
		fputs(buf, outfile);
		memset(buf, 0, BUF_LEN);
	}
	fputs("----\n", outfile);

	if (read_ret < 0)
		return -1;
	return 0;
}

void read_stats_periodically(pid_t app_pid) {
	/*const char *stats_filename_template = "./mem_frag_stats_%d";*/
	/*const char *vm_stats_filename_template = "./vm_stats_%d";*/
	/*char proc_buf[64];*/
	char *stats_filename = NULL;
	char *vm_stats_filename = NULL;
	char *stats_buf = NULL;
	int stats_handle = 0;
	FILE *full_stats_output = NULL;
	FILE *contig_stats_output = NULL;
	FILE *defrag_online_output = NULL;
	FILE *vm_output = NULL;
	FILE *proc_stat_output = NULL;
	long read_ret;
	/*int status;*/
	int vm_stats_handle = 0;
	int proc_stat_handle = 0;
	/*const int buf_len = 1024 * 1024 * 1024;*/
	const int buf_len = 1024 * 1024 * 16;
	int loop_count = 0;

	stats_buf = malloc(buf_len);
	if (!stats_buf)
		return;
	memset(stats_buf, 0, buf_len);

	if (mem_frag_full_stats) {
		if (get_new_filename("./mem_frag_stats", &stats_filename))
			goto cleanup;

		full_stats_output = fopen(stats_filename, "w");
		if (!full_stats_output) {
			perror("cannot write stats file");
			goto cleanup;
		}
		free(stats_filename);
		stats_filename = NULL;
	}

	if (mem_frag_contig_stats) {
		if (get_new_filename("./mem_frag_contig_stats", &stats_filename))
			goto cleanup;

		contig_stats_output = fopen(stats_filename, "w");
		if (!contig_stats_output) {
			perror("cannot write stats file");
			goto cleanup;
		}
		free(stats_filename);
		stats_filename = NULL;
	}

	if (defrag_online_stats) {
		if (get_new_filename("./defrag_online_stats", &stats_filename))
			goto cleanup;

		defrag_online_output = fopen(stats_filename, "w");
		if (!defrag_online_output) {
			perror("cannot write stats file");
			goto cleanup;
		}
		free(stats_filename);
		stats_filename = NULL;
	}

	if (vm_stats) {
		if (get_new_filename("./vm_stats", &vm_stats_filename))
			goto close_and_cleanup;

		vm_output = fopen(vm_stats_filename, "w");
		if (!vm_output) {
			perror("cannot write vm_stats file");
			goto close_and_cleanup;
		}
		free(vm_stats_filename);
		vm_stats_filename = NULL;

		vm_stats_handle = open("/proc/vmstat", O_RDONLY);
		if (vm_stats_handle < 0) {
			perror("cannot open /proc/vmstat");
			goto close_all_and_cleanup;
		}
	}
	if (child_proc_stat) {
		char proc_stat_filename[64];
		if (get_new_filename("./proc_stat", &vm_stats_filename))
			goto close_all2_and_cleanup;

		proc_stat_output = fopen(vm_stats_filename, "w");
		if (!proc_stat_output) {
			perror("cannot write proc_stat file");
			goto close_all3_and_cleanup;
		}
		free(vm_stats_filename);
		vm_stats_filename = NULL;

		sprintf(proc_stat_filename, "/proc/%d/stat", app_pid);

		proc_stat_handle = open(proc_stat_filename, O_RDONLY);
		if (proc_stat_handle < 0) {
			perror("cannot open /proc/[pid]/stat");
			goto close_all3_and_cleanup;
		}
	}

	sleep(1);
	do {
		if (dumpstats_signal) {
			loop_count++;

			if (thp_compact_and_mem_defrag) {
				if (loop_count == 1) {
					thp_compact = 1;
					mem_defrag = 0;
				}
				/*else {*/
					/*thp_compact ^= 1;*/
					/*mem_defrag ^= 1;*/
				/*}*/

				if (collect_trace_after_second == -1) {
					thp_compact = 0;
					mem_defrag = 0;
				}

			} else if (thp_compact_after_mem_defrag) {
				/*printf("Current defrag count: %d (thp->defrag at 12)\n",*/
						/*loop_count);*/
				/*fflush(stdout);*/
				if (loop_count <= 12) {
					thp_compact = 1;
					mem_defrag = 0;
					if (loop_count == 12) {
						printf("(thp->defrag)\n");
						fflush(stdout);
					}

				} else {
					thp_compact = 0;
					mem_defrag = 1;
				}
			}
			if (loop_count % defrag_freq_factor == 0) {
				/* defrag memory before scanning  */
				if (mem_defrag) {
					if (defrag_online_stats) {
						while ((read_ret = scan_process_memory(app_pid, stats_buf, buf_len, 3)) > 0) {
							fputs(stats_buf, defrag_online_output);
							memset(stats_buf, 0, buf_len);
							sleep_ms(sleep_ms_defrag);
						}
						if (read_ret < 0)
							break;
						fputs("----\n", defrag_online_output);
					} else {
						while (scan_process_memory(app_pid, NULL, 0, 3) > 0)
							sleep_ms(sleep_ms_defrag);
					}
				}
				/* compact memory before scanning  */
				if (thp_compact)
					scan_process_memory(app_pid, NULL, 0, 4);

				/* alter flags only after actual work is done */
				if (thp_compact_and_mem_defrag) {
					thp_compact ^= 1;
					mem_defrag ^= 1;
				}
			}

			if (mem_frag_full_stats) {
				while ((read_ret = scan_process_memory(app_pid, stats_buf, buf_len, 0)) > 0) {
					fputs(stats_buf, full_stats_output);
					memset(stats_buf, 0, buf_len);
				}
				if (read_ret < 0)
					break;
				fputs("----\n", full_stats_output);
			}
			if (mem_frag_contig_stats) {
				while ((read_ret = scan_process_memory(app_pid, stats_buf, buf_len, 5)) > 0) {
					fputs(stats_buf, contig_stats_output);
					memset(stats_buf, 0, buf_len);
				}
				if (read_ret < 0)
					break;
				fputs("----\n", contig_stats_output);
			}

			if (vm_stats) {
				lseek(vm_stats_handle, 0, SEEK_SET);
				while ((read_ret = read(vm_stats_handle, stats_buf, buf_len)) > 0) {
					fputs(stats_buf, vm_output);
					memset(stats_buf, 0, buf_len);
				}
				if (read_ret < 0)
					break;
				fputs("----\n", vm_output);
			}

			if (child_proc_stat)
				dump_rusage(proc_stat_output, proc_stat_handle);

			/*while ((read_ret = ) > 0)	{*/
				/*fputs(stats_buf, output);*/
				/*memset(stats_buf, 0, 256);*/
			/*} */
			/*lseek(stats_handle, 0, SEEK_SET);*/
		}
		sleep(dumpstats_period);
	} while (!child_quit);


close_all3_and_cleanup:
	if (proc_stat_handle)
		close(proc_stat_handle);
	if (proc_stat_output)
		fclose(proc_stat_output);
close_all2_and_cleanup:
	if (vm_stats_handle)
		close(vm_stats_handle);
close_all_and_cleanup:
	if (vm_output)
		fclose(vm_output);
close_and_cleanup:
	if (contig_stats_output)
		fclose(contig_stats_output);
	if (defrag_online_output)
		fclose(defrag_online_output);
cleanup:
	if (full_stats_output)
		fclose(full_stats_output);
	if (stats_buf)
		free(stats_buf);
	if (stats_filename)
		free(stats_filename);
	if (vm_stats_filename)
		free(vm_stats_filename);

	return;
}

void toggle_dumpstats_signal()
{
	dumpstats_signal ^= 1;
}

void child_exit(int sig, siginfo_t *siginfo, void *context)
{
	char buffer[255];
	char proc_buf[64];
    uint64_t start;
    uint64_t end;
	int status;
	unsigned long r;		/* Elapsed real milliseconds.  */
	unsigned long system_time;
	unsigned long user_time;
	FILE *childinfo;
	unsigned long cpu_freq = 1;
	char *hz;
	char *unit;

	if (waitpid(siginfo->si_pid, &status, WNOHANG) != child)
		return;

	child_quit = 1;
	getrusage(RUSAGE_CHILDREN, &time_stats.ru);
    asm volatile
        ( "RDTSCP\n\t"
          "mov %%edx, %0\n\t"
          "mov %%eax, %1\n\t"
          "CPUID\n\t"
          :
          "=r" (cycles_high1), "=r" (cycles_low1)
          ::
          "rax", "rbx", "rcx", "rdx"
        );
	gettimeofday (&time_stats.elapsed, (struct timezone *) 0);

	time_stats.elapsed.tv_sec -= time_stats.start.tv_sec;
	if (time_stats.elapsed.tv_usec < time_stats.start.tv_usec)
	{
		/* Manually carry a one from the seconds field.  */
		time_stats.elapsed.tv_usec += 1000000;
		--time_stats.elapsed.tv_sec;
	}
	time_stats.elapsed.tv_usec -= time_stats.start.tv_usec;

	time_stats.waitstatus = status;

	r = time_stats.elapsed.tv_sec * 1000 + time_stats.elapsed.tv_usec / 1000;

	user_time = time_stats.ru.ru_utime.tv_sec * 1000 + time_stats.ru.ru_utime.TV_MSEC;
	system_time = time_stats.ru.ru_stime.tv_sec * 1000 + time_stats.ru.ru_stime.TV_MSEC;


    start = ((uint64_t)cycles_high <<32 | cycles_low);
    end = ((uint64_t)cycles_high1 <<32 | cycles_low1);


	fprintf(stderr, "cycles: %lu\n", end - start);

	fprintf(stderr, "real time(ms): %lu, user time(ms): %lu, system time(ms): %lu, virtual cpu time(ms): %lu\n",
			r, user_time, system_time, user_time+system_time);
	fprintf(stderr, "min_flt: %lu, maj_flt: %lu, maxrss: %lu KB\n",
			time_stats.ru.ru_minflt, time_stats.ru.ru_majflt,
			time_stats.ru.ru_maxrss);
	fflush(stderr);


	if (perf_pid)
		kill(perf_pid, SIGINT);

	info_done = 1;
}

int main(int argc, char** argv)
{
	static int dumpstats = 0;
	static int use_dumpstats_signal = 0;
	static int no_migration = 0;
	static int relocate_agent_mem = 0;
	static struct option long_options [] =
	{
		{"cpunode", required_argument, 0, 'N'},
		{"memnode", required_argument, 0, 'm'},
		{"prefer_memnode", required_argument, 0, 'M'},
		{"cpumask", required_argument, 0, 'c'},
		{"dumpstats", no_argument, &dumpstats, 1},
		{"dumpstats_signal", no_argument, &use_dumpstats_signal, 1},
		{"dumpstats_period", required_argument, 0, 'p'},
		{"defrag_freq_factor", required_argument, 0, OPT_DEFRAG_FREQ_FACTOR},
		{"memcg", required_argument, 0, 'g'},
		{"nomigration", no_argument, &no_migration, 1},
		{"mem_defrag", no_argument, &mem_defrag, 1},
		{"vm_stats", no_argument, &vm_stats, 1},
		{"thp_compact", no_argument, &thp_compact, 1},
		{"full_stats", no_argument, &mem_frag_full_stats, 1},
		{"contig_stats", no_argument, &mem_frag_contig_stats, 1},
		{"perf_loc", required_argument, 0, 'l'},
		{"perf_events", required_argument, 0, 'P'},
		{"perf_flamegraph", no_argument, &perf_flamegraph, 1},
		{"perf_interv", required_argument, 0, OPT_PERF_INTERV},
		{"defrag_online_stats", no_argument, &defrag_online_stats, 1},
		{"child_stdin", required_argument, 0, OPT_CHILD_STDIN},
		{"child_stdout", required_argument, 0, OPT_CHILD_STDOUT},
		{"thp_compact_after_mem_defrag", no_argument, &thp_compact_after_mem_defrag, 1},
		{"thp_compact_and_mem_defrag", no_argument, &thp_compact_and_mem_defrag, 1},
		{"sleep_ms_defrag", required_argument, 0, 'S'},
		{"tlb_miss_tracked", no_argument, &tlb_miss_tracked, 1},
		{"pin_loc", required_argument, 0, OPT_PIN_LOC},
		{"pin_tool_loc", required_argument, 0, OPT_PIN_TOOL_LOC},
		{"collect_trace_after", required_argument, 0, OPT_COLLECT_TRACE_AFTER},
		{"trace_loc", required_argument, 0, OPT_TRACE_LOC},
		{"relocate_agent_mem", no_argument, &relocate_agent_mem, 1},
		{"child_proc_stat", no_argument, &child_proc_stat, 1},
		{0,0,0,0}
	};
	struct sigaction child_exit_act = {0}, dumpstats_act = {0};

	int option_index = 0;
	int c;
	unsigned long cpumask = -1;
	int index;
	struct bitmask *cpu_mask = NULL;
	struct bitmask *node_mask = NULL;
	struct bitmask *mem_mask = NULL;
	struct bitmask *parent_mask = NULL;
	char memcg_proc[256] = {0};
	int use_memcg = 0;
	char perf_events[512] = {0};
	char perf_loc[256] = {0};
	int perf_interv = 0;
	int use_perf = 0;
	int child_stdin_fd = 0;
	int child_stdout_fd = 0;
	struct utsname kernel_info;
	int prefer_mem_mode = 0;
	int use_pin = 0;
	char pin_tool_loc[256] = {0};
	char pin_loc[256] = {0};
	char trace_loc[256] = {0};

	/* get kernel version at runtime, change syscall number accordingly  */
	if (uname(&kernel_info))
		return 0;
	/* 4.10 uses 332 as syscall number, afterwards 333 is used*/
	if (strncmp(kernel_info.release, "4.10.0", 6) > 0)
		syscall_mem_defrag = 333;
	if (strncmp(kernel_info.release, "4.19.0", 6) > 0)
		syscall_mem_defrag = 335;

	parent_mask = numa_allocate_nodemask();

	if (!parent_mask)
		numa_error("numa_allocate_nodemask");

	numa_bitmask_setbit(parent_mask, 1);

	/*numa_run_on_node(0);*/
	numa_bind(parent_mask);

	if (argc < 2)
		return 0;

	while ((c = getopt_long(argc, argv, "N:M:m:c:",
							long_options, &option_index)) != -1)
	{
		switch (c)
		{
			case 0:
				 /* If this option set a flag, do nothing else now. */
				if (long_options[option_index].flag != 0)
					break;
				printf ("option %s", long_options[option_index].name);
				if (optarg)
					printf (" with arg %s", optarg);
					printf ("\n");
				break;
			case 'N':
				/* cpunode = (int)strtol(optarg, NULL, 0); */
				node_mask = numa_parse_nodestring(optarg);
				break;
			case 'M':
				prefer_mem_mode = 1;
			case 'm':
				/* memnode = (int)strtol(optarg, NULL, 0); */
				mem_mask = numa_parse_nodestring(optarg);
				break;
			case 'c':
				cpumask = strtoul(optarg, NULL, 0);
				cpu_mask = numa_allocate_nodemask();
				index = 0;
				while (cpumask) {
					if (cpumask & 1) {
						numa_bitmask_setbit(cpu_mask, index);
					}
					cpumask = cpumask >> 1;
					++index;
				}
				break;
			case 'g':
				strncpy(memcg_proc, optarg, 255);
				use_memcg = 1;
				break;
			case 'p':
				dumpstats_period = atoi(optarg);
				break;
			case OPT_DEFRAG_FREQ_FACTOR:
				defrag_freq_factor = atoi(optarg);
				break;
			case 'P':
				strncpy(perf_events, optarg, 512);
				break;
			case 'l':
				strncpy(perf_loc, optarg, 255);
				use_perf = 1;
				break;
			case OPT_CHILD_STDIN:
				child_stdin_fd = open(optarg, O_RDONLY);
				if (!child_stdin_fd) {
					perror("child stdin file open error\n");
					exit(-1);
				}
				break;
			case OPT_CHILD_STDOUT:
				child_stdout_fd = open(optarg, O_CREAT | O_RDWR, 0644);
				if (!child_stdout_fd) {
					perror("child stdout file open error\n");
					exit(-1);
				}
				break;
			case 'S':
				sleep_ms_defrag = atoi(optarg);
				break;
			case OPT_PIN_LOC:
				strncpy(pin_loc, optarg, 255);
				break;
			case OPT_PIN_TOOL_LOC:
				strncpy(pin_tool_loc, optarg, 255);
				use_pin = 1;
				break;
			case OPT_PERF_INTERV:
				perf_interv = atoi(optarg);
				break;
			case OPT_TRACE_LOC:
				strncpy(trace_loc, optarg, 255);
				break;
			case OPT_COLLECT_TRACE_AFTER:
				collect_trace_after_second = atoi(optarg);
				break;
			case '?':
				return 1;
			default:
				abort();
		}
	}

	if (use_memcg) {
		int memcgd = open(memcg_proc, O_RDWR);
		char mypid[10];
		int err;

		if (memcgd < 0) {
			fprintf(stderr, "cannot open the memcg\n");
			exit(0);
		}

		sprintf(mypid, "%d\n", getpid());
		if ((err = write(memcgd, mypid, sizeof(mypid))) <= 0) {
			fprintf(stderr, "write to  memcg: %s error: %d\n", memcg_proc, err);
			if (err < 0)
				perror("memcg error");
			exit(0);
		}
		close(memcgd);
	}

	/* push it to child process command line  */
	argv += optind;

	printf("child arg: %s\n", argv[0]);

	/* cpu_mask overwrites node_mask  */
	if (cpu_mask)
	{
		numa_bitmask_free(node_mask);
		node_mask = NULL;
	}

	child_exit_act.sa_sigaction = child_exit;
	child_exit_act.sa_flags = SA_SIGINFO;
	if (sigaction(SIGCHLD, &child_exit_act, NULL) < 0) {
		perror("sigaction on SIGCHLD");
		exit(0);
	}

	dumpstats_act.sa_handler = toggle_dumpstats_signal;
	if (sigaction(SIGUSR1, &dumpstats_act, NULL) < 0) {
		perror("sigaction on dumpstats");
		exit(0);
	}

    asm volatile
        ( "CPUID\n\t"
          "RDTSC\n\t"
          "mov %%edx, %0\n\t"
          "mov %%eax, %1\n\t"
          :
          "=r" (cycles_high), "=r" (cycles_low)
          ::
          "rax", "rbx", "rcx", "rdx"
        );
	gettimeofday (&time_stats.start, (struct timezone *) 0);

	child = fork();

	if (child == 0) { // child
		int child_status;

		if (node_mask)
		{
			if (numa_run_on_node_mask_all(node_mask) < 0)
				numa_error("numa_run_on_node_mask_all");
		} else if (cpu_mask)
		{
			if (sched_setaffinity(getpid(), numa_bitmask_nbytes(cpu_mask),
							(cpu_set_t*)cpu_mask->maskp) < 0)
				numa_error("sched_setaffinity");
		}

		if (mem_mask && !no_migration)
		{
			if (prefer_mem_mode) {
				if (set_mempolicy(MPOL_PREFERRED,
							  mem_mask->maskp,
							  mem_mask->size + 1) < 0)
					numa_error("set_mempolicy");
			} else {
				if (set_mempolicy(MPOL_BIND,
							  mem_mask->maskp,
							  mem_mask->size + 1) < 0)
					numa_error("set_mempolicy");
			}
		}

		if (child_stdin_fd) {
			dup2(child_stdin_fd, 0);
			close(child_stdin_fd);
		}
		if (child_stdout_fd) {
			dup2(child_stdout_fd, 1);
			close(child_stdout_fd);
		}

		if (mem_defrag || thp_compact_and_mem_defrag ||
			thp_compact_after_mem_defrag ||
			mem_frag_full_stats || mem_frag_contig_stats)
			scan_process_memory(0, NULL, 0, 1);

		if (tlb_miss_tracked)
			scan_process_memory(0, NULL, 0, 8);

		child_status = execvp(argv[0], argv);

		perror("child die\n");
		fprintf(stderr, "application execution error: %d\n", child_status);
		exit(-1);
	}

	fprintf(stderr, "child pid: %d\n", child);
	fprintf(stdout, "child pid: %d\n", child);

	if (relocate_agent_mem) {
		numa_bitmask_setbit(parent_mask, 0);
		numa_set_membind(parent_mask);
	}

	if (use_perf) {
		char child_pid[8] = {0};

		/*sprintf(perf_cmd, "/gauls/kernels/linux/tools/perf/perf stat -e %s -p %d -o perf_results", perf_events, child);*/
		sprintf(child_pid, "%d", child);

		perf_pid = fork();
		if (perf_pid == 0) {
			if (perf_flamegraph) {
				if (strlen(perf_loc))
					execl(perf_loc, "perf", "record",
						  "-F", "99",
						  "-g",
						  "-p", child_pid,
						  "-o", "perf_results", (char *)NULL);
				else
					execl("perf", "perf", "record",
						  "-F", "99",
						  "-g",
						  "-p", child_pid,
						  "-o", "perf_results", (char *)NULL);
			} else {
				if (perf_interv) {
					char interv[8] = {0};

					sprintf(interv, "%d", perf_interv);
					if (strlen(perf_loc))
						execl(perf_loc, "perf", "stat",
							  "-e", perf_events, "-p", child_pid,
							  "-I", interv,
							  "-o", "perf_results", (char *)NULL);
					else
						execl("perf", "perf", "stat",
							  "-e", perf_events, "-p", child_pid,
							  "-I", interv,
							  "-o", "perf_results", (char *)NULL);
				} else {
					if (strlen(perf_loc))
						execl(perf_loc, "perf", "stat",
							  "-e", perf_events, "-p", child_pid,
							  "-o", "perf_results", (char *)NULL);
					else
						execl("perf", "perf", "stat",
							  "-e", perf_events, "-p", child_pid,
							  "-o", "perf_results", (char *)NULL);
				}
			}

			perror("perf execution error\n");
			exit(-1);
		}
	}

	if (use_pin) {
		char child_pid[8] = {0};

		sprintf(child_pid, "%d", child);

		printf("pin is attaching pid: %d\n", child);

		pin_pid = fork();
		if (pin_pid == 0) {
			if (collect_trace_after_second) {
				if (!strlen(trace_loc)) {
					fprintf(stderr, "trace location not specified\n");
					exit(0);
				}

				sleep(collect_trace_after_second);
				collect_trace_after_second = -1;

				/*-pin_memory_range 0x80000000:0x3f0000000*/
				if (strlen(pin_loc))
					execl(pin_loc, "pin",
						  "-pin_memory_range", "0x80000000:0x3f0000000",
						  "-pid", child_pid,
						  "-t", pin_tool_loc,
						  "-o", trace_loc,
						  (char *)NULL);
				else
					execl("pin", "pin",
						  "-pin_memory_range", "0x80000000:0x3f0000000",
						  "-pid", child_pid,
						  "-t", pin_tool_loc,
						  "-o", trace_loc,
						  (char *)NULL);
			} else {
				if (strlen(pin_loc))
					execl(pin_loc, "pin",
						  "-pid", child_pid,
						  "-t", pin_tool_loc,
						  "-o", "pin_output",
						  (char *)NULL);
				else
					execl("pin", "pin",
						  "-pid", child_pid,
						  "-t", pin_tool_loc,
						  "-o", "pin_output",
						  (char *)NULL);
			}
		}
	}

	if (node_mask)
		numa_bitmask_free(node_mask);
	if (mem_mask)
		numa_bitmask_free(mem_mask);
	if (cpu_mask)
		numa_bitmask_free(cpu_mask);
	if (parent_mask)
		numa_bitmask_free(parent_mask);

	if (use_dumpstats_signal)
		dumpstats_signal = 0;

	if (dumpstats || use_dumpstats_signal)
		read_stats_periodically(child);


	if (use_perf) {
		int status;
		waitpid(perf_pid, &status, 0);
	}

	while (!info_done)
		sleep(1);

	return 0;

}
