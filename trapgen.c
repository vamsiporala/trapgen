/*
 * snmptrap.c - send snmp traps to a network entity.
 *
 */
/******************************************************************
	Copyright 1989, 1991, 1992 by Carnegie Mellon University

                      All Rights Reserved

Permission to use, copy, modify, and distribute this software and its
documentation for any purpose and without fee is hereby granted,
provided that the above copyright notice appear in all copies and that
both that copyright notice and this permission notice appear in
supporting documentation, and that the name of CMU not be
used in advertising or publicity pertaining to distribution of the
software without specific, written prior permission.

CMU DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE, INCLUDING
ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO EVENT SHALL
CMU BE LIABLE FOR ANY SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR
ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS,
WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION,
ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS
SOFTWARE.
******************************************************************/
#include <net-snmp/net-snmp-config.h>

#if HAVE_STDLIB_H
#include <stdlib.h>
#endif
#if HAVE_UNISTD_H
#include <unistd.h>
#endif
#if HAVE_STRING_H
#include <string.h>
#else
#include <strings.h>
#endif
#include <sys/types.h>
#if HAVE_NETINET_IN_H
# include <netinet/in.h>
#endif
#if TIME_WITH_SYS_TIME
# ifdef WIN32
#  include <sys/timeb.h>
# else
#  include <sys/time.h>
# endif
# include <time.h>
#else
# if HAVE_SYS_TIME_H
#  include <sys/time.h>
# else
#  include <time.h>
# endif
#endif
#if HAVE_SYS_SELECT_H
#include <sys/select.h>
#endif
#include <stdio.h>
#if HAVE_WINSOCK_H
#include <winsock.h>
#endif
#if HAVE_SYS_SOCKET_H
#include <sys/socket.h>
#endif
#if HAVE_NETDB_H
#include <netdb.h>
#endif
#if HAVE_ARPA_INET_H
#include <arpa/inet.h>
#endif

#include <net-snmp/net-snmp-includes.h>

oid             objid_enterprise[] = { 1, 3, 6, 1, 4, 1, 3, 1, 1 };
oid             objid_sysdescr[] = { 1, 3, 6, 1, 2, 1, 1, 1, 0 };
oid             objid_sysuptime[] = { 1, 3, 6, 1, 2, 1, 1, 3, 0 };
oid             objid_snmptrap[] = { 1, 3, 6, 1, 6, 3, 1, 1, 4, 1, 0 };
int             inform = 0;


/* Variables required for SNMP Trap scale */
unsigned int trap_rate;
unsigned int trap_count;
char *trap_file = {0};
char *trap_type = {0};

void
usage(void)
{
    fprintf(stderr, "USAGE: %s ", inform ? "snmpinform" : "snmptrap");
    snmp_parse_args_usage(stderr);
    fprintf(stderr, " TRAP-PARAMETERS\n\n");
    snmp_parse_args_descriptions(stderr);
    fprintf(stderr,
            "  -C APPOPTS\t\tSet various application specific behaviour:\n");
    fprintf(stderr, "\t\t\t  i:  send an INFORM instead of a TRAP\n");
    fprintf(stderr,
            "\n  -v 1 TRAP-PARAMETERS:\n\t enterprise-oid agent trap-type specific-type uptime [OID TYPE VALUE]...\n");
    fprintf(stderr, "  or\n");
    fprintf(stderr,
            "  -v 2 TRAP-PARAMETERS:\n\t uptime trapoid [OID TYPE VALUE] ...\n");
    fprintf(stderr,
            "  -F\t\t Trap Info file\n");
    fprintf(stderr,
            "  -R\t\t Trap Rate per second\n");
    fprintf(stderr,
            "  -N\t\t Number of Traps to be sent\n");
    fprintf(stderr,
            "  -K\t\t Trap Type ( Options are linkup , linkdown , mac , maclink)\n");
}

int
snmp_input(int operation,
           netsnmp_session * session,
           int reqid, netsnmp_pdu *pdu, void *magic)
{
    return 1;
}

in_addr_t
parse_address(char *address)
{
    in_addr_t       addr;
    struct sockaddr_in saddr;
    struct hostent *hp;

    if ((addr = inet_addr(address)) != -1)
        return addr;
    hp = gethostbyname(address);
    if (hp == NULL) {
        fprintf(stderr, "unknown host: %s\n", address);
        exit(1);
    } else {
        memcpy(&saddr.sin_addr, hp->h_addr, hp->h_length);
        return saddr.sin_addr.s_addr;
    }

}

static void
optProc(int argc, char *const *argv, int opt)
{
    switch (opt) {
    case 'C':
        while (*optarg) {
            switch (*optarg++) {
            case 'i':
                inform = 1;
                break;
            default:
                fprintf(stderr,
                        "Unknown flag passed to -C: %c\n", optarg[-1]);
                exit(1);
            }
        }
        break;
    case 'R':
        if (optarg) {
           trap_rate = atoi(optarg);
        } else {
           fprintf(stderr,
                   "Trap Rate per second input is missing. So setting to default value as 1\n");
           exit(1);
        }
        break;
    case 'N':
        if (optarg) {
           trap_count = atoi(optarg);
        } else {
           fprintf(stderr,
                   "Trap Count is mandatory. Please specify and re-run\n");
           exit(1);
        }
        break;
    case 'F':
        if (optarg) {
           trap_file = (char *) malloc(sizeof(optarg)+1);
           if (trap_file)
              strcpy(trap_file,optarg);
           else
              fprintf(stderr,"Failed to copy the trap file path\n");
        } else {
           fprintf(stderr,
                   "Trap Info file is mandatory. Please specify and re-run \n");
           exit(1);
        }
        break;
    case 'K':
        if (optarg) {
            if (!strcmp("linkup",optarg) || \
                !strcmp("linkdown",optarg) || \
                !strcmp("mac",optarg) || \
                !strcmp("maclink",optarg)) {
                trap_type = (char *) malloc(sizeof(optarg)+1);
                if (trap_type)
                   strcpy(trap_type,optarg);
                else
                   fprintf(stderr,"Failed to parse the trap type\n");
            } else {
                fprintf(stderr,
                        "Trap type mentioned is %s, but supported trap types are linkup,linkdown,mac,maclink\n",optarg);
                exit(1);
            }
        } else {
            fprintf(stderr,
                    "Trap Type is mandatory. Please specify and re-run\n");
            exit(1);
        }
        break;
           
    }
}

static void
parse_trap_info(char *trap_file, char *trapinfo[])
{

    FILE *fp;
    char c;
    long int prev,cur;
    int j=0;
    fp = fopen(trap_file,"r");
    if (fp) {
      prev = SEEK_SET;
      while ((c=fgetc(fp))!= EOF) {
        if (c == ';') {
           cur = ftell(fp);
           trapinfo[j] = (char *) malloc(cur-prev);
           if (trapinfo[j]) {
              fseek(fp,prev,SEEK_SET);
              fgets(trapinfo[j],(cur-prev),fp);
              prev = cur;
              fseek(fp,cur,SEEK_SET);
              j++;
           } else {
              fprintf(stderr, "Failed to allocate memory for storing trap info file content\n");
              exit(1);
           }
        }
      }
      fclose(fp);
    } else {
      fprintf(stderr, "Failed to open the trap info faile %s",trap_file);
      exit(1);
    }
}


int
main(int argc, char *argv[])
{
    netsnmp_session session, *ss;
    netsnmp_pdu    *pdu, *response;
    oid             name[MAX_OID_LEN];
    size_t          name_length;
    int             arg;
    int             status;
    char           *trap = NULL;
    char           *prognam;
    int             exitval = 0;
#ifndef DISABLE_SNMPV1
    char           *specific = NULL, *description = NULL, *agent = NULL;
    in_addr_t      *pdu_in_addr_t;
#endif

    prognam = strrchr(argv[0], '/');
    if (prognam)
        prognam++;
    else
        prognam = argv[0];

    putenv(strdup("POSIXLY_CORRECT=1"));

    if (strcmp(prognam, "snmpinform") == 0)
        inform = 1;
    switch (arg = snmp_parse_args(argc, argv, &session, "C:R:N:F:K:", optProc)) {
    case -2:
        exit(0);
    case -1:
        usage();
        exit(1);
    default:
        break;
    }

    /* Parse Trap Info and find out how many traps info is available */
    char c;
    FILE *fp;
    unsigned int delimit_count = 0;
 
    fp = fopen(trap_file,"r");
    if (fp) {
       while ((c = fgetc(fp)) != EOF) {
          if (c == ';')
             delimit_count++;
       }
    } else {
       fprintf(stderr, "Failed to open the trap info file %s", trap_file);
       exit(1);
    }

    /* Check if trap info is available trap_count, if not report error and abort */
    if (delimit_count != trap_count) {
       fprintf(stderr,
               "Trap Count mentioned is %d for option N,",trap_count);
       fprintf(stderr," but traps info is available only for %d in %s\n",delimit_count,trap_file);
       exit(1);
    }

    /* Parse the trap info file and fetch the content into array strings */
    char *trapinfo[delimit_count];
    parse_trap_info(trap_file,trapinfo);
 

    SOCK_STARTUP;

    session.callback = snmp_input;
    session.callback_magic = NULL;
    netsnmp_ds_set_int(NETSNMP_DS_LIBRARY_ID, NETSNMP_DS_LIB_DEFAULT_PORT, 
		       SNMP_TRAP_PORT);

    if (session.version == SNMP_VERSION_3 && !inform) {
        /*
         * for traps, we use ourselves as the authoritative engine
         * which is really stupid since command line apps don't have a
         * notion of a persistent engine.  Hence, our boots and time
         * values are probably always really wacked with respect to what
         * a manager would like to see.
         * 
         * The following should be enough to:
         * 
         * 1) prevent the library from doing discovery for engineid & time.
         * 2) use our engineid instead of the remote engineid for
         * authoritative & privacy related operations.
         * 3) The remote engine must be configured with users for our engineID.
         * 
         * -- Wes 
         */

        /*
         * setup the engineID based on IP addr.  Need a different
         * algorthim here.  This will cause problems with agents on the
         * same machine sending traps. 
         */
        setup_engineID(NULL, NULL);

        /*
         * pick our own engineID 
         */
        if (session.securityEngineIDLen == 0 ||
            session.securityEngineID == NULL) {
            session.securityEngineID =
                snmpv3_generate_engineID(&session.securityEngineIDLen);
        }
        if (session.contextEngineIDLen == 0 ||
            session.contextEngineID == NULL) {
            session.contextEngineID =
                snmpv3_generate_engineID(&session.contextEngineIDLen);
        }

        /*
         * set boots and time, which will cause problems if this
         * machine ever reboots and a remote trap receiver has cached our
         * boots and time...  I'll cause a not-in-time-window report to
         * be sent back to this machine. 
         */
        if (session.engineBoots == 0)
            session.engineBoots = 1;
        if (session.engineTime == 0)    /* not really correct, */
            session.engineTime = get_uptime();  /* but it'll work. Sort of. */
    }

    ss = snmp_open(&session);
    if (ss == NULL) {
        /*
         * diagnose snmp_open errors with the input netsnmp_session pointer 
         */
        snmp_sess_perror("snmptrap", &session);
        SOCK_CLEANUP;
        exit(1);
    }

#ifndef DISABLE_SNMPV1
    if (session.version == SNMP_VERSION_1) {
        if (inform) {
            fprintf(stderr, "Cannot send INFORM as SNMPv1 PDU\n");
            exit(1);
        }
        pdu = snmp_pdu_create(SNMP_MSG_TRAP);
        pdu_in_addr_t = (in_addr_t *) pdu->agent_addr;
        if (arg == argc) {
            fprintf(stderr, "No enterprise oid\n");
            usage();
            SOCK_CLEANUP;
            exit(1);
        }
        if (argv[arg][0] == 0) {
            pdu->enterprise = (oid *) malloc(sizeof(objid_enterprise));
            memcpy(pdu->enterprise, objid_enterprise,
                   sizeof(objid_enterprise));
            pdu->enterprise_length =
                sizeof(objid_enterprise) / sizeof(oid);
        } else {
            name_length = MAX_OID_LEN;
            if (!snmp_parse_oid(argv[arg], name, &name_length)) {
                snmp_perror(argv[arg]);
                usage();
                SOCK_CLEANUP;
                exit(1);
            }
            pdu->enterprise = (oid *) malloc(name_length * sizeof(oid));
            memcpy(pdu->enterprise, name, name_length * sizeof(oid));
            pdu->enterprise_length = name_length;
        }
        if (++arg >= argc) {
            fprintf(stderr, "Missing agent parameter\n");
            usage();
            SOCK_CLEANUP;
            exit(1);
        }
        agent = argv[arg];
        if (agent != NULL && strlen(agent) != 0) {
            *pdu_in_addr_t = parse_address(agent);
        } else {
            *pdu_in_addr_t = get_myaddr();
        }
        if (++arg == argc) {
            fprintf(stderr, "Missing generic-trap parameter\n");
            usage();
            SOCK_CLEANUP;
            exit(1);
        }
        trap = argv[arg];
        pdu->trap_type = atoi(trap);
        if (++arg == argc) {
            fprintf(stderr, "Missing specific-trap parameter\n");
            usage();
            SOCK_CLEANUP;
            exit(1);
        }
        specific = argv[arg];
        pdu->specific_type = atoi(specific);
        if (++arg == argc) {
            fprintf(stderr, "Missing uptime parameter\n");
            usage();
            SOCK_CLEANUP;
            exit(1);
        }
        description = argv[arg];
        if (description == NULL || *description == 0)
            pdu->time = get_uptime();
        else
            pdu->time = atol(description);
    } else
#endif
    {

        long            sysuptime;
        char            csysuptime[20];
        char base_trap[100] = {0};
        char *token = (char *) malloc(500);
        char one_oid[500] = {0};
        char oid_name[200] = {0};
        char oid_type[10] = {0};
        char oid_val[200] = {0};

        time_t start_time,end_time,elapsed_time;
        start_time = time(NULL);
        printf(ctime(&start_time));

        /* To send the trap load */
        int tc = 1;
        while (tc <= trap_count) {
          pdu = snmp_pdu_create(inform ? SNMP_MSG_INFORM : SNMP_MSG_TRAP2);
          if (arg == argc) {
             fprintf(stderr, "Missing up-time parameter\n");
             usage();
             SOCK_CLEANUP;
             exit(1);
          }
          trap = argv[arg];
          if (*trap == 0) {
              sysuptime = get_uptime();
              sprintf(csysuptime, "%ld", sysuptime);
              trap = csysuptime;
          }
          snmp_add_var(pdu, objid_sysuptime,
                       sizeof(objid_sysuptime) / sizeof(oid), 't', trap);

          if (!strcmp("linkup",trap_type)) {
              strcpy(base_trap,"IF-MIB::linkUp");
          }

          if (snmp_add_var
             (pdu, objid_snmptrap, sizeof(objid_snmptrap) / sizeof(oid),
              'o', base_trap) != 0) {
             snmp_perror(base_trap);
             SOCK_CLEANUP;
             exit(1);
          }

          token = strtok(trapinfo[tc-1],",");
          while(token) {
            one_oid[0] = '\0';
            strcpy(one_oid,token);
            int p,q;
            int space_count = 0;
            memset(oid_name,0,sizeof(oid_name));
            memset(oid_type,0,sizeof(oid_type));
            memset(oid_val,0,sizeof(oid_val));
            for (p=0,q=0; one_oid[p] != '\0'; p++,q++) {
                 if (isspace(one_oid[p])) {
                     space_count++;
                     q = -1;
                 }
                 if (space_count == 0)
                    oid_name[q] = one_oid[p];
                 else if (space_count == 1)
                    oid_type[q] = one_oid[p];
                 else if (space_count == 2)
                    oid_val[q] = one_oid[p];
                    
            }
           
            name_length = MAX_OID_LEN;
            if (!snmp_parse_oid(oid_name, name, &name_length)) {
               snmp_perror(oid_name);
               SOCK_CLEANUP;
               exit(1);
            }
            if (snmp_add_var
               (pdu, name, name_length, oid_type[0],
               oid_val) != 0) {
               snmp_perror(oid_name);
               SOCK_CLEANUP;
               exit(1);
            }

            token = strtok(NULL,",");
          }

          if (inform)
             status = snmp_synch_response(ss, pdu, &response);
          else
             status = snmp_send(ss, pdu) == 0;
          if (status) {
             snmp_sess_perror(inform ? "snmpinform" : "snmptrap", ss);
             if (!inform)
                snmp_free_pdu(pdu);
             exitval = 1;
          } else if (inform)
             snmp_free_pdu(response);

          // wait for one second after pumping trap_rate number of traps
          if (tc % trap_rate == 0)
              sleep(1);

          tc++; //Increment the trap counter
       }
       end_time = time(NULL);
       printf(ctime(&end_time));
 
    }
    /*
    arg++;

    while (arg < argc) {
        arg += 3;
        if (arg > argc) {
            fprintf(stderr, "%s: Missing type/value for variable\n",
                    argv[arg - 3]);
            SOCK_CLEANUP;
            exit(1);
        }
        name_length = MAX_OID_LEN;
        if (!snmp_parse_oid(argv[arg - 3], name, &name_length)) {
            snmp_perror(argv[arg - 3]);
            SOCK_CLEANUP;
            exit(1);
        }
        if (snmp_add_var
            (pdu, name, name_length, argv[arg - 2][0],
             argv[arg - 1]) != 0) {
            snmp_perror(argv[arg - 3]);
            SOCK_CLEANUP;
            exit(1);
        }
    }

    if (inform)
        status = snmp_synch_response(ss, pdu, &response);
    else
        status = snmp_send(ss, pdu) == 0;
    if (status) {
        snmp_sess_perror(inform ? "snmpinform" : "snmptrap", ss);
        if (!inform)
            snmp_free_pdu(pdu);
        exitval = 1;
    } else if (inform)
        snmp_free_pdu(response);

    */
    snmp_close(ss);
    SOCK_CLEANUP;
    return exitval;
}
