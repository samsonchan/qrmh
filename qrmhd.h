#define QRMH_START_REOPEN_STD_012 \
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

#define QRMH_END_REOPEN_STD_012

