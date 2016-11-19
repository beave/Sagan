/*
** Copyright (C) 2009-2016 Quadrant Information Security <quadrantsec.com>
** Copyright (C) 2009-2016 Champ Clark III <cclark@quadrantsec.com>
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

/* sagan-config.c
 *
 * Loads the sagan.conf file into memory
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"             /* From autoconf */
#endif

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>
#include <pthread.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <string.h>
#include <getopt.h>
#include <time.h>
#include <signal.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <math.h>
#include <syslog.h>
#include <stdbool.h>

#ifdef HAVE_LIBLOGNORM
#include <liblognorm.h>
#include "sagan-liblognorm.h"
#endif

#include "version.h"
#include "sagan.h"
#include "sagan-defs.h"
#include "sagan-rules.h"
#include "sagan-config.h"
#include "sagan-classifications.h"
#include "sagan-gen-msg.h"
#include "sagan-protocol-map.h"
#include "sagan-references.h"
#include "parsers/parsers.h"

#ifdef HAVE_LIBMAXMINDDB
#include "sagan-geoip2.h"
#endif

/* Processors */

#include "processors/sagan-blacklist.h"
#include "processors/sagan-perfmon.h"
#include "processors/sagan-bro-intel.h"

#ifdef WITH_BLUEDOT
#include "processors/sagan-bluedot.h"
#endif


#if defined(HAVE_DNET_H) || defined(HAVE_DUMBNET_H)
#include "output-plugins/sagan-unified2.h"
#endif

#ifdef HAVE_LIBLOGNORM
struct liblognorm_struct *liblognormstruct;
int liblognorm_count;
#endif

pthread_mutex_t SaganRulesLoadedMutex;	/* Used when reloading configuration/rules */
sbool reload_rules;

sbool liblognorm_load = 0;		/* Need to stay outside of HAVE_LIBLOGNORM */

struct _Rule_Struct *rulestruct;
struct _Rules_Loaded *rules_loaded;
struct _SaganCounters *counters;
struct _SaganDebug *debug;
struct _SaganConfig *config;
struct _SaganVar *var;


void Load_Config( void )
{

    struct stat filecheck;

    FILE *sagancfg;
    FILE *tmpcfg;

    char *filename;
    char ruleset[MAXPATH];

    char tmpbuf[CONFBUF];
    char tmpbuf2[CONFBUF];
    char tmpbuf3[CONFBUF];
    char tmpbuf4[CONFBUF];
    char tmpstring[CONFBUF];

    char *sagan_option=NULL;
    char *sagan_var1=NULL;
    char *sagan_var2=NULL;
    char *sagan_var3=NULL;
    char *ptmp=NULL;

    char *tok=NULL;
    char *tok2=NULL;
    char *tok3=NULL;

    int i,check;



#ifdef HAVE_LIBLOGNORM
#endif

    /* Set some system defaults */

    strlcpy(config->sagan_alert_filepath, ALERTLOG, sizeof(config->sagan_alert_filepath));
    strlcpy(config->sagan_lockfile, LOCKFILE, sizeof(config->sagan_lockfile));
    strlcpy(config->sagan_log_path, SAGANLOGPATH, sizeof(config->sagan_log_path));
    strlcpy(config->sagan_rule_path, RULE_PATH, sizeof(config->sagan_rule_path));
    strlcpy(config->ipc_directory, IPC_DIRECTORY, sizeof(config->ipc_directory));
    strlcpy(config->external_net, EXTERNAL_NET, sizeof(config->external_net));
    strlcpy(config->home_net, HOME_NET, sizeof(config->home_net));


    config->sagan_host[0] = '\0';
    config->sagan_port = 514;

    config->max_xbits = DEFAULT_IPC_XBITS;

    config->max_threshold_by_src = DEFAULT_IPC_THRESH_BY_SRC;
    config->max_threshold_by_dst = DEFAULT_IPC_THRESH_BY_DST;
    config->max_threshold_by_srcport = DEFAULT_IPC_THRESH_BY_SRC_PORT;
    config->max_threshold_by_dstport = DEFAULT_IPC_THRESH_BY_DST_PORT;
    config->max_threshold_by_username = DEFAULT_IPC_THRESH_BY_USERNAME;

    config->max_after_by_src = DEFAULT_IPC_AFTER_BY_SRC;
    config->max_after_by_dst = DEFAULT_IPC_AFTER_BY_DST;
    config->max_after_by_srcport = DEFAULT_IPC_AFTER_BY_SRC_PORT;
    config->max_after_by_dstport = DEFAULT_IPC_AFTER_BY_DST_PORT;
    config->max_after_by_username = DEFAULT_IPC_AFTER_BY_USERNAME;

    config->max_track_clients = DEFAULT_IPC_CLIENT_TRACK_IPC;

#if defined(F_GETPIPE_SZ) && defined(F_SETPIPE_SZ)
    config->sagan_fifo_size = MAX_FIFO_SIZE;
#endif

    /* Copy default FIFO */

    if ( config->sagan_is_file == 0 ) {
        strlcpy(config->sagan_fifo, FIFO, sizeof(config->sagan_fifo));
    }


#ifdef HAVE_LIBESMTP
    strlcpy(config->sagan_email_subject, DEFAULT_SMTP_SUBJECT, sizeof(config->sagan_email_subject));
#endif

    config->sagan_proto = 17;		/* Default to UDP */
    config->max_processor_threads = MAX_PROCESSOR_THREADS;

    /* PLOG defaults */

#ifdef HAVE_LIBPCAP
    strlcpy(config->plog_interface, PLOG_INTERFACE, sizeof(config->plog_interface));
    strlcpy(config->plog_filter, PLOG_FILTER, sizeof(config->plog_filter));
    strlcpy(config->plog_logdev, PLOG_LOGDEV, sizeof(config->plog_logdev));
#endif

    /* Start loading configuration */

    rulestruct = (_Rule_Struct *) realloc(rulestruct, (counters->rulecount+1) * sizeof(_Rule_Struct));

    if ( rulestruct == NULL ) {
        Sagan_Log(S_ERROR, "[%s, line %d] Failed to reallocate memory for rulestruct. Abort!", __FILE__, __LINE__);
    }

    /* Gather information for the master configuration file */


    if ((sagancfg = fopen(config->sagan_config, "r")) == NULL) {
        fprintf(stderr, "[%s, line %d] Cannot open configuration file (%s)\n", __FILE__,  __LINE__, config->sagan_config);
        exit(1);
    }

    while(fgets(tmpbuf, sizeof(tmpbuf), sagancfg) != NULL) {
        if (tmpbuf[0] == '#') continue;
        if (tmpbuf[0] == ';') continue;
        if (tmpbuf[0] == 10 ) continue;
        if (tmpbuf[0] == 32 ) continue;

        strlcpy(tmpbuf2, tmpbuf, sizeof(tmpbuf2));	/* Retain a copy of the entire line */

        sagan_option = strtok_r(tmpbuf, " ", &tok);

        if (!strcmp(Remove_Return(sagan_option), "max_processor_threads")) {
            sagan_var1 = strtok_r(NULL, " ", &tok);

            if ( sagan_var1 == NULL ) {
                Sagan_Log(S_ERROR, "[%s, line %d] \"max_processor_threads\" is incomplete!", __FILE__, __LINE__);
            }

            config->max_processor_threads = strtoull(sagan_var1, NULL, 10);
        }

        if (!strcmp(Remove_Return(sagan_option), "disable_dns_warnings")) {
            Sagan_Log(S_NORMAL, "Supressing DNS warnings");
            config->disable_dns_warnings = 1;
        }

        if (!strcmp(Remove_Return(sagan_option), "syslog_src_lookup")) {
            Sagan_Log(S_NORMAL, "DNS lookup of source address supplied by syslog daemon");
            config->syslog_src_lookup = 1;
        }

        if (!strcmp(Remove_Return(sagan_option), "sagan_host")) {
            sagan_var1 = Remove_Return(strtok_r(NULL, " ", &tok));

            if ( sagan_var1 == NULL ) {
                Sagan_Log(S_ERROR, "[%s, line %d] \"sagan_host\" is incomplete!", __FILE__, __LINE__);
            }

            strlcpy(config->sagan_host, sagan_var1, sizeof(config->sagan_host));
        }

        if (!strcmp(sagan_option, "sagan_port")) {
            sagan_var1 = strtok_r(NULL, " ", &tok);

            if ( sagan_var1 == NULL ) {
                Sagan_Log(S_ERROR, "[%s, line %d] \"sagan_port\" is incomplete!", __FILE__, __LINE__);
            }

            config->sagan_port = atoi(sagan_var1);
        }

        /* IPC configurations! */

        if (!strcmp(sagan_option, "ipc_directory")) {
            sagan_var1 = Remove_Return(strtok_r(NULL, " ", &tok));

            if ( sagan_var1 == NULL ) {
                Sagan_Log(S_ERROR, "[%s, line %d] \"ipc_directory\" is incomplete!", __FILE__, __LINE__);
            }

            strlcpy(config->ipc_directory, sagan_var1, sizeof(config->ipc_directory));
        }


        if (!strcmp(sagan_option, "flowbits") || !strcmp(sagan_option, "xbits")) {

            if (!strcmp(sagan_option, "flowbits")) {
                Sagan_Log(S_WARN, "[%s, line %d] Depreciated \"flowbits\" in use.  Use \"xbits\".", __FILE__, __LINE__);
            }

            sagan_var1 = strtok_r(NULL, " ", &tok);

            if ( sagan_var1 == NULL ) {
                Sagan_Log(S_ERROR, "[%s, line %d] \"xbits\" is incomplete!", __FILE__, __LINE__);
            }

            config->max_xbits = atoi(sagan_var1);
        }


        if (!strcmp(sagan_option, "threshold_by_src")) {
            sagan_var1 = strtok_r(NULL, " ", &tok);

            if ( sagan_var1 == NULL ) {
                Sagan_Log(S_ERROR, "[%s, line %d] \"threshold_by_src\" is incomplete!", __FILE__, __LINE__);
            }

            config->max_threshold_by_src = atoi(sagan_var1);
        }


        if (!strcmp(sagan_option, "threshold_by_dst")) {
            sagan_var1 = strtok_r(NULL, " ", &tok);

            if ( sagan_var1 == NULL ) {
                Sagan_Log(S_ERROR, "[%s, line %d] \"threshold_by_dst\" is incomplete!", __FILE__, __LINE__);
            }

            config->max_threshold_by_dst = atoi(sagan_var1);
        }


        if (!strcmp(sagan_option, "threshold_by_dstport")) {
            sagan_var1 = strtok_r(NULL, " ", &tok);

            if ( sagan_var1 == NULL ) {
                Sagan_Log(S_ERROR, "[%s, line %d] \"threshold_by_dstport\" is incomplete!", __FILE__, __LINE__);
            }

            config->max_threshold_by_dstport = atoi(sagan_var1);
        }


        if (!strcmp(sagan_option, "threshold_by_username")) {
            sagan_var1 = strtok_r(NULL, " ", &tok);

            if ( sagan_var1 == NULL ) {
                Sagan_Log(S_ERROR, "[%s, line %d] \"threshold_by_username\" is incomplete!", __FILE__, __LINE__);
            }

            config->max_threshold_by_username = atoi(sagan_var1);
        }


        if (!strcmp(sagan_option, "after_by_username")) {
            sagan_var1 = strtok_r(NULL, " ", &tok);

            if ( sagan_var1 == NULL ) {
                Sagan_Log(S_ERROR, "[%s, line %d] \"after_by_username\" is incomplete!", __FILE__, __LINE__);
            }

            config->max_after_by_username = atoi(sagan_var1);
        }


        if (!strcmp(sagan_option, "after_by_src")) {
            sagan_var1 = strtok_r(NULL, " ", &tok);

            if ( sagan_var1 == NULL ) {
                Sagan_Log(S_ERROR, "[%s, line %d] \"after_by_src\" is incomplete!", __FILE__, __LINE__);
            }

            config->max_after_by_src = atoi(sagan_var1);
        }


        if (!strcmp(sagan_option, "after_by_dst")) {
            sagan_var1 = strtok_r(NULL, " ", &tok);

            if ( sagan_var1 == NULL ) {
                Sagan_Log(S_ERROR, "[%s, line %d] \"after_by_dst\" is incomplete!", __FILE__, __LINE__);
            }

            config->max_after_by_dst = atoi(sagan_var1);
        }


        if (!strcmp(sagan_option, "after_by_dstport")) {
            sagan_var1 = strtok_r(NULL, " ", &tok);

            if ( sagan_var1 == NULL ) {
                Sagan_Log(S_ERROR, "[%s, line %d] \"after_by_dstport\" is incomplete!", __FILE__, __LINE__);
            }

            config->max_after_by_dstport = atoi(sagan_var1);
        }


        if (!strcmp(sagan_option, "track_clients")) {
            sagan_var1 = strtok_r(NULL, " ", &tok);

            if ( sagan_var1 == NULL ) {
                Sagan_Log(S_ERROR, "[%s, line %d] \"track_clients\" is incomplete!", __FILE__, __LINE__);
            }

            config->max_track_clients = atoi(sagan_var1);
        }


#if defined(F_GETPIPE_SZ) && defined(F_SETPIPE_SZ)

        if (!strcmp(sagan_option, "sagan_fifo_size")) {
            sagan_var1 = strtok_r(NULL, " ", &tok);

            if ( sagan_var1 == NULL ) {
                Sagan_Log(S_ERROR, "[%s, line %d] \"sagan_fifo_size\" is incomplete!", __FILE__, __LINE__);
            }

            config->sagan_fifo_size = atoi(sagan_var1);
        }
#endif


#ifndef HAVE_LIBESMTP
        if (!strcmp(sagan_option, "send-to") || !strcmp(sagan_option, "min_email_priority")) {
            Sagan_Log(S_ERROR, "[%s, line %d] \"libesmtp\" support not found. Re-compile with ESMTP support or disable in the sagan.conf.", __FILE__, __LINE__);
        }

#endif

#ifdef HAVE_LIBESMTP

        if (!strcmp(sagan_option, "send-to")) {
            sagan_var1 = strtok_r(NULL, " ", &tok);

            if ( sagan_var1 == NULL ) {
                Sagan_Log(S_ERROR, "[%s, line %d] \"send-to\" is incomplete!", __FILE__, __LINE__);
            }

            strlcpy(config->sagan_esmtp_to, Remove_Return(sagan_var1), sizeof(config->sagan_esmtp_to));
            config->sagan_esmtp_flag=1;
            config->sagan_sendto_flag=1;
        }

        if (!strcmp(sagan_option, "min_email_priority")) {
            sagan_var1 = strtok_r(NULL, " ", &tok);

            if ( sagan_var1 == NULL ) {
                Sagan_Log(S_ERROR, "[%s, line %d] \"min_email_priority\" is incomplete!", __FILE__, __LINE__);
            }

            config->min_email_priority = atoi(sagan_var1);
        }

        if (!strcmp(sagan_option, "email_subject")) {
            sagan_var1 = strtok_r(NULL, " ", &tok);

            if ( sagan_var1 == NULL ) {
                Sagan_Log(S_ERROR, "[%s, line %d] \email_subject\" is incomplete!", __FILE__, __LINE__);
            }

            strlcpy(config->sagan_email_subject, Remove_Return(Between_Quotes(tmpbuf2)), sizeof(config->sagan_email_subject));
        }

#endif

#ifndef HAVE_LIBPCAP
        if (!strcmp(sagan_option, "plog_interface") || !strcmp(sagan_option, "plog_logdev") || !strcmp(sagan_option, "plog_port")) {
            Sagan_Log(S_ERROR, "[%s, line %d] \"libpcap\" support not found. Re-compile with PCAP support or disable in the sagan.conf.", __FILE__, __LINE__);
        }
#endif

#ifdef HAVE_LIBPCAP

        if (!strcmp(sagan_option, "plog_interface")) {
            strlcpy(config->plog_interface, Remove_Return(strtok_r(NULL, " ", &tok)), sizeof(config->plog_interface));
            config->plog_flag=1;
        }

        if (!strcmp(sagan_option, "plog_logdev")) {
            strlcpy(config->plog_logdev, Remove_Return(strtok_r(NULL, " ", &tok)), sizeof(config->plog_logdev));
            config->plog_flag=1;
        }

        if (!strcmp(sagan_option, "plog_filter")) {
            strlcpy(config->plog_filter, Remove_Return(Between_Quotes(tmpbuf2)), sizeof(config->plog_filter));
            config->plog_flag = 1;
        }

        if (!strcmp(sagan_option, "plog_promiscuous")) {
            config->plog_promiscuous = 1;
            config->plog_flag = 1;
        }

#endif

        /* Detect and abort on deperciated "normalize" value */

        if (!strcmp(sagan_option, "normalize:")) {
            Sagan_Log(S_ERROR, "[%s, line %d] Usage of deperciated 'normalize:' is no longer supported. Use 'normalize_file:' instead!", __FILE__, __LINE__);
        }

#ifndef HAVE_LIBLOGNORM


        if (!strcmp(sagan_option, "normalize_file:")) {
            liblognorm_load = true;

            Sagan_Log(S_WARN, "WARNING: Sagan was not compiled with \"liblognorm\" support!");
            Sagan_Log(S_WARN, "WARNING: Sagan will continue,  but _without_ liblognorm!");
        }
#endif

#ifdef HAVE_LIBLOGNORM

        if (!strcmp(sagan_option, "normalize_file:")) {

            liblognorm_load = true;

            liblognormstruct = (liblognorm_struct *) realloc(liblognormstruct, (liblognorm_count+1) * sizeof(liblognorm_struct));

            if ( liblognormstruct == NULL ) {
                Sagan_Log(S_ERROR, "[%s, line %d] Failed to reallocate memory for rulestruct. Abort!", __FILE__, __LINE__);
            }

            strlcpy(tmpstring, Remove_Return(strtok_r(NULL, ":", &tok)), sizeof(tmpstring));
            strlcpy(ruleset, Sagan_Var_To_Value(tmpstring), sizeof(ruleset));
            Remove_Spaces(ruleset);

            Sagan_Liblognorm_Load(ruleset);

        }

#endif

#ifndef HAVE_LIBMAXMINDDB
        if (!strcmp(sagan_option, "country_database")) {
            Sagan_Log(S_WARN, "WARNING: Sagan was not compiled with Maxmind \"GeoIP2\" support!");
            Sagan_Log(S_WARN, "WARNING: Sagan will continue,  but _without_ GeoIP2 enabled!");
        }
#endif

#ifdef HAVE_LIBMAXMINDDB
        if (!strcmp(sagan_option, "country_database:")) {
            sagan_var1 = Remove_Return(strtok_r(NULL, " ", &tok));

            if ( sagan_var1 == NULL ) {
                Sagan_Log(S_ERROR, "[%s, line %d] country_database: is missing country codes!", __FILE__, __LINE__);
            }

            strlcpy(config->geoip2_country_file, sagan_var1, sizeof(config->geoip2_country_file));
            Sagan_Log(S_NORMAL, "Loading GeoIP2 database. [%s]", config->geoip2_country_file);
            Sagan_Open_GeoIP2_Database();
            config->have_geoip2 = 1;
        }
#endif

        if (!strcmp(sagan_option, "ignore_list:")) {
            sagan_var1 = Remove_Return(strtok_r(NULL, " ", &tok));

            if ( sagan_var1 == NULL ) {
                Sagan_Log(S_ERROR, "[%s, line %d] No \"ignore file\" specified in the sagan.conf file!", __FILE__, __LINE__);
            }

            config->sagan_droplist_flag = 1;
            strlcpy(config->sagan_droplistfile, sagan_var1, sizeof(config->sagan_droplistfile));
        }

        /****************************************************************************
         * Processors
         ****************************************************************************/

        if (!strcmp(sagan_option, "processor")) {

            sagan_var1 = strtok_r(NULL," ", &tok);

            /******* Dynamic rule loading & reporting *******/

            if (!strcmp(sagan_var1, "dynamic_load:")) {

                ptmp = sagan_var1;

                while (ptmp != NULL) {

                    if (!strcmp(ptmp, "sample_rate")) {

                        ptmp = strtok_r(NULL," ", &tok);
                        Remove_Return(ptmp);

                        if ( ptmp == NULL ) {
                            Sagan_Log(S_ERROR, "[%s, line %d] 'dynamic_load' processor has an invalid or missing 'sample_rate'!", __FILE__, __LINE__);
                        }

                        config->dynamic_load_sample_rate = atoi(ptmp);
                    }

                    if (!strcmp(ptmp, "type")) {

                        ptmp = strtok_r(NULL," ", &tok);
                        Remove_Return(ptmp);

                        if ( ptmp == NULL ) {
                            Sagan_Log(S_ERROR, "[%s, line %d] 'dynamic_load' processor has an invalid or missing 'type'!", __FILE__, __LINE__);
                        }

                        if (!strcmp(ptmp, "dynamic_load")) {
                            config->dynamic_load_type = 0;
                        }

                        else if (!strcmp(ptmp, "log_only")) {
                            config->dynamic_load_type = 1;
                        }

                        else if (!strcmp(ptmp, "alert")) {
                            config->dynamic_load_type = 2;
                        }

                    }

                    ptmp = strtok_r(NULL, "=", &tok);
                }
            }

            /******* Client tracker *******/

            if (!strcmp(sagan_var1, "sagan-track-clients:")) {

                /* Set defaults */

                config->pp_sagan_track_clients = TRACK_TIME;

                config->sagan_track_clients_flag = 1;

                ptmp = sagan_var1;

                while (ptmp != NULL) {

                    if (!strcmp(ptmp, "client_timeout")) {
                        ptmp = strtok_r(NULL," ", &tok);
                        config->pp_sagan_track_clients = atoi(ptmp);
                    }

                    ptmp = strtok_r(NULL, "=", &tok);
                }
            }


            /******* Perfmon ********/

            if (!strcmp(sagan_var1, "perfmonitor:")) {

                config->perfmonitor_time = 0;
                config->perfmonitor_file_name[0] = '\0';

                config->perfmonitor_flag = 1;

                ptmp = sagan_var1;

                while (ptmp != NULL ) {

                    if (!strcmp(ptmp, "time")) {
                        ptmp = strtok_r(NULL, " ", &tok);
                        config->perfmonitor_time = atoi(ptmp);
                    }

                    if (!strcmp(ptmp, "file")) {
                        ptmp = strtok_r(NULL," ", &tok);
                        strlcpy(config->perfmonitor_file_name, Remove_Return(ptmp), sizeof(config->perfmonitor_file_name));
                    }

                    ptmp = strtok_r(NULL, "=", &tok);
                }


                /* TODO:  Do these same sanity checks on other processors! */

                if ( config->perfmonitor_time == 0 || config->perfmonitor_file_name[0] == '\0' ) {
                    Sagan_Log(S_ERROR, "[%s, line %d] Perfmonitor time or file is incorrect or missing!", __FILE__, __LINE__);
                }

            }


            /******* Backlist *******/

            if (!strcmp(sagan_var1, "blacklist:")) {

                config->blacklist_flag=1;

                ptmp = strtok_r(NULL,"\0", &tok);

                if ( ptmp == NULL ) {
                    Sagan_Log(S_ERROR, "[%s, line %d] \"blacklist:\" processor file(s) missing", __FILE__, __LINE__);
                }

                Remove_Return(ptmp);
                Remove_Spaces(ptmp);

                strlcpy(config->blacklist_files, ptmp, sizeof(config->blacklist_files));

            }

#ifdef WITH_BLUEDOT

            /******* Bluedot *******/

            if (!strcmp(sagan_var1, "bluedot:")) {

                config->bluedot_flag=1;

                /* Set defaults */

                strlcpy(config->bluedot_device_id, "NO_DEVICE_ID", sizeof(config->bluedot_device_id));
                config->bluedot_timeout = 120;

                config->bluedot_cat[0] = '\0';
                config->bluedot_url[0] = '\0';

                ptmp = sagan_var1;

                while (ptmp != NULL ) {

                    if (!strcmp(ptmp, "catagories")) {
                        ptmp = strtok_r(NULL, " ", &tok);
                        strlcpy(config->bluedot_cat, Remove_Return(ptmp), sizeof(config->bluedot_cat));
                    }

                    if (!strcmp(ptmp, "url")) {
                        ptmp = strtok_r(NULL, " ", &tok);
                        strlcpy(config->bluedot_url, Remove_Return(ptmp), sizeof(config->bluedot_url));
                    }

                    if (!strcmp(ptmp, "max_cache")) {
                        ptmp = strtok_r(NULL, " ", &tok);
                        config->bluedot_max_cache = strtoull(ptmp, NULL, 10);
                    }

                    if (!strcmp(ptmp, "cache_timeout")) {
                        ptmp = strtok_r(NULL, " ", &tok);
                        config->bluedot_timeout = atoi(ptmp) * 60;
                    }

                    if (!strcmp(ptmp, "device_id")) {
                        ptmp = strtok_r(NULL, " ", &tok);
                        strlcpy(config->bluedot_device_id, Remove_Return(ptmp), sizeof(config->bluedot_device_id));
                    }

                    ptmp = strtok_r(NULL, "=", &tok);
                }


                /* Bluedot sanity checks */

                if ( config->bluedot_cat[0] == '\0' ) {
                    Sagan_Log(S_ERROR, "[%s, line %d] Bluedot \"catagories\" option is missing.", __FILE__, __LINE__);
                }

                if ( config->bluedot_url[0] == '\0' ) {
                    Sagan_Log(S_ERROR, "[%s, line %d] Bluedott \"url\" optin is missing.", __FILE__, __LINE__);
                }

                Sagan_Bluedot_Load_Cat();

            }
#endif

            /* For the Bro Intellegence framework */

            /* This allows Sagan to load Bro Intel files like Critical Stack Bro/Intel files */

            if (!strcmp(sagan_var1, "bro-intel:")) {

                config->brointel_flag = 1;

                ptmp = strtok_r(NULL,"\0", &tok);

                if ( ptmp == NULL ) {
                    Sagan_Log(S_ERROR, "[%s, line %d] \"bro-intel:\" processor file(s) missing", __FILE__, __LINE__);
                }

                Remove_Return(ptmp);
                Remove_Spaces(ptmp);

                strlcpy(config->brointel_files, ptmp, sizeof(config->brointel_files));

            }

        }

        /****************************************************************************
         * Output formats
         ****************************************************************************/

        if (!strcmp(sagan_option, "output")) {

            config->output_thread_flag = 1;

            sagan_var1 = strtok_r(NULL," ", &tok);

            if ( sagan_var1 == NULL ) {
                Sagan_Log(S_ERROR, "[%s, line %d] \"%s\" appears to be incomplete!", sagan_option, __FILE__, __LINE__);
            }

            if (!strncmp(sagan_var1, "external:", 9)) {
                config->sagan_ext_flag=1;
                config->sagan_external_output_flag=1;

		check=0;
		i=0;

		if (sagan_var1[10] != ' ') {

			check = strlen(sagan_var1);
			strncpy(tmpbuf3, sagan_var1, 9);
			strcat(tmpbuf3," ");
			strncpy(tmpbuf4, sagan_var1+9,check);
			sagan_var1=strcat(tmpbuf3,tmpbuf4);
			sagan_var1[check+1]='\0';
			i=1;
		}
		
		if ( i == 1 ) {
			sagan_var1 = strtok_r(sagan_var1," ", &tok3);
			ptmp = strtok_r(NULL, " ", &tok3);
			strtok_r(NULL," ", &tok);

		}else{
                	ptmp = strtok_r(NULL, " ", &tok);
		}

                if ( ptmp == NULL ) {
                    Sagan_Log(S_ERROR, "[%s, line %d] \"external:\" output option is incomplete!", __FILE__, __LINE__);
                }

                Remove_Return(ptmp);
                Remove_Spaces(ptmp);

                if (stat(ptmp, &filecheck) != 0 ) {
                    Sagan_Log(S_ERROR, "[%s, line %d] \"external:\" output program '%s' does not exist! Abort!", __FILE__, __LINE__, ptmp);
                }

                if (access(ptmp, X_OK) == -1) {
                    Sagan_Log(S_ERROR, "[%s, line %d] \"external:\" output program '%s' is not executable! Abort!", __FILE__, __LINE__, ptmp);
                }

                strlcpy(config->sagan_extern, ptmp, sizeof(config->sagan_extern));
            }

#ifdef WITH_SYSLOG
            if (!strcmp(sagan_var1, "syslog:") || !strcmp(sagan_var1, "alert_syslog:" ) ) {

                config->sagan_syslog_flag = 1;

                /* Set defaults */

                config->sagan_syslog_facility = DEFAULT_SYSLOG_FACILITY;
                config->sagan_syslog_priority = DEFAULT_SYSLOG_PRIORITY;
                config->sagan_syslog_options = LOG_PID;

                /* Facility */

                sagan_var3 = strtok_r(NULL, " ", &tok);

                if ( sagan_var3 != NULL ) {

                    sagan_var2 = Remove_Return(sagan_var3);

#ifdef LOG_AUTH
                    if (!strcmp(sagan_var2, "LOG_AUTH")) {
                        config->sagan_syslog_facility = LOG_AUTH;
                    }
#endif

#ifdef LOG_AUTHPRIV
                    if (!strcmp(sagan_var2, "LOG_AUTHPRIV")) {
                        config->sagan_syslog_facility = LOG_AUTHPRIV;
                    }
#endif

#ifdef LOG_CRON
                    if (!strcmp(sagan_var2, "LOG_CRON")) {
                        config->sagan_syslog_facility = LOG_CRON;
                    }
#endif

#ifdef LOG_DAEMON
                    if (!strcmp(sagan_var2, "LOG_DAEMON")) {
                        config->sagan_syslog_facility = LOG_DAEMON;
                    }
#endif

#ifdef LOG_FTP
                    if (!strcmp(sagan_var2, "LOG_FTP")) {
                        config->sagan_syslog_facility = LOG_FTP;
                    }
#endif

#ifdef LOG_INSTALL
                    if (!strcmp(sagan_var2, "LOG_INSTALL")) {
                        config->sagan_syslog_facility = LOG_INSTALL;
                    }
#endif

#ifdef LOG_KERN
                    if (!strcmp(sagan_var2, "LOG_KERN")) {
                        config->sagan_syslog_facility = LOG_KERN;
                    }
#endif

#ifdef LOG_LPR
                    if (!strcmp(sagan_var2, "LOG_LPR")) {
                        config->sagan_syslog_facility = LOG_LPR;
                    }
#endif

#ifdef LOG_MAIL
                    if (!strcmp(sagan_var2, "LOG_MAIL")) {
                        config->sagan_syslog_facility = LOG_MAIL;
                    }
#endif

#ifdef LOG_NETINFO
                    if (!strcmp(sagan_var2, "LOG_NETINFO")) {
                        config->sagan_syslog_facility = LOG_NETINFO;
                    }
#endif

#ifdef LOG_RAS
                    if (!strcmp(sagan_var2, "LOG_RAS")) {
                        config->sagan_syslog_facility = LOG_RAS;
                    }
#endif

#ifdef LOG_REMOTEAUTH
                    if (!strcmp(sagan_var2, "LOG_REMOTEAUTH")) {
                        config->sagan_syslog_facility = LOG_REMOTEAUTH;
                    }
#endif

#ifdef LOG_NEWS
                    if (!strcmp(sagan_var2, "LOG_NEWS")) {
                        config->sagan_syslog_facility = LOG_NEWS;
                    }
#endif

#ifdef LOG_SYSLOG
                    if (!strcmp(sagan_var2, "LOG_SYSLOG")) {
                        config->sagan_syslog_facility = LOG_SYSLOG;
                    }
#endif

#ifdef LOG_USER
                    if (!strcmp(sagan_var2, "LOG_USER")) {
                        config->sagan_syslog_facility = LOG_USER;
                    }
#endif

#ifdef LOG_UUCP
                    if (!strcmp(sagan_var2, "LOG_UUCP")) {
                        config->sagan_syslog_facility = LOG_UUCP;
                    }
#endif

#ifdef LOG_LOCAL0
                    if (!strcmp(sagan_var2, "LOG_LOCAL0")) {
                        config->sagan_syslog_facility = LOG_LOCAL0;
                    }
#endif

#ifdef LOG_LOCAL1
                    if (!strcmp(sagan_var2, "LOG_LOCAL1")) {
                        config->sagan_syslog_facility = LOG_LOCAL1;
                    }
#endif

#ifdef LOG_LOCAL2
                    if (!strcmp(sagan_var2, "LOG_LOCAL2")) {
                        config->sagan_syslog_facility = LOG_LOCAL2;
                    }
#endif

#ifdef LOG_LOCAL3
                    if (!strcmp(sagan_var2, "LOG_LOCAL3")) {
                        config->sagan_syslog_facility = LOG_LOCAL3;
                    }
#endif

#ifdef LOG_LOCAL4
                    if (!strcmp(sagan_var2, "LOG_LOCAL4")) {
                        config->sagan_syslog_facility = LOG_LOCAL4;
                    }
#endif

#ifdef LOG_LOCAL5
                    if (!strcmp(sagan_var2, "LOG_LOCAL5")) {
                        config->sagan_syslog_facility = LOG_LOCAL5;
                    }
#endif

#ifdef LOG_LOCAL6
                    if (!strcmp(sagan_var2, "LOG_LOCAL6")) {
                        config->sagan_syslog_facility = LOG_LOCAL6;
                    }
#endif

#ifdef LOG_LOCAL7
                    if (!strcmp(sagan_var2, "LOG_LOCAL7")) {
                        config->sagan_syslog_facility = LOG_LOCAL7;
                    }
#endif

                }


                /* Priority */

                sagan_var3 = strtok_r(NULL, " ", &tok);

                if ( sagan_var3 == NULL ) {
                    Sagan_Log(S_ERROR, "[%s, line %d] \"syslog\" output \"priority\" is missing!", __FILE__, __LINE__);
                }

                if ( sagan_var3 != NULL ) {

                    sagan_var2 = Remove_Return(sagan_var3);


#ifdef LOG_EMERG
                    if (!strcmp(sagan_var2, "LOG_EMERG")) {
                        config->sagan_syslog_priority = LOG_EMERG;
                    }
#endif

#ifdef LOG_ALERT
                    if (!strcmp(sagan_var2, "LOG_ALERT")) {
                        config->sagan_syslog_priority = LOG_ALERT;
                    }
#endif

#ifdef LOG_CRIT
                    if (!strcmp(sagan_var2, "LOG_CRIT")) {
                        config->sagan_syslog_priority = LOG_CRIT;
                    }
#endif

#ifdef LOG_ERR
                    if (!strcmp(sagan_var2, "LOG_ERR")) {
                        config->sagan_syslog_priority = LOG_ERR;
                    }
#endif

#ifdef LOG_WARNING
                    if (!strcmp(sagan_var2, "LOG_WARNING")) {
                        config->sagan_syslog_priority = LOG_WARNING;
                    }
#endif

#ifdef LOG_NOTICE
                    if (!strcmp(sagan_var2, "LOG_NOTICE")) {
                        config->sagan_syslog_priority = LOG_NOTICE;
                    }
#endif

#ifdef LOG_INFO
                    if (!strcmp(sagan_var2, "LOG_INFO")) {
                        config->sagan_syslog_priority = LOG_INFO;
                    }
#endif

#ifdef LOG_DEBUG
                    if (!strcmp(sagan_var2, "LOG_DEBUG")) {
                        config->sagan_syslog_priority = LOG_DEBUG;
                    }
#endif
                }

                /* Syslog options */

                sagan_var3 = strtok_r(NULL, " ", &tok);

                if ( sagan_var3 != NULL ) {

                    sagan_var2 = Remove_Return(sagan_var3);


#ifdef LOG_CONS
                    if (!strcmp(sagan_var2, "LOG_CONS")) {
                        config->sagan_syslog_options |= LOG_CONS;
                    }
#endif

#ifdef LOG_NDELAY
                    if (!strcmp(sagan_var2, "LOG_NDELAY")) {
                        config->sagan_syslog_options |= LOG_NDELAY;
                    }
#endif

#ifdef LOG_PERROR
                    if (!strcmp(sagan_var2, "LOG_PERROR")) {
                        config->sagan_syslog_options |= LOG_PERROR;
                    }
#endif

#ifdef LOG_PID
                    if (!strcmp(sagan_var2, "LOG_PID")) {
                        config->sagan_syslog_options |= LOG_PID;
                    }
#endif

#ifdef LOG_NOWAIT
                    if (!strcmp(sagan_var2, "LOG_NOWAIT")) {
                        config->sagan_syslog_options |= LOG_NOWAIT;
                    }
#endif

                }

            } /* end of syslog config */
#endif


#ifdef WITH_SNORTSAM
            if (!strcmp(sagan_var1, "alert_fwsam:")) {
                strlcpy(config->sagan_fwsam_info, Remove_Return(strtok_r(NULL, " ", &tok)), sizeof(config->sagan_fwsam_info));
                config->sagan_fwsam_flag=1;
            }
#endif

#if !defined(HAVE_DNET_H) && !defined(HAVE_DUMBNET_H)
            if (!strcmp(sagan_var1, "unified2:")) {
                Sagan_Log(S_WARN,"\"libdnet\" support not found.  This is needed for unified2.");
                Sagan_Log(S_WARN, "Re-compile with libdnet support or disable in the sagan.conf.");
            }
#endif

#if defined(HAVE_DNET_H) || defined(HAVE_DUMBNET_H)

            if (!strcmp(sagan_var1, "unified2:")) {

                config->sagan_unified2_flag = 1;

                config->unified2_filepath[0] = '\0';

                ptmp = sagan_var1;
                Remove_Return(ptmp);

                while (ptmp != NULL ) {

                    if (!strcmp(ptmp, "filename")) {
                        ptmp = strtok_r(NULL, ",", &tok);
                        snprintf(config->unified2_filepath, sizeof(config->unified2_filepath)-1, "%s/%s", config->sagan_log_path, ptmp);
                    }

                    if (!strcmp(ptmp, "limit")) {
                        ptmp = strtok_r(NULL, " ", &tok);
                        config->unified2_limit = atoi(ptmp) * 1024 * 1024;
                    }

                    if (!strcmp(ptmp, "nostamp")) config->unified2_nostamp = 1;

                    ptmp = strtok_r(NULL, " ", &tok);

                }

                /* Sanity check for unified2 */

                if ( config->unified2_filepath == '\0' ) {
                    Sagan_Log(S_ERROR, "[%s, line %d] Unified2 \"filename\" is missing.", __FILE__, __LINE__);
                }
            }

#endif

#ifdef HAVE_LIBESMTP

            if (!strcmp(sagan_var1, "email:")) {

                ptmp = sagan_var1;

                config->sagan_esmtp_from[0] = '\0';
                config->sagan_esmtp_server[0] = '\0';

                while (ptmp != NULL ) {

                    if (!strcmp(ptmp, "from")) {
                        ptmp = strtok_r(NULL, " ", &tok);
                        strlcpy(config->sagan_esmtp_from, Remove_Return(ptmp), sizeof(config->sagan_esmtp_from));
                    }

                    if (!strcmp(ptmp, "smtpserver")) {
                        ptmp = strtok_r(NULL, " ", &tok);
                        strlcpy(config->sagan_esmtp_server, Remove_Return(ptmp), sizeof(config->sagan_esmtp_server));
                    }

                    ptmp = strtok_r(NULL, "=", &tok);
                }

                /* Sanity checks for email: */

                if ( config->sagan_esmtp_from == '\0' ) {
                    Sagan_Log(S_ERROR, "[%s, line %d] email: \"from\" option is missing.", __FILE__, __LINE__);
                }

                if ( config->sagan_esmtp_server == '\0' ) {
                    Sagan_Log(S_ERROR, "[%s, line %d] email: \"smtpserver\" option is missing.", __FILE__, __LINE__);
                }

            }
#endif
        }

        /* var */

        if (!strcmp(sagan_option, "var")) {

            sagan_var1 = strtok_r(NULL, " ", &tok);
            var = (_SaganVar *) realloc(var, (counters->var_count+1) * sizeof(_SaganVar));   /* Allocate memory */

            if ( var == NULL ) {
                Sagan_Log(S_ERROR, "[%s, line %d] Failed to reallocate memory for var. Abort!", __FILE__, __LINE__);
            }

            snprintf(var[counters->var_count].var_name, sizeof(var[counters->var_count].var_name)-1, "$%s", sagan_var1);

            /* Load var from file as suggested in https://github.com/beave/sagan/issues/75 */

            if (Sagan_strstr(tmpbuf2, "file:/")) {

                sagan_var2 = strtok_r(NULL, " ", &tok);				/* file://path/file */
                strtok_r(sagan_var2, ":", &tok2);				/* skip paste file: */
                filename = Remove_Return(strtok_r(NULL, ":", &tok2)); 		/* Get path + filename */

                if ((tmpcfg = fopen(filename, "r")) == NULL) {
                    fprintf(stderr, "[%s, line %d] Cannot open var file:%s\n", __FILE__,  __LINE__, filename);
                    exit(1);
                }


                check = 0;

                while(fgets(tmpbuf2, sizeof(tmpbuf2), tmpcfg) != NULL) {

                    /* Stuff to skip */

                    if (tmpbuf2[0] == '#') continue;
                    if (tmpbuf2[0] == ';') continue;
                    if (tmpbuf2[0] == 10 ) continue;
                    if (tmpbuf2[0] == 32 ) continue;

                    /* Simple check to see if this is the first entry or not.  This is to keep our
                       "," on mark */

                    if ( check == 0 ) {

                        snprintf(tmpstring, sizeof(tmpstring), "%s", Remove_Return(tmpbuf2));
                        check = 1;

                    } else {

                        snprintf(tmpstring, sizeof(tmpstring), ",%s", Remove_Return(tmpbuf2));

                    }

                    /* Append to the var */

                    strlcat(var[counters->var_count].var_value, tmpstring, sizeof(var[counters->var_count].var_value));

                }


                fclose(tmpcfg);

            } else {

                /* Non-file based var's */

                /* Test for multiple values via [ ] or single value */

                if ((Sagan_strstr(tmpbuf2, "[") && !Sagan_strstr(tmpbuf2, "]")) || (!Sagan_strstr(tmpbuf2, "[") && Sagan_strstr(tmpbuf2, "]"))) {
                    Sagan_Log(S_ERROR, "[%s, line %d] A 'var' in the sagan.conf file contains mismatched [ ]!", __FILE__, __LINE__);
                }

                /* Multiple values */

                if (Sagan_strstr(tmpbuf2, "[") && Sagan_strstr(tmpbuf2, "]")) {

                    sagan_var2 = strtok_r(NULL, "[", &tok);
                    sagan_var3 = strtok_r(sagan_var2, "]", &tok2);

                    Sagan_Var_To_Value(sagan_var3);
                    Remove_Return(sagan_var3);
                    Remove_Spaces(sagan_var3);

                    strlcpy(var[counters->var_count].var_value, sagan_var3, sizeof(var[counters->var_count].var_value));

                } else {

                    /* Single value */

                    sagan_var2 = strtok_r(NULL, " ", &tok); /* Move to position of value of var */

                    Sagan_Var_To_Value(sagan_var2);
                    Remove_Return(sagan_var2);
                    Remove_Spaces(sagan_var2);

                    strlcpy(var[counters->var_count].var_value, sagan_var2, sizeof(var[counters->var_count].var_value));

                }

                counters->var_count++;

            }

            /* Required var's - all others are optional */

            if (!strcmp(sagan_var1, "FIFO") && config->sagan_is_file == 0) {
                strlcpy(config->sagan_fifo, sagan_var2, sizeof(config->sagan_fifo));
            }

            if (!strcmp(sagan_var1, "LOCKFILE" )) {
                strlcpy(config->sagan_lockfile, sagan_var2, sizeof(config->sagan_lockfile));
            }

            if (!strcmp(sagan_var1, "ALERTLOG" )) {
                strlcpy(config->sagan_alert_filepath, sagan_var2, sizeof(config->sagan_alert_filepath));
            }

            if (!strcmp(sagan_var1, "SAGANLOGPATH" )) {
                strlcpy(config->sagan_log_path, sagan_var2, sizeof(config->sagan_log_path));
            }
        }

        /* Check for duplicate VAR's */

        for (i = 0; i < counters->var_count; i++) {

            for ( check = i+1; check < counters->var_count; check ++) {

                if (!strcmp (var[check].var_name, var[i].var_name )) {

                    Sagan_Log(S_ERROR, "[%s, line %d] Detected duplicate var '%s' & '%s'.  Please correct this.", __FILE__, __LINE__, var[check].var_name, var[i].var_name);

                }
            }
        }

        /* "include */

        if (!strcmp(sagan_option, "include" )) {

            strlcpy(tmpstring, Remove_Return(strtok_r(NULL, " ", &tok)), sizeof(tmpstring));

            strlcpy(ruleset, Sagan_Var_To_Value(tmpstring), sizeof(ruleset));
            Remove_Spaces(ruleset);

            filename=Get_Filename(ruleset);   /* Get the file name to figure out "what" we're loading */

            if (!strcmp(filename, "classification.config")) {
                Load_Classifications(ruleset);
            }

            if (!strcmp(filename, "reference.config")) {
                Load_Reference(ruleset);
            }

            if (!strcmp(filename, "gen-msg.map")) {
                Load_Gen_Map(ruleset);
            }

            if (!strcmp(filename, "protocol.map")) {
                Load_Protocol_Map(ruleset);
            }

            /* It's not reference.config, classification.config, gen-msg.map or protocol.map, it must be a ruleset */

            if (strcmp(filename, "reference.config") && strcmp(filename, "classification.config") && strcmp(filename, "gen-msg.map") && strcmp(filename, "protocol.map")) {

                pthread_mutex_lock(&SaganRulesLoadedMutex);

                reload_rules = 1;

                rules_loaded = (_Rules_Loaded *) realloc(rules_loaded, (counters->rules_loaded_count+1) * sizeof(_Rules_Loaded));

                if ( rules_loaded == NULL ) {
                    Sagan_Log(S_ERROR, "[%s, line %d] Failed to reallocate memory for rules_loaded. Abort!", __FILE__, __LINE__);
                }

                strlcpy(rules_loaded[counters->rules_loaded_count].ruleset, ruleset, sizeof(rules_loaded[counters->rules_loaded_count].ruleset));
                counters->rules_loaded_count++;

                Load_Rules(ruleset);

                reload_rules = 0;
                pthread_mutex_unlock(&SaganRulesLoadedMutex);

            }
        }
    }

    fclose(sagancfg);

    /* Check rules for duplicate sid.  We can't have that! */

    for (i = 0; i < counters->rulecount; i++) {
        for ( check = i+1; check < counters->rulecount; check ++) {
            if (!strcmp (rulestruct[check].s_sid, rulestruct[i].s_sid )) {
                Sagan_Log(S_ERROR, "[%s, line %d] Detected duplicate signature id [sid] number %s.  Please correct this.", __FILE__, __LINE__, rulestruct[check].s_sid, rulestruct[i].s_sid);
            }
        }
    }

    /* If we have the "send-to" option,  verify the configuration has the proper smtpserver, etc.  */

#ifdef HAVE_LIBESMTP

    if (config->sagan_esmtp_flag && !strcmp(config->sagan_esmtp_server, "")) {
        Sagan_Log(S_ERROR, "[%s, line %d] Configuration SMTP 'smtpserver' field is missing! |%s|", __FILE__, __LINE__, config->sagan_esmtp_server);
    }

    if (config->sagan_esmtp_flag && !strcmp(config->sagan_esmtp_from, "" )) {
        Sagan_Log(S_ERROR, "[%s, line %d] Configuration SMTP 'from' field is missing!", __FILE__,  __LINE__);
    }

#endif

    if ( config->sagan_is_file == 0 && config->sagan_fifo[0] == '\0' ) {
        Sagan_Log(S_ERROR, "[%s, line %d] No FIFO option found which is required! Aborting!", __FILE__, __LINE__);
    }

    if ( config->sagan_host[0] == '\0' ) {
        Sagan_Log(S_ERROR, "[%s, line %d] The 'sagan_host' option was not found and is required.", __FILE__, __LINE__);
    }

#ifdef HAVE_LIBMAXMINDDB
    if ( config->have_geoip2 ) {
        if ( Sagan_Check_Var("$HOME_COUNTRY") == 0 ) {
            Sagan_Log(S_ERROR, "[%s, line %d] GeoIP2 is in use,  but $HOME_COUNTRY was never set in your configuration. Abort.", __FILE__, __LINE__);
        }
    }
#endif

#ifdef HAVE_LIBLOGNORM
    if ( liblognorm_load == 0 ) {
        Sagan_Log(S_ERROR, "[%s, line %d] liblognorm is in use but 'normalize_file' is not set.  Abort.", __FILE__, __LINE__);
    }
#endif

}
