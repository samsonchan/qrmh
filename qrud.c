#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <getopt.h>
#include <fcntl.h>
#include <libgen.h>

#include "cfg_system.h"
#include "qrud.h"

// ----------------------------------------------------------------------------
// Debug 
// ----------------------------------------------------------------------------
static int debug_lvl = 0;
static int daemonize_background = 0;
static FILE *debug_fp = NULL;

#define QRU_LOG_E(format, args...) do { if(debug_lvl > 0) { fprintf((debug_fp)?debug_fp:stderr, "%s(%d)" format, __func__, __LINE__, ##args); if(debug_fp) fflush(debug_fp); } } while(0)
#define QRU_LOG_D(format, args...) do { if(debug_lvl > 1) { fprintf((debug_fp)?debug_fp:stdout, format, ##args); if(debug_fp) fflush(debug_fp); } } while(0)

#define REAL_TIME_INTERVAL	5
#define DAY_INTERVAL		300
#define WEEK_INTERVAL		1800
#define MONTH_INTERVAL		7200
#define YEAR_INTERVAL		86400

static int lock_fd=-1;

static int schedule_proc()
{
	int		ret=-1,
			i;
	SYS_RES sys_res={};

	if(0 > (ret=Get_System_Resource(&sys_res, SRES_ALL))){
		goto myreturn;
	}
	printf("Memory:\n");
	printf("\tMemory Total[%u]\n", sys_res.mem.mem_total);
	printf("\tMemory Used[%u]\n", sys_res.mem.mem_used);
	printf("Swap Memory:\n");
	printf("\tSwap Memory Total[%u]\n", sys_res.mem.swap_total);
	printf("\tSwap Memory Used[%u]\n", sys_res.mem.swap_used);
	printf("CPU:\n");
	printf("\tCPU Usage[%.1f %%]\n", sys_res.cpu_info[0].real);
	printf("Process(%d):\n", sys_res.proc_count);
/*
	for(i=0; i<sys_res.proc_count; i++){
		printf("\tIterm(%d) start\n", i);
		printf("\t\tproc_name[%s]\n", sys_res.proc_info[i].proc_name);
		printf("\t\tapp_des[%s]\n", sys_res.proc_info[i].proc_des); // qpkg internal name or "sys_proc"
		printf("\t\tuser[%s]\n", sys_res.proc_info[i].user);
		printf("\t\tpid[%u]\n", sys_res.proc_info[i].pid);
		printf("\t\tcpu_us[%.2f]\n", sys_res.proc_info[i].cpu_us);
		printf("\t\tmem_us[%u]\n", sys_res.proc_info[i].mem_us);
		printf("\t\tproc_tx[%llu]\n", sys_res.proc_info[i].proc_tx);
		printf("\t\tproc_rx[%llu]\n", sys_res.proc_info[i].proc_rx);
		printf("\t\tstate[%c]\n", sys_res.proc_info[i].state);
		printf("\t\tcpu_exe_time[%llu]\n", sys_res.proc_info[i].exe_time);
		printf("\tIterm(%d) end\n", i);
	}
*/
	Free_System_Resource(&sys_res);
	ret=0;
myreturn:
	return ret;
}

void daemonize()
{
	int	child_pid=0;

	if(0 > (child_pid=fork())){
		printf("can't fork\n");
		exit(-1);
	}else if(0 < child_pid){
		// parent exit
		if(NULL != debug_fp){
			fclose(debug_fp);
			debug_fp=NULL;
		}
		exit(0);
	}
	// child run
	QRU_START_REOPEN_STD_012;
	chdir("/");
	umask(0);
}

static void usage(const char *name)
{
	printf(	"Usage : %s [option]\n"\
			"options:\n"\
			"   -h | --help			Show help\n"\
			"   -d | --debug		debug level\n"\
			"   -l | --log			log\n"\
			"   -b | --background	run as daemon background\n", name);
}

static int parse_opt(int argc, char **argv)
{
	int c;
	int opt_idx;
	struct option long_options[] = {
		{"help",		no_argument,		0,	'h'},
		{"debug",		required_argument,	0,	'd'},
		{"log",			required_argument,	0,	'l'},
		{"background",	no_argument,		0,	'b'},
		{0,			0,					0,	0}
	};

	while((c = getopt_long(argc, argv, "hd:l:b", long_options, &opt_idx)) != -1) {
		switch(c) {
			case 'd':
				debug_lvl = atoi(optarg);
				break;
			case 'l':
				if(optarg){
					if(NULL != debug_fp){
						fclose(debug_fp);
						debug_fp=NULL;
					}
					debug_fp=fopen(optarg, "a");
				}
				break;
			case 'b':
				daemonize_background = 1;
				break;
			case 'h':
			default:
				usage(basename(argv[0]));
				exit(0);
		}
	}
	return 0;
}

static int check_process()
{
	int	ret=-1;
#ifdef O_CLOEXEC
	lock_fd=open("/var/run/qrud.pid", O_CREAT | O_CLOEXEC | O_RDWR, 0600);
#else
	lock_fd=open("/var/run/qrud.pid", O_CREAT | O_RDWR, 0600);
#endif
	if(lock_fd < 0){
		goto myreturn;
	}else{
		if(lockf(lock_fd, F_TLOCK, 0)){
			perror("qrud is already running");
			goto myreturn;
		}
#ifndef O_CLOEXEC
#ifdef FD_CLOEXEC
		int val;
		if((val = fcntl(lock_fd, F_GETFD)) >= 0){
			val |= FD_CLOEXEC;
			if(fcntl(lock_fd, F_SETFD, val) < 0){
				perror("set FD_CLOEXEC");
				goto myreturn;
			}
		}
#endif
#endif
	}
	ret=0;
myreturn:
	return ret;
}

static int service_main(void)
{
	if(daemonize_background){
		daemonize();
	}
	if(0 > check_process()){
		exit(-1);
	}
	printf("Service start\n");

	uint32_t last=(uint32_t)time(0);
	while(1) {
		sleep(1);
		uint32_t now = (uint32_t)time(0);
		if(now == last || (now % REAL_TIME_INTERVAL) != 0) {
			if((now/REAL_TIME_INTERVAL) == (last/REAL_TIME_INTERVAL)){
				continue;
			}
		}
		printf("\n!!!!!!Record Start(%d)!!!!!!\n", now);
		schedule_proc();
		last = now;
	}
    return 0;
}

// ----------------------------------------------------------------------------
// Main
// ----------------------------------------------------------------------------
int main(int argc, char *argv[])
{
	setenv("QNAP_QPKG", "Qboost", 1);
	parse_opt(argc, argv);
	service_main();

	if(-1 != lock_fd){
		close(lock_fd);
		lock_fd=-1;
	}
	if(NULL != debug_fp){
		fclose(debug_fp);
		debug_fp=NULL;
	}
	QRU_END_REOPEN_STD_012;
	return 0;
}

