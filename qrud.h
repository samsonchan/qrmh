
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

#define DAY_INTERVAL		(300/60)
#define WEEK_INTERVAL		(1800/60)
#define MONTH_INTERVAL		(7200/60)
#define TIME_DISPARITY		(600/60)
#define QRU_MAX_PROC_RECORD	500
#define QRU_BANWIDTH_CONF   "/etc/bandwidth_record.conf"


