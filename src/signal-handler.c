/*
** Copyright (C) 2009-2018 Quadrant Information Security <quadrantsec.com>
** Copyright (C) 2009-2018 Champ Clark III <cclark@quadrantsec.com>
**
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU General Public License Version 2 as
** published by the Free Software Foundation.  You may not use, modify or
** distribute this program under any other version of the GNU General
** Public License.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with this program; if not, write to the Free Software
** Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
*/

/* signal.c
 *
 * This runs as a thread for signal processing.
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"             /* From autoconf */
#endif

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <pthread.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <stdbool.h>
#include <errno.h>

#ifdef HAVE_SYS_PRCTL_H
#include <sys/prctl.h>
#endif

#include "version.h"

#include "sagan.h"
#include "sagan-defs.h"
#include "xbit-mmap.h"
#include "sagan-config.h"
#include "config-yaml.h"
#include "lockfile.h"
#include "signal-handler.h"
#include "stats.h"
#include "gen-msg.h"
#include "classifications.h"

#include "processors/perfmon.h"
#include "rules.h"
#include "ignore-list.h"
#include "flow.h"

#include "processors/blacklist.h"
#include "processors/track-clients.h"
#include "processors/bro-intel.h"

#ifdef HAVE_LIBLOGNORM
#include "liblognormalize.h"
#include <liblognorm.h>
int liblognorm_count;
#endif

#if defined(HAVE_DNET_H) || defined(HAVE_DUMBNET_H)
#include "output-plugins/unified2.h"
bool sagan_unified2_flag;
#endif

#ifdef HAVE_LIBMAXMINDDB
#include <maxminddb.h>
#include "geoip2.h"
#endif

struct _SaganCounters *counters;
struct _SaganDebug *debug;
struct _SaganConfig *config;
struct _Rule_Struct *rulestruct;
struct _Rules_Loaded *rules_loaded;
struct _Class_Struct *classstruct;
struct _Sagan_Processor_Generator *generator;
struct _Sagan_Blacklist *SaganBlacklist;
struct _Sagan_Track_Clients *SaganTrackClients;
struct _SaganVar *var;

struct _Sagan_Ignorelist *SaganIgnorelist;

struct _Sagan_BroIntel_Intel_Addr *Sagan_BroIntel_Intel_Addr;
struct _Sagan_BroIntel_Intel_Domain *Sagan_BroIntel_Intel_Domain;
struct _Sagan_BroIntel_Intel_File_Hash *Sagan_BroIntel_Intel_File_Hash;
struct _Sagan_BroIntel_Intel_URL *Sagan_BroIntel_Intel_URL;
struct _Sagan_BroIntel_Intel_Software *Sagan_BroIntel_Intel_Software;
struct _Sagan_BroIntel_Intel_Email *Sagan_BroIntel_Intel_Email;
struct _Sagan_BroIntel_Intel_User_Name *Sagan_BroIntel_Intel_User_Name;
struct _Sagan_BroIntel_Intel_File_Name *Sagan_BroIntel_Intel_File_Name;
struct _Sagan_BroIntel_Intel_Cert_Hash *Sagan_BroIntel_Intel_Cert_Hash;

pthread_mutex_t SaganReloadMutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t SaganReloadCond = PTHREAD_COND_INITIALIZER;

pthread_mutex_t SaganRulesLoadedMutex;

bool death;
int proc_running;

void Sig_Handler( void )
{

    (void)SetThreadName("SaganSignal");

    sigset_t signal_set;
    int sig;
    bool orig_perfmon_value = 0;

#ifdef HAVE_LIBPCAP
    bool orig_plog_value = 0;
#endif

    for(;;)
        {
            /* wait for any and all signals */
            sigfillset( &signal_set );
            sigwait( &signal_set, &sig );


            switch( sig )
                {
                /* exit */
                case SIGQUIT:
                case SIGINT:
                case SIGTERM:
                case SIGSEGV:
                case SIGABRT:


                    Sagan_Log(NORMAL, "\n\n[Received signal %d. Sagan version %s shutting down]-------\n", sig, VERSION);

                    /* This tells "new" threads to stop processing new data */

                    death=true;

                    /* We wait until there are no more running/processing threads
                       or until the thread space is zero.  We don't want to start
                       closing files, etc until everything has settled. */

                    while( proc_running != 0 || config->max_processor_threads == 0 )
                        {
                            Sagan_Log(WARN, "Waiting on %d working thread(s)....", proc_running);
                            sleep(1);
                        }

                    Statistics();

#if defined(HAVE_DNET_H) || defined(HAVE_DUMBNET_H)

                    if ( sagan_unified2_flag )
                        {
                            Unified2CleanExit();
                        }

#endif

#ifdef HAVE_LIBMAXMINDDB

                    MMDB_close(&config->geoip2);

#endif

                    if ( config->eve_flag == true )
                        {

                            fflush(config->eve_stream);
                            fclose(config->eve_stream);

                        }


                    if ( config->alert_flag == true )
                        {

                            fflush(config->sagan_alert_stream);
                            fclose(config->sagan_alert_stream);             /* Close Sagan alert file */

                        }

                    if ( config->fast_flag == true )
                        {

                            fflush(config->sagan_fast_stream);
                            fclose(config->sagan_fast_stream);

                        }

                    fflush(config->sagan_log_stream);               /* Close the sagan.log */
                    fclose(config->sagan_log_stream);

                    /* IPC Shared Memory */

                    File_Unlock(config->shm_counters);

                    if ( close(config->shm_counters) != 0 )
                        {
                            Sagan_Log(WARN, "[%s, line %d] Cannot close IPC counters! [%s]", __FILE__, __LINE__, strerror(errno));
                        }

                    File_Unlock(config->shm_xbit);

                    if ( close(config->shm_xbit) != 0 )
                        {
                            Sagan_Log(WARN, "[%s, line %d] Cannot close IPC xbit! [%s]", __FILE__, __LINE__, strerror(errno));
                        }

                    File_Unlock(config->shm_thresh_by_src);

                    if ( close(config->shm_thresh_by_src) != 0 )
                        {
                            Sagan_Log(WARN, "[%s, line %d] Cannot close IPC thresh_by_src! [%s]", __FILE__, __LINE__, strerror(errno));
                        }

                    File_Unlock(config->shm_thresh_by_dst);

                    if ( close(config->shm_thresh_by_dst) != 0 )
                        {
                            Sagan_Log(WARN, "[%s, line %d] Cannot close IPC thresh_by_dst! [%s]", __FILE__, __LINE__, strerror(errno));
                        }

                    File_Unlock(config->shm_thresh_by_username);

                    if ( close(config->shm_thresh_by_username) != 0 )
                        {
                            Sagan_Log(WARN, "[%s, line %d] Cannot close IPC thresh_by_username! [%s]", __FILE__, __LINE__, strerror(errno));
                        }

                    File_Unlock(config->shm_after_by_src);

                    if ( close(config->shm_after_by_src) != 0 )
                        {
                            Sagan_Log(WARN, "[%s, line %d] Cannot close IPC after_by_src! [%s]", __FILE__, __LINE__, strerror(errno));
                        }

                    File_Unlock(config->shm_after_by_dst);

                    if ( close(config->shm_after_by_dst) != 0 )
                        {
                            Sagan_Log(WARN, "[%s, line %d] Cannot close IPC after_by_dst! [%s]", __FILE__, __LINE__, strerror(errno));
                        }

                    File_Unlock(config->shm_after_by_username);

                    if ( close(config->shm_after_by_username) != 0 )
                        {
                            Sagan_Log(WARN, "[%s, line %d] Cannot close IPC after_by_username! [%s]", __FILE__, __LINE__, strerror(errno));
                        }

                    if ( config->sagan_track_clients_flag )
                        {

                            File_Unlock(config->shm_track_clients);

                            if ( close(config->shm_track_clients) != 0 )
                                {
                                    Sagan_Log(WARN, "[%s, line %d] Cannot close IPC _Sagan_Track_Clients! [%s]", __FILE__, __LINE__, strerror(errno));
                                }

                        }


                    if ( config->perfmonitor_flag )
                        {
                            Sagan_Perfmonitor_Close();
                        }

                    Remove_Lock_File();
                    exit(0);
                    break;

                case SIGHUP:

                    config->sagan_reload = 1;				/* Only this thread can alter this */

                    pthread_mutex_lock(&SaganReloadMutex);

                    Sagan_Log(NORMAL, "[Reloading Sagan version %s.]-------", VERSION);

                    /*
                    * Close and re-open log files.  This is for logrotate and such
                    * 04/14/2015 - Champ Clark III (cclark@quadrantsec.com)
                    */

                    Open_Log_File(REOPEN, ALL_LOGS);

                    /******************/
                    /* Reset counters */
                    /******************/

                    counters->refcount=0;
                    counters->classcount=0;
                    counters->rulecount=0;
                    counters->ruletotal=0;
                    counters->genmapcount=0;
                    counters->rules_loaded_count=0;
                    counters->var_count=0;

                    memset(rules_loaded, 0, sizeof(_Rules_Loaded));
                    memset(rulestruct, 0, sizeof(_Rule_Struct));
                    memset(classstruct, 0, sizeof(_Class_Struct));
                    memset(generator, 0, sizeof(_Sagan_Processor_Generator));
                    memset(var, 0, sizeof(_SaganVar));

                    /**********************************/
                    /* Disabled and reset processors. */
                    /**********************************/

                    /* Note: Processors that run as there own thread (perfmon, plog) cannot be
                     * loaded via SIGHUP.  They must be loaded at run time.  Once they are loaded,
                     * they can be disabled/re-enabled. */

                    /* Single Threaded processors */

                    if ( config->perfmonitor_flag == 1 && orig_perfmon_value == 0 )
                        {
                            Sagan_Perfmonitor_Close();
                            orig_perfmon_value = 1;
                        }

                    config->perfmonitor_flag = 0;

#ifdef HAVE_LIBPCAP

                    if ( config->plog_flag )
                        {
                            orig_plog_value = 1;
                        }

                    config->plog_flag = 0;
#endif

                    /* Multi Threaded processors */

                    config->blacklist_flag = 0;

                    if ( config->blacklist_flag )
                        {
                            free(SaganBlacklist);
                        }

                    config->blacklist_flag = 0;

                    if ( config->brointel_flag )
                        {
                            free(Sagan_BroIntel_Intel_Addr);
                            free(Sagan_BroIntel_Intel_Domain);
                            free(Sagan_BroIntel_Intel_File_Hash);
                            free(Sagan_BroIntel_Intel_URL);
                            free(Sagan_BroIntel_Intel_Software);
                            free(Sagan_BroIntel_Intel_Email);
                            free(Sagan_BroIntel_Intel_User_Name);
                            free(Sagan_BroIntel_Intel_File_Name);
                            free(Sagan_BroIntel_Intel_Cert_Hash);

                            counters->brointel_addr_count = 0;
                            counters->brointel_domain_count = 0;
                            counters->brointel_file_hash_count = 0;
                            counters->brointel_url_count = 0;
                            counters->brointel_software_count = 0;
                            counters->brointel_email_count = 0;
                            counters->brointel_user_name_count = 0;
                            counters->brointel_file_name_count = 0;
                            counters->brointel_cert_hash_count = 0;
                            counters->brointel_dups = 0;
                        }

                    config->brointel_flag = 0;

                    if ( config->sagan_track_clients_flag )
                        {

                            free(SaganTrackClients);

                        }

                    /* Output formats */

                    config->sagan_external_output_flag = 0;

#ifdef WITH_SYSLOG
                    config->sagan_syslog_flag = 0;
#endif


#ifdef HAVE_LIBESMTP
                    config->sagan_esmtp_flag = 0;
#endif

#ifdef WITH_SNORTSAM
                    config->sagan_fwsam_flag = 0;
#endif

                    /* Non-output / Processors */

                    if ( config->sagan_droplist_flag )
                        {
                            config->sagan_droplist_flag = 0;
                            free(SaganIgnorelist);
                        }

                    /************************************************************/
                    /* Re-load primary configuration (rules/classifictions/etc) */
                    /************************************************************/

                    pthread_mutex_lock(&SaganRulesLoadedMutex);
                    Load_YAML_Config(config->sagan_config);	/* <- RELOAD */
                    pthread_mutex_unlock(&SaganRulesLoadedMutex);

                    /************************************************************/
                    /* Re-load primary configuration (rules/classifictions/etc) */
                    /************************************************************/

                    if ( config->perfmonitor_flag == 1 )
                        {
                            if ( orig_perfmon_value == 1 )
                                {
                                    Sagan_Perfmonitor_Open();
                                }
                            else
                                {
                                    Sagan_Log(WARN, "** 'perfmonitor' must be loaded at runtime! NOT loading 'perfmonitor'!");
                                    config->perfmonitor_flag = 0;
                                }
                        }


#ifdef HAVE_LIBPCAP

                    if ( config->plog_flag == 1 )
                        {
                            if ( orig_plog_value == 1 )
                                {
                                    config->plog_flag = 1;
                                }
                            else
                                {
                                    Sagan_Log(WARN, "** 'plog' must be loaded at runtime! NOT loading 'plog'!");
                                    config->plog_flag = 0;
                                }
                        }
#endif

                    /* Load Blacklist data */

                    if ( config->blacklist_flag )
                        {
                            counters->blacklist_count=0;
                            Sagan_Blacklist_Init();
                            Sagan_Blacklist_Load();
                        }

                    if ( config->brointel_flag )
                        {
                            Sagan_BroIntel_Init();
                            Sagan_BroIntel_Load_File();
                        }

                    if ( config->sagan_track_clients_flag )
                        {
                            Sagan_Log(NORMAL, "Reset Sagan Track Client.");
                        }


                    /* Non output / processors */

                    if ( config->sagan_droplist_flag )
                        {
                            Load_Ignore_List();
                            Sagan_Log(NORMAL, "Loaded %d ignore/drop list item(s).", counters->droplist_count);
                        }

#ifdef HAVE_LIBMAXMINDDB
                    Sagan_Log(NORMAL, "Reloading GeoIP2 data.");
                    Open_GeoIP2_Database();
#endif

                    pthread_cond_signal(&SaganReloadCond);
                    pthread_mutex_unlock(&SaganReloadMutex);

                    config->sagan_reload = 0;

                    Sagan_Log(NORMAL, "Configuration reloaded.");
                    break;

                /* Signals to ignore */
                case 17:		/* Child process has exited. */
                case 28:		/* Terminal 'resize'/alarm. */
                    break;

                case SIGUSR1:
                    Statistics();
                    break;

                default:
                    Sagan_Log(NORMAL, "[Received signal %d. Sagan doesn't know how to deal with]", sig);
                }
        }
}

