#**************************************************************************
#
#	Copyright (c) 2017  QNAP Electronics Inc.  All Rights Reserved.
#
#	FILE:
#		Makefile
#
#	Abstract:
#		Makefile for Qboost Utility
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
INCLUDES = -I. -I${NAS_LIB_PATH}/include -I${NAS_LIB_PATH}/uLinux -I${NAS_LIB_PATH}/libnaslog-2.0.0 -I${NAS_LIB_PATH}/statistics -I${NAS_LIB_PATH}/common -I${SYS_TARGET_PREFIX}/usr/include -I../libqrmh
LIBS= -L${ROOT_PATH}/lib -L${ROOT_PATH}/usr/lib -L${ROOT_PATH}/usr/local/lib -L${TARGET_PREFIX}/usr/lib -L${SYS_LIB_PATH}/json-c-0.9/.libs -ljson -luLinux_Util -luLinux_PDC -luLinux_config -luLinux_NAS -luLinux_quota -luLinux_cgi -luLinux_Storage -luLinux_naslog -luLinux_nasauth -lssl -lcrypto -lpthread -lsqlite3 -L../../../DataService/DBMS/mariadb/lib -lmysqlclient #-L../libqrmh -lqrmh

ifeq ($(RECYCLE_EX),yes)
CFLAGS += -DRECYCLE_EX
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
endif

TARGET = qrmhd

all: $(TARGET)

qrmhd.o: qrmhd.c
	$(CC) $(CFLAGS) -c $< $(INCLUDES)

qrmhd: qrmhd.o
	$(CC) $(CFLAGS) $^ -o $@ ${LIBS}
	${OBJCOPY} --strip-all $@

clean:
	rm -f *.o core *~
	rm -f $(TARGET)
