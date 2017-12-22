#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <getopt.h>
#include <fcntl.h>
#include <libgen.h>

#include "Util.h"
#include "NAS.h"
#include "cfg_system.h"
#include "cfg_nic.h"
#include "qrud.h"
#include "qru_mariadb.h"
#ifdef QNAP_HAL_SUPPORT
#include "storage_man.h"
#include "volume.h"
#include "cfg_tbtnet.h"
#endif
#ifdef NVS_SUPPORT
#include "cpp_src/api.h"
#endif

static int debug_lvl=0;
static int daemonize_background=0;
static FILE *debug_fp=NULL;

#define QRU_LOG_E(format, args...) do { if(debug_lvl > 0) { fprintf((debug_fp)?debug_fp:stderr, "%s(%d)" format, __func__, __LINE__, ##args); if(debug_fp) fflush(debug_fp); } } while(0)
#define QRU_LOG_D(format, args...) do { if(debug_lvl > 1) { fprintf((debug_fp)?debug_fp:stdout, format, ##args); if(debug_fp) fflush(debug_fp); } } while(0)

static int lock_fd=-1;

static int g_get_sys_res=0;
static int g_get_nic_res=0;
static int g_get_pool_res=0;

static int g_proc_record_cnt=0;
static int g_app_record_cnt=0;
static int g_nic_record_cnt=0;
static int g_pool_record_cnt=0;

static QRU_CPU_INFO *g_cpu_info=NULL;
static QRU_MEM_INFO *g_mem_info=NULL;
static QRU_PROC_INFO *g_proc_info=NULL;
static QRU_APP_INFO *g_app_info=NULL;
static QRU_NIC_INFO *g_nic_info=NULL;
static QRU_POOL_INFO *g_pool_info=NULL;

static int get_sys_resource(time_t monitor_time)
{
	int		ret=-1,
			i,
			j,
			effective_proc_cnt=0,
			effective_app_cnt=0,
			qpkg_cnt=0;

	SYS_RES sys_res={};
	QPKG_INFO *qpkg_list=NULL;

	if(0 > (ret=Get_System_Resource(&sys_res, SRES_ALL))){
		goto myreturn;
	}

	if(NULL == (g_cpu_info=calloc(1, sizeof(QRU_CPU_INFO)))){
		goto myreturn;
	}
	g_cpu_info->monitor_time=(unsigned long long)monitor_time;

	if(NULL == (g_mem_info=calloc(1, sizeof(QRU_MEM_INFO)))){
		goto myreturn;
	}	
	g_mem_info->monitor_time=(unsigned long long)monitor_time;
	g_mem_info->mem_usage=(double)(100 * sys_res.mem.mem_used / sys_res.mem.mem_total);
	g_mem_info->mem_used=sys_res.mem.mem_used;

	for(i=0; i<sys_res.proc_count; i++){
		if(effective_proc_cnt < QRU_MAX_PROC_RECORD && sys_res.proc_info[i].cpu_us > 0.1){			
			QRU_PROC_INFO *ptr_proc=NULL;
			QRU_PROC_INFO one_proc_info={};
			if(NULL == (ptr_proc=(QRU_PROC_INFO*)realloc((void*)g_proc_info, (effective_proc_cnt+1)*sizeof(QRU_PROC_INFO)))){
				goto myreturn;
			}
			one_proc_info.monitor_time=(unsigned long long)monitor_time;
			one_proc_info.pid=sys_res.proc_info[i].pid;
			strncpy(one_proc_info.user, sys_res.proc_info[i].user, sizeof(one_proc_info.user)-1);
			strncpy(one_proc_info.proc_name, sys_res.proc_info[i].proc_name, sizeof(one_proc_info.proc_name)-1);
			strncpy(one_proc_info.proc_app_name, sys_res.proc_info[i].proc_des, sizeof(one_proc_info.proc_app_name)-1);
			one_proc_info.proc_cpu_usage=sys_res.proc_info[i].cpu_us;
			one_proc_info.proc_mem_usage=(double)(100 * sys_res.proc_info[i].mem_us / sys_res.mem.mem_total);
			one_proc_info.proc_mem_used=sys_res.proc_info[i].mem_us;
			one_proc_info.proc_tx=sys_res.proc_info[i].proc_tx;
			one_proc_info.proc_rx=sys_res.proc_info[i].proc_rx;
			g_proc_info=ptr_proc;
			memcpy(&g_proc_info[effective_proc_cnt], &one_proc_info, sizeof(QRU_PROC_INFO));
			effective_proc_cnt++;
		}
		g_cpu_info->cpu_usage += sys_res.proc_info[i].cpu_us;
	}
	g_proc_record_cnt=effective_proc_cnt;

	qpkg_cnt=Get_All_QPKG_Info(&qpkg_list);
	for(i=0; i<qpkg_cnt; i++){
		QRU_APP_INFO *ptr_app=NULL;
		QRU_APP_INFO one_app_info={};
		
		if(NULL == (ptr_app=(QRU_APP_INFO*)realloc((void*)g_app_info, (effective_app_cnt+1)*sizeof(QRU_APP_INFO)))){
			goto myreturn;
		}
		one_app_info.monitor_time=(unsigned long long)monitor_time;
		strncpy(one_app_info.app_name, qpkg_list[i].name, sizeof(one_app_info.app_name)-1);
		one_app_info.app_cpu_usage=0.0;
		g_app_info=ptr_app;
		memcpy(&g_app_info[effective_app_cnt], &one_app_info, sizeof(QRU_APP_INFO));
		effective_app_cnt++;
	}

	for(i=0; i<g_proc_record_cnt; i++){
		for(j=0; j<i; j++){
			// check if the picked element is already printed
			if(!strcmp(g_proc_info[i].proc_app_name, g_proc_info[j].proc_app_name)){
				break;
			}
		}
		// if not printed earlier, it means not duplicate
		if(i == j){
			int	k,
				is_qpkg=0;
			for(k=0; k<qpkg_cnt; k++){
				if(0 == strcmp(g_proc_info[i].proc_app_name, qpkg_list[k].name)){
					is_qpkg=1;
					break;
				}
			}
			// if not qpkg, append it
			if(!is_qpkg){
				QRU_APP_INFO *ptr_app=NULL;
				QRU_APP_INFO one_app_info={};
				
				if(NULL == (ptr_app=(QRU_APP_INFO*)realloc((void*)g_app_info, (effective_app_cnt+1)*sizeof(QRU_APP_INFO)))){
					goto myreturn;
				}
				one_app_info.monitor_time=(unsigned long long)monitor_time;
				strncpy(one_app_info.app_name, g_proc_info[i].proc_app_name, sizeof(one_app_info.app_name)-1);
				g_app_info=ptr_app;
				memcpy(&g_app_info[effective_app_cnt], &one_app_info, sizeof(QRU_APP_INFO));
				effective_app_cnt++;
			}
		}
	}
	g_app_record_cnt=effective_app_cnt;

	for(i=0; i<g_app_record_cnt; i++){
		for(j=0; j<g_proc_record_cnt; j++){
			if(!strcmp(g_app_info[i].app_name, g_proc_info[j].proc_app_name)){
				g_app_info[i].app_cpu_usage += g_proc_info[j].proc_cpu_usage;
				g_app_info[i].app_mem_usage += g_proc_info[j].proc_mem_usage;
				g_app_info[i].app_mem_used += g_proc_info[j].proc_mem_used;
				g_app_info[i].app_proc_tx += g_proc_info[j].proc_tx;
				g_app_info[i].app_proc_rx += g_proc_info[j].proc_rx;
			}
		}
	}

	ret=0;
myreturn:
	if(0 != ret){
		if(NULL != g_cpu_info){
			free(g_cpu_info);
			g_cpu_info=NULL;
		}
		if(NULL != g_mem_info){
			free(g_mem_info);
			g_mem_info=NULL;
		}
		if(NULL != g_proc_info){
			free(g_proc_info);
			g_proc_info=NULL;
			g_proc_record_cnt=0;
		}
		if(NULL != g_app_info){
			free(g_app_info);
			g_app_info=NULL;
			g_app_record_cnt=0;
		}
	}
	if(NULL != qpkg_list){
		free(qpkg_list);
		qpkg_list=NULL;
	}
	Free_System_Resource(&sys_res);
	return ret;
}

#ifdef NVS_SUPPORT
static int get_nic_resource(time_t monitor_time)
{
	int		ret=-1,
			wifi_count=0,
			eth_count=0,
			qa_port_count=0,
			bonding_count=0,
			bonding_member_count=0,
			//tbt_count=0,
			br_count=0,
			i,
			j,
			effective_cnt=0;
	
	char	buf[BUF_SIZE]={};
	
	INTERFACE_LIST	*wifi_list=NULL,
					*eth_list=NULL,
					*qa_port_list=NULL,
					*bonding_list=NULL,
					*bonding_member_list=NULL,
					//*tbt_list=NULL,
					*br_list=NULL;
 	
	// Wifi
	if(0 < (wifi_count=Get_interface_count(QWLAN))){
		wifi_list=calloc(wifi_count, sizeof(INTERFACE_LIST));
		if(NULL != wifi_list){
			if(0 == Get_interface_list(QWLAN, wifi_list, wifi_count)){
				for(i=0; i<wifi_count; i++){
					AUTO_INTERFACE_STATUS	*if_status=NULL;
					int if_ret=0;
					if(0 == (if_ret=AUTO_Get_interface_status(wifi_list[i].ifindex, &if_status))){						
						QRU_NIC_INFO *ptr=NULL;
						QRU_NIC_INFO one_nic_info={};
						if(NULL == (ptr=(QRU_NIC_INFO*)realloc((void*)g_nic_info, (effective_cnt+1)*sizeof(QRU_NIC_INFO)))){
							free(wifi_list);
							wifi_list=NULL;
							goto myreturn;
						}
						one_nic_info.monitor_time=monitor_time;
						one_nic_info.nic_type=QRU_WLAN;
						strncpy(one_nic_info.nic_name, wifi_list[i].ifname, sizeof(one_nic_info.nic_name)-1);
						strncpy(one_nic_info.nic_display_name, wifi_list[i].display_name, sizeof(one_nic_info.nic_display_name)-1);
						strncpy(one_nic_info.nic_member, "", sizeof(one_nic_info.nic_member)-1);
 						memset(buf, 0, sizeof(buf));
						Get_Private_Profile_String(wifi_list[i].ifname, "RX", "0", buf, sizeof(buf), QRU_BANWIDTH_CONF);
						one_nic_info.nic_rx=atoll(buf)/BANDWIDTH_CACHE_EXPIRE;
 						memset(buf, 0, sizeof(buf));
						Get_Private_Profile_String(wifi_list[i].ifname, "TX", "0", buf, sizeof(buf), QRU_BANWIDTH_CONF);
						one_nic_info.nic_tx=atoll(buf)/BANDWIDTH_CACHE_EXPIRE;
						g_nic_info=ptr;
						memcpy(&g_nic_info[effective_cnt], &one_nic_info, sizeof(QRU_NIC_INFO));
						effective_cnt++;
  					}
				}
			}
			free(wifi_list);
			wifi_list=NULL;
		}
	}
	
#ifdef QNAP_HAL_SUPPORT
	// USBQuickAccess
	if(0 < (qa_port_count=Get_interface_count(QQAPORT))){
		qa_port_list=calloc(qa_port_count, sizeof(INTERFACE_LIST));
		if(NULL != qa_port_list){
			if(0 == Get_interface_list(QQAPORT, qa_port_list, qa_port_count)){
				for(i=0; i<qa_port_count; i++){
					AUTO_INTERFACE_STATUS	*if_status=NULL;
					int if_ret=0;
					if(0 == (if_ret=AUTO_Get_interface_status(qa_port_list[i].ifindex, &if_status))){
						QRU_NIC_INFO *ptr=NULL;
						QRU_NIC_INFO one_nic_info={};
						if(NULL == (ptr=(QRU_NIC_INFO*)realloc((void*)g_nic_info, (effective_cnt+1)*sizeof(QRU_NIC_INFO)))){
							free(qa_port_list);
							qa_port_list=NULL;
							goto myreturn;
						}
						one_nic_info.monitor_time=monitor_time;
						one_nic_info.nic_type=QRU_QAPORT;
						strncpy(one_nic_info.nic_name, qa_port_list[i].ifname, sizeof(one_nic_info.nic_name)-1);
						strncpy(one_nic_info.nic_display_name, qa_port_list[i].display_name, sizeof(one_nic_info.nic_display_name)-1);
						strncpy(one_nic_info.nic_member, "", sizeof(one_nic_info.nic_member)-1);
						memset(buf, 0, sizeof(buf));
						Get_Private_Profile_String(qa_port_list[i].ifname, "RX", "0", buf, sizeof(buf), QRU_BANWIDTH_CONF);
						one_nic_info.nic_rx=atoll(buf)/BANDWIDTH_CACHE_EXPIRE;
						memset(buf, 0, sizeof(buf));
						Get_Private_Profile_String(qa_port_list[i].ifname, "TX", "0", buf, sizeof(buf), QRU_BANWIDTH_CONF);
						one_nic_info.nic_tx=atoll(buf)/BANDWIDTH_CACHE_EXPIRE;
						g_nic_info=ptr;
						memcpy(&g_nic_info[effective_cnt], &one_nic_info, sizeof(QRU_NIC_INFO));
						effective_cnt++;
					}
				}
			}
			free(qa_port_list);
			qa_port_list=NULL;
		}
	}
#endif

	// Ethernet 
	if(0 < (eth_count=Get_interface_count(QETHERNET))){
		eth_list=calloc(eth_count, sizeof(INTERFACE_LIST));
		if(NULL != eth_list){
			if(0 == Get_interface_list(QETHERNET, eth_list, eth_count)){
				for(i=0; i<eth_count; i++){
					AUTO_INTERFACE_STATUS	*if_status=NULL;
					int if_ret=0;
					if(0 == (if_ret=AUTO_Get_interface_status(eth_list[i].ifindex, &if_status))){
						QRU_NIC_INFO *ptr=NULL;
						QRU_NIC_INFO one_nic_info={};
						if(NULL == (ptr=(QRU_NIC_INFO*)realloc((void*)g_nic_info, (effective_cnt+1)*sizeof(QRU_NIC_INFO)))){
							free(eth_list);
							eth_list=NULL;
							goto myreturn;
						}
						one_nic_info.monitor_time=monitor_time;
						one_nic_info.nic_type=QRU_ETHERNET;
						strncpy(one_nic_info.nic_name, eth_list[i].ifname, sizeof(one_nic_info.nic_name)-1);
						strncpy(one_nic_info.nic_display_name, eth_list[i].display_name, sizeof(one_nic_info.nic_display_name)-1);
						strncpy(one_nic_info.nic_member, "", sizeof(one_nic_info.nic_member)-1);
						memset(buf, 0, sizeof(buf));
						Get_Private_Profile_String(eth_list[i].ifname, "RX", "0", buf, sizeof(buf), QRU_BANWIDTH_CONF);
						one_nic_info.nic_rx=atoll(buf)/BANDWIDTH_CACHE_EXPIRE;
						memset(buf, 0, sizeof(buf));
						Get_Private_Profile_String(eth_list[i].ifname, "TX", "0", buf, sizeof(buf), QRU_BANWIDTH_CONF);
						one_nic_info.nic_tx=atoll(buf)/BANDWIDTH_CACHE_EXPIRE;
						g_nic_info=ptr;
						memcpy(&g_nic_info[effective_cnt], &one_nic_info, sizeof(QRU_NIC_INFO));
						effective_cnt++;
					}
				}
			}
			free(eth_list);
			eth_list=NULL;
		}
	}

#ifdef QNAP_HAL_SUPPORT
	if (TBT_Support() == 1){
		// Thunderbolt Bridge
		if(0 < (br_count=Get_interface_count(QBRIDGE))){
			br_list=calloc(br_count, sizeof(INTERFACE_LIST));
			if(NULL != br_list){
				if(0 == Get_interface_list(QBRIDGE, br_list, br_count)){
					for(i=0; i<br_count; i++){
						if(NULL != strstr(br_list[i].ifname, "tbtbr")){
							QRU_NIC_INFO *ptr=NULL;
							QRU_NIC_INFO one_nic_info={};
							if(NULL == (ptr=(QRU_NIC_INFO*)realloc((void*)g_nic_info, (effective_cnt+1)*sizeof(QRU_NIC_INFO)))){
								free(br_list);
								br_list=NULL;
								goto myreturn;
							}
							one_nic_info.monitor_time=monitor_time;
							one_nic_info.nic_type=QRU_BRIDGE;
							strncpy(one_nic_info.nic_name, br_list[i].ifname, sizeof(one_nic_info.nic_name)-1);
							strncpy(one_nic_info.nic_display_name, br_list[i].display_name, sizeof(one_nic_info.nic_display_name)-1);
							strncpy(one_nic_info.nic_member, "", sizeof(one_nic_info.nic_member)-1);
							memset(buf, 0, sizeof(buf));
							Get_Private_Profile_String(br_list[i].ifname, "RX", "0", buf, sizeof(buf), QRU_BANWIDTH_CONF);
							one_nic_info.nic_rx=atoll(buf)/BANDWIDTH_CACHE_EXPIRE;
							memset(buf, 0, sizeof(buf));
							Get_Private_Profile_String(br_list[i].ifname, "TX", "0", buf, sizeof(buf), QRU_BANWIDTH_CONF);
							one_nic_info.nic_tx=atoll(buf)/BANDWIDTH_CACHE_EXPIRE;
							g_nic_info=ptr;
							memcpy(&g_nic_info[effective_cnt], &one_nic_info, sizeof(QRU_NIC_INFO));
							effective_cnt++;
						}
					}
				}
				free(br_list);
				br_list=NULL;
			}
		}
/*	
		// Thunderbolt
		if(0 < (tbt_count=Get_interface_count(QTHUNDERBOLT))){
			tbt_list=calloc(tbt_count, sizeof(INTERFACE_LIST));
			if(NULL != tbt_list){
				if(0 == Get_interface_list(QTHUNDERBOLT, tbt_list, tbt_count)){
					for(i=0; i<tbt_count; i++){
						AUTO_INTERFACE_STATUS	*if_status=NULL;
						int if_ret=0;
						if(0 == (if_ret=AUTO_Get_interface_status(tbt_list[i].ifindex, &if_status))){
							QRU_NIC_INFO *ptr=NULL;
							QRU_NIC_INFO one_nic_info={};
							if(NULL == (ptr=(QRU_NIC_INFO*)realloc((void*)g_nic_info, (effective_cnt+1)*sizeof(QRU_NIC_INFO)))){
								free(tbt_list);
								tbt_list=NULL;
								goto myreturn;
							}
							one_nic_info.monitor_time=monitor_time;
							one_nic_info.nic_type=QRU_THUNDERBOLT;
							strncpy(one_nic_info.nic_name, tbt_list[i].ifname, sizeof(one_nic_info.nic_name)-1);
							strncpy(one_nic_info.nic_display_name, tbt_list[i].display_name, sizeof(one_nic_info.nic_display_name)-1);
							strncpy(one_nic_info.nic_member, "", sizeof(one_nic_info.nic_member)-1);
							memset(buf, 0, sizeof(buf));
							Get_Private_Profile_String(tbt_list[i].ifname, "RX", "0", buf, sizeof(buf), QRU_BANWIDTH_CONF);
							one_nic_info.nic_rx=atoll(buf)/BANDWIDTH_CACHE_EXPIRE;
							memset(buf, 0, sizeof(buf));
							Get_Private_Profile_String(tbt_list[i].ifname, "TX", "0", buf, sizeof(buf), QRU_BANWIDTH_CONF);
							one_nic_info.nic_tx=atoll(buf)/BANDWIDTH_CACHE_EXPIRE;
							g_nic_info=ptr;
							memcpy(&g_nic_info[effective_cnt], &one_nic_info, sizeof(QRU_NIC_INFO));
							effective_cnt++;
						}
					}
				}
				free(tbt_list);
				tbt_list=NULL;
			}
		}
*/
	}
#endif

	// Port trunking 
	if(0 < (bonding_count=Get_interface_count(QBONDING))){
		bonding_list=calloc(bonding_count, sizeof(INTERFACE_LIST));
		if(NULL != bonding_list){
			if(0 == Get_interface_list(QBONDING, bonding_list, bonding_count)){
				for(i=0; i<bonding_count; i++){
					int 	offset=0;
					char	*bonding_ptr=NULL;
					AUTO_INTERFACE_STATUS	*if_status=NULL;
					int if_ret=0;
					if(0 == (if_ret=AUTO_Get_interface_status(bonding_list[i].ifindex, &if_status))){
						if(if_status->iface_updown){
							QRU_NIC_INFO *ptr=NULL;
							QRU_NIC_INFO one_nic_info={};
							if(NULL == (ptr=(QRU_NIC_INFO*)realloc((void*)g_nic_info, (effective_cnt+1)*sizeof(QRU_NIC_INFO)))){
								free(bonding_list);
								bonding_list=NULL;
								goto myreturn;
							}
							one_nic_info.monitor_time=monitor_time;
							one_nic_info.nic_type=QRU_BONDING;
							strncpy(one_nic_info.nic_name, bonding_list[i].ifname, sizeof(one_nic_info.nic_name)-1);
							strncpy(one_nic_info.nic_display_name, bonding_list[i].display_name, sizeof(one_nic_info.nic_display_name)-1);
							memset(buf, 0, sizeof(buf));
							Get_Private_Profile_String(bonding_list[i].ifname, "RX", "0", buf, sizeof(buf), QRU_BANWIDTH_CONF);
							one_nic_info.nic_rx=atoll(buf)/BANDWIDTH_CACHE_EXPIRE;
							memset(buf, 0, sizeof(buf));
							Get_Private_Profile_String(bonding_list[i].ifname, "TX", "0", buf, sizeof(buf), QRU_BANWIDTH_CONF);
							one_nic_info.nic_tx=atoll(buf)/BANDWIDTH_CACHE_EXPIRE;
							if(0 < (bonding_member_count=Get_bonding_slaves_count(bonding_list[i].ifindex))){
								bonding_member_list=calloc(bonding_member_count, sizeof(INTERFACE_LIST));
								if(NULL != bonding_member_list){
									if(0 == Get_bonding_slaves(bonding_list[i].ifindex, bonding_member_list, bonding_member_count)){
										for(j=0; j<bonding_member_count; j++){
											if(j == bonding_member_count-1){
												if(NULL != (bonding_ptr=strstr(bonding_member_list[j].ifname, "eth"))){
													offset += snprintf(one_nic_info.nic_member+offset, sizeof(one_nic_info.nic_member)-offset, "%s", bonding_ptr+strlen("eth"));
												}
											}else{
												if(NULL != (bonding_ptr=strstr(bonding_member_list[j].ifname, "eth"))){
													offset += snprintf(one_nic_info.nic_member+offset, sizeof(one_nic_info.nic_member)-offset, "%s,", bonding_ptr+strlen("eth"));
												}
											}
										}
									}
									free(bonding_member_list);
									bonding_member_list=NULL;
								}
							}
							g_nic_info=ptr;
							memcpy(&g_nic_info[effective_cnt], &one_nic_info, sizeof(QRU_NIC_INFO));
							effective_cnt++;
						}
					}
				}
			}
			free(bonding_list);
			bonding_list=NULL;
		}
	}
	g_nic_record_cnt=effective_cnt;

	ret=0;
myreturn:
	if(0 != ret){
		if(NULL != g_nic_info){
			free(g_nic_info);
			g_nic_info=NULL;
			g_nic_record_cnt=0;
		}
	}
	return ret;
}
#else
// clone from NasMgmt/HTTP/WebPage/UI_2.1/Management/chartReq.c
static char *get_wireless_ifname()
{
#ifdef WIRELESS
    FILE *fp=NULL;
    char buf[128], *ifname = NULL, *pt, *end;

    if((fp = fopen("/proc/net/wireless", "r")) == NULL)
    {
        //printf("open error\n");
        return NULL;
    }
    while(fgets(buf, sizeof(buf), fp) != NULL)
    {
        if(strstr(buf, "Inter") || strstr(buf, "face"))
            continue;
        pt = buf;
        while(*pt == ' ')
            pt++;
        end = strchr(pt, ':');
        *end = '\0';
        ifname = strdup(pt);
    }
	if(fp){
		fclose(fp);
		fp=NULL;
	}
    if(!ifname)
        return NULL;
    snprintf(buf,sizeof(buf), "/sbin/ifconfig %s", ifname);
    if((fp = popen(buf, "r")))
    {
        while(fgets(buf, sizeof(buf), fp))
        {
            if(strstr(buf, "UP"))
            {
                pclose(fp);
                return ifname;
            }
        }
    }
	if(fp){
		pclose(fp);
		fp=NULL;
	}
#endif
    return NULL;
}

static int get_nic_resource(time_t monitor_time)
{
	int		ret=-1,
			eth_count=Get_Profile_Integer("Network", "Interface Number", 0),
			i,
			total_group_number=NIC_Get_Total_Group_Num(),
			effective_cnt=0;
		
	char 	nic_name[32]={},
			buf[BUF_SIZE]={},
			*wifname=get_wireless_ifname(),
			ifname[NLK_NAME_LEN]={};

#ifdef QNAP_HAL_SUPPORT
	int tbtbr_id_ary[16]={},
		tbtbr_id_cnt=16;
	int tbtbr_cnt=1;
	char brname[32]={};
#endif
	NIC_Group_Conf nic_group_conf={};

	// Wifi
	if(NULL != wifname){
		QRU_NIC_INFO *ptr=NULL;
		QRU_NIC_INFO one_nic_info={};
		if(NULL == (ptr=(QRU_NIC_INFO*)realloc((void*)g_nic_info, (effective_cnt+1)*sizeof(QRU_NIC_INFO)))){
			goto myreturn;
		}
		snprintf(nic_name, sizeof(nic_name), "wlan0");
		one_nic_info.monitor_time=monitor_time;
		one_nic_info.nic_type=QRU_WLAN;
		strncpy(one_nic_info.nic_name, nic_name, sizeof(one_nic_info.nic_name)-1);
		strncpy(one_nic_info.nic_display_name, nic_name, sizeof(one_nic_info.nic_display_name)-1);
		strncpy(one_nic_info.nic_member, "", sizeof(one_nic_info.nic_member)-1);
		memset(buf, 0, sizeof(buf));
		Get_Private_Profile_String(nic_name, "RX", "0", buf, sizeof(buf), QRU_BANWIDTH_CONF);
		one_nic_info.nic_rx=atoll(buf)/BANDWIDTH_CACHE_EXPIRE;
		memset(buf, 0, sizeof(buf));
		Get_Private_Profile_String(nic_name, "TX", "0", buf, sizeof(buf), QRU_BANWIDTH_CONF);
		one_nic_info.nic_rx=atoll(buf)/BANDWIDTH_CACHE_EXPIRE;
		g_nic_info=ptr;
		memcpy(&g_nic_info[effective_cnt], &one_nic_info, sizeof(QRU_NIC_INFO));
		effective_cnt++;
		free(wifname);
		wifname=NULL;
	}

	// Ethernet, USBQuickAccess
	for (i=0; i<eth_count; i++){
		QRU_NIC_INFO *ptr=NULL;
		QRU_NIC_INFO one_nic_info={};
		if(NULL == (ptr=(QRU_NIC_INFO*)realloc((void*)g_nic_info, (effective_cnt+1)*sizeof(QRU_NIC_INFO)))){
			goto myreturn;
		}
		memset(nic_name, 0, sizeof(nic_name));
		snprintf(nic_name, sizeof(nic_name), "eth%d", i);
		one_nic_info.monitor_time=monitor_time;
		strncpy(one_nic_info.nic_name, nic_name, sizeof(one_nic_info.nic_name)-1);
		strncpy(one_nic_info.nic_display_name, nic_name, sizeof(one_nic_info.nic_display_name)-1);
		strncpy(one_nic_info.nic_member, "", sizeof(one_nic_info.nic_member)-1);
#ifdef QNAP_HAL_SUPPORT
		if(Net_NIC_Is_QA_Port(i+1)){			
			one_nic_info.nic_type=QRU_QAPORT;
		}else{
			one_nic_info.nic_type=QRU_ETHERNET;
		}
#else
		one_nic_info.nic_type=QRU_ETHERNET;
#endif
		memset(buf, 0, sizeof(buf));
		Get_Private_Profile_String(nic_name, "RX", "0", buf, sizeof(buf), QRU_BANWIDTH_CONF);
		one_nic_info.nic_rx=atoll(buf)/BANDWIDTH_CACHE_EXPIRE;
		memset(buf, 0, sizeof(buf));
		Get_Private_Profile_String(nic_name, "TX", "0", buf, sizeof(buf), QRU_BANWIDTH_CONF);
		one_nic_info.nic_tx=atoll(buf)/BANDWIDTH_CACHE_EXPIRE;
		g_nic_info=ptr;
		memcpy(&g_nic_info[effective_cnt], &one_nic_info, sizeof(QRU_NIC_INFO));
		effective_cnt++;
	}

#ifdef QNAP_HAL_SUPPORT
	// Thunderbolt Bridge
	if (TBT_Support() == 1) {
		tbtbr_cnt=TBT_Bridge_Enumerate(tbtbr_id_ary, tbtbr_id_cnt, NULL, NULL);
		for (i=0; i<tbtbr_cnt; i++){
			QRU_NIC_INFO *ptr=NULL;
			QRU_NIC_INFO one_nic_info={};
			if(NULL == (ptr=(QRU_NIC_INFO*)realloc((void*)g_nic_info, (effective_cnt+1)*sizeof(QRU_NIC_INFO)))){
				goto myreturn;
			}
			snprintf(brname, sizeof(brname), "tbtbr%d", i);
			one_nic_info.monitor_time=monitor_time;
			one_nic_info.nic_type=QRU_BRIDGE;
			strncpy(one_nic_info.nic_name, brname, sizeof(one_nic_info.nic_name)-1);
			strncpy(one_nic_info.nic_display_name, brname, sizeof(one_nic_info.nic_display_name)-1);
			strncpy(one_nic_info.nic_member, "", sizeof(one_nic_info.nic_member)-1);
			memset(buf, 0, sizeof(buf));
			Get_Private_Profile_String(brname, "RX", "0", buf, sizeof(buf), QRU_BANWIDTH_CONF);
			one_nic_info.nic_rx=atoll(buf)/BANDWIDTH_CACHE_EXPIRE;
			memset(buf, 0, sizeof(buf));
			Get_Private_Profile_String(brname, "TX", "0", buf, sizeof(buf), QRU_BANWIDTH_CONF);
			one_nic_info.nic_tx=atoll(buf)/BANDWIDTH_CACHE_EXPIRE;
			g_nic_info=ptr;
			memcpy(&g_nic_info[effective_cnt], &one_nic_info, sizeof(QRU_NIC_INFO));
			effective_cnt++;
		}
	}
#endif

	// Port trunking 
	for(i =0; i < total_group_number; i++){
		memset(&nic_group_conf, 0, sizeof(NIC_Group_Conf));
		NIC_Get_Group_Conf(i, &nic_group_conf);
		if(nic_group_conf.bond_conf.bond_mode != BOND_MODE_STANDALONE){
			QRU_NIC_INFO *ptr=NULL;
			QRU_NIC_INFO one_nic_info={};
			if(NULL == (ptr=(QRU_NIC_INFO*)realloc((void*)g_nic_info, (effective_cnt+1)*sizeof(QRU_NIC_INFO)))){
				goto myreturn;
			}
			memset(ifname, 0, sizeof(ifname));
			NIC_Get_Dev_By_Group_Index(i, ifname, sizeof(ifname));
			one_nic_info.monitor_time=monitor_time;
			one_nic_info.nic_type=QRU_BONDING;
			strncpy(one_nic_info.nic_name, ifname, sizeof(one_nic_info.nic_name)-1);
			strncpy(one_nic_info.nic_display_name, ifname, sizeof(one_nic_info.nic_display_name)-1);
			strncpy(one_nic_info.nic_member, nic_group_conf.bond_conf.member, sizeof(one_nic_info.nic_member)-1);
			memset(buf, 0, sizeof(buf));
			Get_Private_Profile_String(ifname, "RX", "0", buf, sizeof(buf), QRU_BANWIDTH_CONF);
			one_nic_info.nic_rx=atoll(buf)/BANDWIDTH_CACHE_EXPIRE;
			memset(buf, 0, sizeof(buf));
			Get_Private_Profile_String(ifname, "TX", "0", buf, sizeof(buf), QRU_BANWIDTH_CONF);
			one_nic_info.nic_tx=atoll(buf)/BANDWIDTH_CACHE_EXPIRE;
			g_nic_info=ptr;
			memcpy(&g_nic_info[effective_cnt], &one_nic_info, sizeof(QRU_NIC_INFO));
			effective_cnt++;
		}
	}
	g_nic_record_cnt=effective_cnt;

	ret=0;
myreturn:
	if(0 != ret){
		if(NULL != g_nic_info){
			free(g_nic_info);
			g_nic_info=NULL;
			g_nic_record_cnt=0;
		}
	}
	return ret;
}
#endif

static int get_pool_resource(time_t monitor_time)
{
	int		ret=-1,
			pool_id_ary[MAX_POOL_MEMBER]={},
			pool_count=1,
			cache_pool_id=SSDCacheGroup_Get_Pool_Id(0),
			i,
			effective_cnt=0;

	BLK_Perf	*blk_perfP=NULL;

	pool_count=Pool_Enumerate(pool_id_ary, MAX_POOL_MEMBER, NULL, NULL);
	for (i=0; i<pool_count; i++){
		QRU_POOL_INFO *ptr=NULL;
		QRU_POOL_INFO one_pool_info={};
		POOL_CONFIG pool_conf={};

		// refer to NasMgmt/HTTP/WebPage/UI_2.1/Disk/disk_manage.c
		if (Pool_Is_Reserved(pool_id_ary[i]) || Pool_Is_QNAP_Static(pool_id_ary[i]) || pool_id_ary[i] == cache_pool_id) {
			continue;
		}

		if(NULL == (blk_perfP=calloc(1, sizeof(BLK_Perf)))){
			goto myreturn;
		}

		if(0 != BLK_Get_Perf(3, pool_id_ary[i], blk_perfP)){
			goto myreturn;
		}

		if(0 != Pool_Get_Conf(pool_id_ary[i], &pool_conf)){
			goto myreturn;
		}

		if(NULL == (ptr=(QRU_POOL_INFO*)realloc((void*)g_pool_info, (effective_cnt+1)*sizeof(QRU_POOL_INFO)))){
			goto myreturn;
		}
		one_pool_info.monitor_time=monitor_time;
		one_pool_info.pool_id=pool_id_ary[i];
		strncpy(one_pool_info.pool_name, pool_conf.pool_name, sizeof(one_pool_info.pool_name)-1);
		one_pool_info.rd_iops=blk_perfP->rd_iops;
		one_pool_info.wr_iops=blk_perfP->wr_iops;
		one_pool_info.rd_latency=blk_perfP->rd_latency;
		one_pool_info.wr_latency=blk_perfP->wr_latency;
		one_pool_info.rd_throughput=blk_perfP->rd_throughput;
		one_pool_info.wr_throughput=blk_perfP->wr_throughput;
		g_pool_info=ptr;
		memcpy(&g_pool_info[effective_cnt], &one_pool_info, sizeof(QRU_POOL_INFO));
		effective_cnt++;

		if(NULL != blk_perfP){
			free(blk_perfP);
			blk_perfP=NULL;
		}
	}
	g_pool_record_cnt=effective_cnt;

	ret=0;
myreturn:
	if(0 != ret){
		if(NULL != blk_perfP){
			free(blk_perfP);
			blk_perfP=NULL;
		}
		if(NULL != g_pool_info){
			free(g_pool_info);
			g_pool_info=NULL;
			g_pool_record_cnt=0;
		}
	}
	return ret;
}

void get_global_resource(time_t monitor_time)
{	
	if(0 == get_sys_resource(monitor_time)){
		g_get_sys_res=1;
	}else{
		QRU_LOG_D("get_sys_resource() fail(%ld)\n", monitor_time);
	}

	if(0 == get_nic_resource(monitor_time)){
		g_get_nic_res=1;
	}else{
		QRU_LOG_D("get_nic_resource() fail(%ld)\n", monitor_time);
	}

	if(0 == get_pool_resource(monitor_time)){
		g_get_pool_res=1;
	}else{
		QRU_LOG_D("get_pool_resource() fail(%ld)\n", monitor_time);
	}

	int	i;
	QRU_LOG_D("CPU:\n");
	QRU_LOG_D("\t\tmonitor_time[%llu]\n", g_cpu_info->monitor_time);
	QRU_LOG_D("\t\tCPU Usage[%.1f]\n", g_cpu_info->cpu_usage);
	QRU_LOG_D("Memory:\n");
	QRU_LOG_D("\t\tmonitor_time[%llu]\n", g_mem_info->monitor_time);
	QRU_LOG_D("\t\tMemory Usage[%.1f]\n", g_mem_info->mem_usage);
	QRU_LOG_D("\t\tMemory Used[%u]\n", g_mem_info->mem_used);
	QRU_LOG_D("Process Record(%d):\n", g_proc_record_cnt);
	for(i=0; i<g_proc_record_cnt; i++){
		QRU_LOG_D("\tEffective Iterm(%d) start\n", i);
		QRU_LOG_D("\t\tmonitor_time[%llu]\n", g_proc_info[i].monitor_time);
		QRU_LOG_D("\t\tproc_name[%s]\n", g_proc_info[i].proc_name);
		QRU_LOG_D("\t\tproc_app_name[%s]\n", g_proc_info[i].proc_app_name);
		QRU_LOG_D("\t\tuser[%s]\n", g_proc_info[i].user);
		QRU_LOG_D("\t\tpid[%u]\n", g_proc_info[i].pid);
		QRU_LOG_D("\t\tproc_cpu_usage[%.1f]\n", g_proc_info[i].proc_cpu_usage);
		QRU_LOG_D("\t\tproc_mem_usage[%.1f]\n", g_proc_info[i].proc_mem_usage);
		QRU_LOG_D("\t\tproc_mem_used[%u]\n", g_proc_info[i].proc_mem_used);
		QRU_LOG_D("\t\tproc_tx[%llu]\n", g_proc_info[i].proc_tx);
		QRU_LOG_D("\t\tproc_rx[%llu]\n", g_proc_info[i].proc_rx);
		QRU_LOG_D("\tEffective Iterm(%d) end\n", i);
	}
	QRU_LOG_D("App Record(%d):\n", g_app_record_cnt);
	for(i=0; i<g_app_record_cnt; i++){
		QRU_LOG_D("\tEffective Iterm(%d) start\n", i);
		QRU_LOG_D("\t\tmonitor_time[%llu]\n", g_app_info[i].monitor_time);
		QRU_LOG_D("\t\tapp_name[%s]\n", g_app_info[i].app_name);
		QRU_LOG_D("\t\tapp_cpu_usage[%.1f]\n", g_app_info[i].app_cpu_usage);
		QRU_LOG_D("\t\tapp_mem_usage[%.1f]\n", g_app_info[i].app_mem_usage);
		QRU_LOG_D("\t\tapp_mem_used[%u]\n", g_app_info[i].app_mem_used);
		QRU_LOG_D("\t\tapp_proc_tx[%llu]\n", g_app_info[i].app_proc_tx);
		QRU_LOG_D("\t\tapp_proc_rx[%llu]\n", g_app_info[i].app_proc_rx);
		QRU_LOG_D("\tEffective Iterm(%d) end\n", i);
	}
	QRU_LOG_D("NIC(%d):\n", g_nic_record_cnt);
	for(i=0; i<g_nic_record_cnt; i++){
		QRU_LOG_D("\tEffective Iterm(%d) start\n", i);
		QRU_LOG_D("\t\tmonitor_time[%llu]\n", g_nic_info[i].monitor_time);
		QRU_LOG_D("\t\tnic_type[%d]\n", g_nic_info[i].nic_type);
		QRU_LOG_D("\t\tnic_name[%s]\n", g_nic_info[i].nic_name);
		QRU_LOG_D("\t\tnic_display_name[%s]\n", g_nic_info[i].nic_display_name);
		QRU_LOG_D("\t\tnic_member[%s]\n", g_nic_info[i].nic_member);
		QRU_LOG_D("\t\tnic_tx[%lld]\n", g_nic_info[i].nic_tx);
		QRU_LOG_D("\t\tnic_rx[%lld]\n", g_nic_info[i].nic_rx);
		QRU_LOG_D("\tEffective Iterm(%d) end\n", i);
	}
	QRU_LOG_D("POOL(%d):\n", g_pool_record_cnt);
	for(i=0; i<g_pool_record_cnt; i++){
		QRU_LOG_D("\tEffective Iterm(%d) start\n", i);
		QRU_LOG_D("\t\tmonitor_time[%llu]\n", g_pool_info[i].monitor_time);
		QRU_LOG_D("\t\tpool_id[%d]\n", g_pool_info[i].pool_id);
		QRU_LOG_D("\t\tpool_name[%s]\n", g_pool_info[i].pool_name);
		QRU_LOG_D("\t\trd_iops[%d]\n", g_pool_info[i].rd_iops);
		QRU_LOG_D("\t\twr_iops[%d]\n", g_pool_info[i].wr_iops);
		QRU_LOG_D("\t\trd_latency[%d]\n", g_pool_info[i].rd_latency);
		QRU_LOG_D("\t\twr_latency[%d]\n", g_pool_info[i].wr_latency);
		QRU_LOG_D("\t\trd_throughput[%d]\n", g_pool_info[i].rd_throughput);
		QRU_LOG_D("\t\twr_throughput[%d]\n", g_pool_info[i].wr_throughput);
		QRU_LOG_D("\tEffective Iterm(%d) end\n", i);
	}

	return;
}

static void cleanup_sys_resource()
{
	if(NULL != g_cpu_info){
		free(g_cpu_info);
		g_cpu_info=NULL;
	}
	if(NULL != g_mem_info){
		free(g_mem_info);
		g_mem_info=NULL;
	}
	if(NULL != g_proc_info){
		free(g_proc_info);
		g_proc_info=NULL;
		g_proc_record_cnt=0;
	}
	if(NULL != g_app_info){
		free(g_app_info);
		g_app_info=NULL;
		g_app_record_cnt=0;
	}
	return;
}

static void cleanup_nic_resource()
{
	if(NULL != g_nic_info){
		free(g_nic_info);
		g_nic_info=NULL;
		g_nic_record_cnt=0;
	}
	return;
}

static void cleanup_pool_resource()
{
	if(NULL != g_pool_info){
		free(g_pool_info);
		g_pool_info=NULL;
		g_pool_record_cnt=0;
	}
	return;
}

static void cleanup_global_resource()
{
	if(g_get_sys_res){
		cleanup_sys_resource();
		g_get_sys_res=0;
	}

	if(g_get_nic_res){
		cleanup_nic_resource();
		g_get_nic_res=0;
	}
	
	if(g_get_pool_res){
		cleanup_pool_resource();
		g_get_pool_res=0;
	}

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
		month_cnt=0;
	time_t last=time(0);
	while(1) {
		time_t 	now=time(0),
				monitor_start=0,
				monitor_end=0,
				monitor_diff=0;

		if(-TIME_DISPARITY > (now-last) || (now-last) > TIME_DISPARITY){
			QRU_LOG_D("time disparity of %ld minutes detected.([last(%ld)][now(%ld)][diff(%ld)])\n", (now-last) / 60, last, now, (now-last));
			// time disparity detected and do nothing...
			last=time(0);
			usleep(500000);
			continue;
		}
		if(now == last || (now % DAY_INTERVAL) != 0){
			usleep(500000);
			continue;
		}
		monitor_start=now;		
		get_global_resource(monitor_start);
		// write resource data to daily table
		QRU_LOG_D("Record Day[last(%ld)][now(%ld)][diff(%ld)]\n", last, now, (now-last));
		if(week_cnt == WEEK_INTERVAL / DAY_INTERVAL){
			// write resource data to weekly table
			QRU_LOG_D("Record Week[last(%ld)][now(%ld)][diff(%ld)]\n", last, now, (now-last));
			week_cnt=0;
		}
		if(month_cnt == MONTH_INTERVAL / DAY_INTERVAL){
			// write resource data to monthly table
			QRU_LOG_D("Record Month[last(%ld)][now(%ld)][diff(%ld)]\n", last, now, (now-last));
			month_cnt=0;	
		}
		cleanup_global_resource();
		monitor_end=(uint32_t)time(0);
		monitor_diff=monitor_end-monitor_start;

		if(monitor_diff < DAY_INTERVAL){
			sleep(DAY_INTERVAL-monitor_diff-1);
		}else{
			QRU_LOG_E("Miss DAY_INTERVAL(%d) [last(%ld)][now(%ld)][diff(%ld)][monitor_diff(%ld)]\n", DAY_INTERVAL, last, now, (now-last), monitor_diff);
		}

		last=now;
		week_cnt++;
		month_cnt++;
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

