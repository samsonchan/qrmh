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

static int debug_lvl=0;
static int daemonize_background=0;
static FILE *debug_fp=NULL;

#define QRU_LOG_E(format, args...) do { if(debug_lvl > 0) { fprintf((debug_fp)?debug_fp:stderr, "%s(%d)" format, __func__, __LINE__, ##args); if(debug_fp) fflush(debug_fp); } } while(0)
#define QRU_LOG_D(format, args...) do { if(debug_lvl > 1) { fprintf((debug_fp)?debug_fp:stdout, format, ##args); if(debug_fp) fflush(debug_fp); } } while(0)

#define DAY_INTERVAL		(300/60)
#define WEEK_INTERVAL		(1800/60)
#define MONTH_INTERVAL		(7200/60)
#define YEAR_INTERVAL		(86400/60)
#define TIME_DISPARITY		(600/60)

static int lock_fd=-1;
SYS_RES g_sys_res={};


#define QRUD_START_REOPEN_STD_012 \
	do { \
			int dev = open("/dev/null", 2); \
			if(dev != -1) \
			{ \
				close(0); \
				dup2(dev, 0); \
				close(1); \
				dup2(dev, 1); \
				close(2); \
				dup2(dev, 2); \
				close(dev); \
			} \
	} while(0)

static int get_sys_resource()
{
	int		ret=-1;

	if(0 > (ret=Get_System_Resource(&g_sys_res, SRES_ALL))){
		goto myreturn;
	}

	ret=0;
myreturn:
	return ret;
}

static int get_nic_resource()
{
	int		ret=-1;


	ret=0;
myreturn:
	return ret;
}

static int get_pool_resource()
{
	int		ret=-1;


	ret=0;
myreturn:
	return ret;
}

static int get_global_resource()
{
	int	ret=-1;
	
	if(0 > get_sys_resource()){
		goto myreturn;
	}






	ret=0;
myreturn:
	return ret;
}

static void cleanup_sys_resource()
{
	int	i,
		effective_cnt=0;

	QRU_LOG_D("Memory:\n");
	QRU_LOG_D("\tMemory Total[%u]\n", g_sys_res.mem.mem_total);
	QRU_LOG_D("\tMemory Used[%u]\n", g_sys_res.mem.mem_used);
	QRU_LOG_D("Swap Memory:\n");
	QRU_LOG_D("\tSwap Memory Total[%u]\n", g_sys_res.mem.swap_total);
	QRU_LOG_D("\tSwap Memory Used[%u]\n", g_sys_res.mem.swap_used);
	QRU_LOG_D("CPU:\n");
	QRU_LOG_D("\tCPU Usage[%.1f %%]\n", g_sys_res.cpu_info[0].real);
	QRU_LOG_D("Process(%d):\n", g_sys_res.proc_count);

	for(i=0; i<g_sys_res.proc_count; i++){
		if(effective_cnt < 500 && g_sys_res.proc_info[i].cpu_us > 0.1){
			QRU_LOG_D("\tEffective Iterm(%d) start\n", effective_cnt);			
			QRU_LOG_D("\t\tproc_name[%s]\n", g_sys_res.proc_info[i].proc_name);
			QRU_LOG_D("\t\tapp_des[%s]\n", g_sys_res.proc_info[i].proc_des); // application internal name or "sys_proc"
			QRU_LOG_D("\t\tuser[%s]\n", g_sys_res.proc_info[i].user);
			QRU_LOG_D("\t\tpid[%u]\n", g_sys_res.proc_info[i].pid);
			QRU_LOG_D("\t\tcpu_us[%.2f]\n", g_sys_res.proc_info[i].cpu_us);
			QRU_LOG_D("\t\tmem_us[%u]\n", g_sys_res.proc_info[i].mem_us);
			QRU_LOG_D("\t\tproc_tx[%llu]\n", g_sys_res.proc_info[i].proc_tx);
			QRU_LOG_D("\t\tproc_rx[%llu]\n", g_sys_res.proc_info[i].proc_rx);
			QRU_LOG_D("\tEffective Iterm(%d) end\n", effective_cnt);
			effective_cnt++;
		}
	}
	Free_System_Resource(&g_sys_res);
	memset(&g_sys_res, 0, sizeof(SYS_RES));
	return;
}

static void cleanup_global_resource()
{
	cleanup_sys_resource();


	return;
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
	}else{
		// child run
		setsid();
		QRUD_START_REOPEN_STD_012;
		chdir("/");
		umask(0);
	}
	return;
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
	QRU_LOG_D("Service start\n");

	int week_cnt=0,
		month_cnt=0,
		year_cnt=0;
	time_t last=time(0);
	while(1) {
		time_t 	now=time(0),
				recode_start=0,
				recode_end=0,
				recode_diff=0;

		if(-TIME_DISPARITY > (now-last) || (now-last) > TIME_DISPARITY){
			QRU_LOG_D("time disparity of %ld minutes detected.([last(%ld)][now(%ld)][diff(%ld)])\n", (now-last) / 60, last, now, (now-last));
			// we need drop the record that were after now time
			last=time(0);
			usleep(500000);
			continue;
		}
		if(now == last || (now % DAY_INTERVAL) != 0){
			usleep(500000);
			continue;
		}
		recode_start=now;		
		get_global_resource();
		// write resource data to day table
		QRU_LOG_D("Record Day[last(%ld)][now(%ld)][diff(%ld)]\n", last, now, (now-last));
		if(week_cnt == WEEK_INTERVAL / DAY_INTERVAL){
			// write resource data to week table
			QRU_LOG_D("Record Week[last(%ld)][now(%ld)][diff(%ld)]\n", last, now, (now-last));
			week_cnt=0;
		}
		if(month_cnt == MONTH_INTERVAL / DAY_INTERVAL){
			// write resource data to month table
			QRU_LOG_D("Record Month[last(%ld)][now(%ld)][diff(%ld)]\n", last, now, (now-last));
			month_cnt=0;	
		}
		if(year_cnt == YEAR_INTERVAL / DAY_INTERVAL){
			// write resource data to year table
			QRU_LOG_D("Record Year[last(%ld)][now(%ld)][diff(%ld)]\n", last, now, (now-last));
			year_cnt=0;
		}		
		cleanup_global_resource();
		recode_end=(uint32_t)time(0);
		recode_diff=recode_end-recode_start;

		if(recode_diff < DAY_INTERVAL){
			sleep(DAY_INTERVAL-recode_diff-1);
		}else{
			QRU_LOG_E("Miss DAY_INTERVAL(%d) [last(%ld)][now(%ld)][diff(%ld)][recode_diff(%ld)]\n", DAY_INTERVAL, last, now, (now-last), recode_diff);
		}

		last=now;
		week_cnt++;
		month_cnt++;
		year_cnt++;
	}
    return 0;
}

// ----------------------------------------------------------------------------
// Main
// ----------------------------------------------------------------------------
int main(int argc, char *argv[])
{
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
	return 0;
}

