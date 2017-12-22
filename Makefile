#**************************************************************************
#
#	Copyright (c) 2017  QNAP Electronics Inc.  All Rights Reserved.
#
#	FILE:
#		Makefile
#
#	Abstract:
#		Makefile for TaskManager Utility
#
#	HISTORY:
#**************************************************************************

ifdef NAS_LIB_PATH
$(shell export > ".build_env")
endif

ifeq (${NASWARE_PATH},)
$(error "NASWARE_PATH not defined")
endif

ifeq (${ROOT_PATH},)
$(error "ROOT_PATH not defined")
endif

CC = ${CROSS_COMPILE}gcc
CXX = ${CROSS_COMPILE}g++
CPP = ${CROSS_COMPILE}c++
STRIP = ${CROSS_COMPILE}strip
OBJCOPY = $(CROSS_COMPILE)objcopy

NAS_LIB_PATH = ${NASWARE_PATH}/NasLib
SYS_LIB_PATH = ${NASWARE_PATH}/SysLib
SYS_UTIL_PATH = ${NASWARE_PATH}/SysUtil

CFLAGS = -Wall -O2 -D${TARGET_PLATFORM}
INCLUDES = -I../lib -I${NAS_LIB_PATH}/include -I${NAS_LIB_PATH}/uLinux -I${NAS_LIB_PATH}/libnaslog-2.0.0 -I${NAS_LIB_PATH}/statistics -I${NAS_LIB_PATH}/common -I${SYS_TARGET_PREFIX}/usr/include
LIBS= -L../lib -L${ROOT_PATH}/lib -L${ROOT_PATH}/usr/lib -L${ROOT_PATH}/usr/local/lib -L${TARGET_PREFIX}/usr/lib -L${SYS_LIB_PATH}/json-c-0.9/.libs -ljson -luLinux_Util -luLinux_PDC -luLinux_config -luLinux_NAS -luLinux_quota -luLinux_cgi -luLinux_Storage -luLinux_naslog -luLinux_nasauth -lssl -lcrypto -lpthread -lqschedule -lsqlite3

ifeq ($(RECYCLE_EX),yes)
CFLAGS += -DRECYCLE_EX
endif

ifeq ($(WIRELESS),yes)
CFLAGS += -DWIRELESS
endif

ifeq (${QNAP_HAL_SUPPORT},yes)
LIBS += -luLinux_ini -luLinux_hal
CFLAGS += -DQNAP_HAL_SUPPORT
ifeq (${STORAGE_V2},yes)
CFLAGS += -DSTORAGE_V2
LIBS += -luLinux_qlicense -lxml2
INCLUDES += -I${NAS_LIB_PATH}/storage_man_v2 -I${NAS_LIB_PATH}/qlicense2
ifeq (${FOLDER_ENCRYPTION},ecryptfs)
CFLAGS += -DFOLDER_ENCRYPTION_ECRYPTFS -DFOLDER_ENCRYPTION
else ifeq (${FOLDER_ENCRYPTION},encfs)
CFLAGS += -DFOLDER_ENCRYPTION_ENCFS -DFOLDER_ENCRYPTION
endif
else
INCLUDES += -I${NAS_LIB_PATH}/storage_man
endif
ifeq (${NVS_SUPPORT},yes)
INCLUDES += -I${NAS_LIB_PATH}/network_management
LIBS += -L${ROOT_PATH}/lib -lnetworkinterface
CFLAGS += -DNVS_SUPPORT
endif
endif

TARGET = qscheduled qschedule_cli api_test qrud

all: $(TARGET)

qscheduled.o: qscheduled.c
	$(CC) $(CFLAGS) -c $< $(INCLUDES)

qschedule_cli.o: qschedule_cli.c
	$(CC) $(CFLAGS) -c $< $(INCLUDES)

api_test.o: api_test.c
	$(CC) $(CFLAGS) -c $< $(INCLUDES)

qrud.o: qrud.c
	$(CC) $(CFLAGS) -c $< $(INCLUDES)

qscheduled: qscheduled.o
	$(CC) $(CFLAGS) $^ -o $@ ${LIBS}
	${OBJCOPY} --strip-all $@

qschedule_cli: qschedule_cli.o
	$(CC) $(CFLAGS) $^ -o $@ ${LIBS}
	${OBJCOPY} --strip-all $@

api_test: api_test.o
	$(CC) $(CFLAGS) $^ -o $@ ${LIBS}
	${OBJCOPY} --strip-all $@

qrud: qrud.o
	$(CC) $(CFLAGS) $^ -o $@ ${LIBS}
	${OBJCOPY} --strip-all $@

clean:
	rm -f *.o core *~
	rm -f $(TARGET)
