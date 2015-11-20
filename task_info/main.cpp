// 2014-01-18 simon_b: created filed
// 2014-01-16 simon_b: working version 1.0
// 2014-01-17 simon_b: version 1.1
// 2014-01-18 simon_b: added network data stats
// 2014-01-18 simon_b: version 1.2
// 2014-01-21 simon_b: added some basic usage help, fix for poss. overflow in net stats
// 2014-01-21 simon_b: Timestamp now includes date
// 2014-01-21 simon_b: version 1.22
// 2014-01-25 simon_b: now printing net interface name with -v
// 2014-01-25 simon_b: version 1.23

#include <stdio.h>
#include <stdlib.h>

#include <libproc.h>
#include <mach/mach_init.h>
#include <mach/mach_error.h>
#include <mach/mach_traps.h>
#include <mach/thread_act.h>
#include <mach/mach_types.h>
#include <mach/thread_info.h>
#include <mach/task.h>
#include <map.h>
#include <math.h>
#include <netinet/in.h>
#include <net/if.h>
#import <net/if_dl.h>
#include <net/route.h>
#import <sys/socket.h>

#include <string>
#include <sys/param.h>
#include <sys/sysctl.h>
#include <pthread.h>
#include <sys/time.h>
#include <unistd.h>

#include <mach/mach_host.h>	/* host_statistics */


struct statinfo {
	host_cpu_load_info_data_t   load;    
    u_int64_t                   netBytesIn;
    u_int64_t                   netBytesOut;
    
    // We have to track the stats seperately for each interface.
    std::map<int,long>          ibytes,obytes;
};

// Used for tracking system CPU usage.
static struct statinfo gCur, gLast;

bool                gFmpFormat = false;
static bool         gKeepRunning = true;
static mach_port_t  gHostPort;
bool                gHasRunOnce = false;
bool                gVerbose;


//
//  p r i n t _ t i m e s t a m p
//

void print_timestamp () {

    const char          *formatStr = "%04d-%02d-%02d %d:%02d:%02d.%06d";
    struct tm           theTime;
    int					timeLen;				// length of time str

	struct timeval		tv;
	struct timezone		tz;

    if (gFmpFormat) {
        printf ("$pd.timestamp=\"");
    }

    // Get GMT time, convert to local time, and then to desired string format.
    gettimeofday(&tv, &tz);
    localtime_r (&tv.tv_sec, &theTime);
    timeLen = printf(formatStr, theTime.tm_year+1900, theTime.tm_mon+1, theTime.tm_mday, theTime.tm_hour, theTime.tm_min, theTime.tm_sec, tv.tv_usec);
    if (gFmpFormat) {
        printf ("\";\n");
    } else {
        printf (", ");
    }
}

//
//  n e t _ s t a t s
//

// Returns non-zero value if there was an error.

int net_stats () {
    
    int mib[] = {
        CTL_NET,
        PF_ROUTE,
        0,
        0,
        NET_RT_IFLIST2,
        0
    };
    
    char                *sysctlBuf = NULL;
    struct if_msghdr    *ifm;
    char                ifName[32];
    size_t              len = 0;
    int                 errNo;
    char                *lim   = NULL;
    char                *next = NULL;

    
    const char  *printFormatStandard = "%qu, %qu, ";
    const char  *printFormatFMP = "$pd.netin=%qu;\n$pd.netout=%qu\n";
    const char  *formatStr;
    
    if (gFmpFormat)
        formatStr = printFormatFMP;
    else
        formatStr = printFormatStandard;
    
    // Determine how bug the buffer needs to be.
    errNo=sysctl(mib, 6, NULL, &len, NULL, 0);
    
    if (errNo < 0) {
        fprintf(stderr, "sysctl: %s\n", strerror(errNo));
        return errNo;
    }
    
    sysctlBuf = (char *) malloc(len);
    if (sysctlBuf == NULL) return -1;
    
    errNo = sysctl(mib, 6, sysctlBuf, &len, NULL, 0);
    if (errNo < 0) {
        fprintf(stderr, "sysctl: %s\n", strerror(errNo));
        return errNo;
    }
    
    lim = sysctlBuf + len;
    
    gCur.netBytesIn = 0;
    gCur.netBytesOut = 0;
    int ifNum = 0;
    
    for (next = sysctlBuf; next < lim; ) {

        ifm = (struct if_msghdr *)next;
        next += ifm->ifm_msglen;
        
        if (ifm->ifm_type == RTM_IFINFO2) {

            // GET INTERFACE NAME
            
            // Parts of below based on intpr() from http://www.opensource.apple.com/source/network_cmds/network_cmds-433/netstat.tproj/if.c
            // and is used to get the interface name.
            struct if_msghdr2 *if2m = (struct if_msghdr2 *)ifm;
            struct sockaddr_dl *sdl = (struct sockaddr_dl *)(if2m + 1);

            /*
             int mibname[6];
             size_t miblen = sizeof(struct ifmibdata_supplemental);
             */
            
            strncpy(ifName, sdl->sdl_data, sdl->sdl_nlen);
            ifName[sdl->sdl_nlen] = 0;  // strncpy does not add null

            /// https://github.com/lid/MenuMeters/blob/master/MenuExtras/MenuMeterNet/MenuMeterNetStats.m
            //// Expecting interface data
            // Only look at link layer items
            //struct sockaddr_dl *sdl = (struct sockaddr_dl *)(ifm + 1);
            //if (sdl->sdl_family != AF_LINK) {
                //continue;
            //}

            ///std::string ifName (sdl->sdl_data, sdl->sdl_nlen);
            //// printf ("interface: %s\n", ifName.c_str());
            
            
            // Skip over the loopback
            if (ifm->ifm_flags & IFF_LOOPBACK) {
                //printf ("skip loopback\n");
                continue;
            }

            // If verbose and there is something to report.
            if (gVerbose && if2m->ifm_data.ifi_ibytes && if2m->ifm_data.ifi_obytes) {
                printf ("%s:  ibytes: %lld, obytes: %lld\n", ifName, if2m->ifm_data.ifi_ibytes, if2m->ifm_data.ifi_obytes);
            }
            // ==========

            
            if (gLast.ibytes[ifNum] > if2m->ifm_data.ifi_ibytes) {
                // We have overflow.
                gCur.netBytesIn += gLast.netBytesIn + if2m->ifm_data.ifi_ibytes + UINT_MAX - gLast.ibytes[ifNum] + 1;
            } else {
                // No overflow so use simpler calc.
                gCur.netBytesIn += if2m->ifm_data.ifi_ibytes - gLast.ibytes[ifNum];
            }
            
            if (gLast.obytes[ifNum] > if2m->ifm_data.ifi_obytes) {
                gCur.netBytesOut = gLast.netBytesOut + if2m->ifm_data.ifi_obytes + UINT_MAX - gLast.obytes[ifNum] + 1;
            } else {
                gCur.netBytesOut += if2m->ifm_data.ifi_obytes - gLast.obytes[ifNum];
            }

            gCur.ibytes [ifNum] = if2m->ifm_data.ifi_ibytes;
            gCur.obytes [ifNum] = if2m->ifm_data.ifi_obytes;
            //printf (" ifNum %i done\n", ifNum);
        } else {
            //printf ("skip if not IFINFO2\n");
        }
    }
    
    free (sysctlBuf);
    
    return 0;
}

//
//  p r i n t _ n e t _ s t a t s
//

void net_print_stats (bool doPrint) {
    
    const char  *printFormatStandard = "%qu, %qu, ";
    const char  *printFormatFMP = "$pd.netin=%qu;\n$pd.netout=%qu\n";
    const char  *formatStr;
    
    if (gFmpFormat)
        formatStr = printFormatFMP;
    else
        formatStr = printFormatStandard;

    if (doPrint) {
        // The 32-bit values may overflow
        uint64_t deltaIn = (gCur.netBytesIn > gLast.netBytesIn) ? (gCur.netBytesIn - gLast.netBytesIn) : 0;
        uint64_t deltaOut = (gCur.netBytesOut > gLast.netBytesOut) ? (gCur.netBytesOut - gLast.netBytesOut) : 0;

        printf(formatStr, deltaIn,  deltaOut);
        //printf(formatStr, gCur.netBytesIn, gCur.netBytesOut);
    }
    
    // Update these for next iteration.
    gLast.netBytesIn = gCur.netBytesIn;
    gLast.netBytesOut = gCur.netBytesOut;
}

//
//  g e t _ p i d _ f o r _ p r o c e s s
//

// Returns 0 if there was no match.

pid_t get_pid_for_process (const char *procName)
{
    int processCount = proc_listpids (PROC_ALL_PIDS, 0, NULL, 0) / sizeof (pid_t);
    if (processCount < 1)
    {
        printf ("Only found %d processes running!\n", processCount);
        return -1;
    } else if (gVerbose) {
        printf ("%i processes now running\n", processCount);
    }
    
    // Allocate a few extra slots in case new processes are spawned
    int     allPidsSize = sizeof (pid_t) * (processCount + 3);
    pid_t   *allPids = (pid_t *) malloc (allPidsSize);
    
    // re-set process_count in case the number of processes changed (got smaller; we won't do bigger)
    processCount = proc_listpids (PROC_ALL_PIDS, 0, allPids, allPidsSize) / sizeof (pid_t);
    
    int     i;
    pid_t   highestPid = 0;
    int     matchCount = 0;
    
    for (i = 1; i < processCount; i++)
    {
        char pidPath[PATH_MAX];
        int pidPathLen = proc_pidpath (allPids[i], pidPath, sizeof (pidPath));
        
        if (pidPathLen == 0)
            continue;
        
        char *j = strrchr (pidPath, '/');
        
        if ((j == NULL && strcmp (procName, pidPath) == 0)
            || (j != NULL && strcmp (j + 1, procName)  == 0))
        {
            matchCount++;
            if (allPids[i] > highestPid)
                highestPid = allPids[i];
        }
    }
    free (allPids);
    
    if (matchCount == 0 && gVerbose)
    {
        printf ("Did not find process '%s'\n", procName);
    }
    
    if (matchCount > 1 && gVerbose)
    {
        printf ("Got %i matches for '%s'!, Defaulting to the highest-pid\n", matchCount, procName);
    }
    
    return highestPid;
}

//
//  c p u _ p r i n t _ s t a t s
//

void cpu_print_stat (int kind, double time)
{
    const char  *printFormatStandard = "%.2f, ";
    const char  *printFormatFMP = "$pd.%s=%*.2f;\n";
    const char  *formatStr = printFormatStandard;
    const char  *idleLable = "idle";
    const char  *sysLable = "system";
    const char  *usrLable = "user";
    const char  *lableStr;
    
    double cpu = rint(100. * gCur.load.cpu_ticks[kind] / (time ? time : 1));\

    if (gFmpFormat) {
        formatStr = printFormatFMP;
        switch (kind) {
            case CPU_STATE_USER:
                lableStr = usrLable;
                break;
            case CPU_STATE_SYSTEM:
                lableStr = sysLable;
                break;
            case CPU_STATE_IDLE:
                lableStr = idleLable;
                break;
        }
        
        printf(formatStr, lableStr, (100 == cpu) ? 4 : 3, cpu);
        
    } else {
        
        printf(formatStr, (100 == cpu) ? 4 : 3, cpu);
    }
}


//
//  c p u _ s t a t i s t i c s
//

double cpu_statistics (void)
{    
	mach_msg_type_number_t count;
	kern_return_t status;
	double time;
    
	/*
	 * Get CPU usage counters.
	 */
	count = HOST_CPU_LOAD_INFO_COUNT;
	status = host_statistics (gHostPort, HOST_CPU_LOAD_INFO, (host_info_t) &gCur.load, &count);
	if (status != KERN_SUCCESS) {
		perror ("couldn't fetch CPU stats");
        exit (1);
    }
    
	/*
	 * Make 'cur' fields relative, update 'last' fields to current values,
	 * calculate total elapsed time.
	 */
	time = 0.0;
    
	gCur.load.cpu_ticks[CPU_STATE_USER]
    -= gLast.load.cpu_ticks[CPU_STATE_USER];
    
	gLast.load.cpu_ticks[CPU_STATE_USER]
    += gCur.load.cpu_ticks[CPU_STATE_USER];
    
	time += gCur.load.cpu_ticks[CPU_STATE_USER];
    
	gCur.load.cpu_ticks[CPU_STATE_SYSTEM]
    -= gLast.load.cpu_ticks[CPU_STATE_SYSTEM];
    
	gLast.load.cpu_ticks[CPU_STATE_SYSTEM]
    += gCur.load.cpu_ticks[CPU_STATE_SYSTEM];
    
	time += gCur.load.cpu_ticks[CPU_STATE_SYSTEM];
    
	gCur.load.cpu_ticks[CPU_STATE_IDLE]
    -= gLast.load.cpu_ticks[CPU_STATE_IDLE];
    
	gLast.load.cpu_ticks[CPU_STATE_IDLE]
    += gCur.load.cpu_ticks[CPU_STATE_IDLE];
    
	time += gCur.load.cpu_ticks[CPU_STATE_IDLE];
	
    return time;
}

//
//  c p u _ p r i n t _ s t a t i s t i c s
//

void cpu_print_statistics (double inTime)
{
    // PRINT TIMES
    cpu_print_stat (CPU_STATE_USER, inTime);
	cpu_print_stat (CPU_STATE_SYSTEM, inTime);
	cpu_print_stat (CPU_STATE_IDLE, inTime);
}


void print_usage()
{
    printf ("usage: processdetail [-d] [-n name] [-p pid] [-w seconds] [-v]\n");
}


void print_verbose_info ()
{
    printf ("processdetail 1.23, compiled %s\n", __DATE__);
}

// http://stackoverflow.com/questions/11962289/how-can-i-retrieve-the-following-processor-stats
// http://stackoverflow.com/questions/15401363/retrieve-thread-name-in-ios-of-non-current-thread
// http://stackoverflow.com/questions/1543157/how-can-i-find-out-how-much-memory-my-c-app-is-using-on-the-mac

int main (int argc, char* argv[]) {
    
    mach_msg_type_number_t  actListCnt;
    thread_act_array_t      actList;
    double                  cpuTime;
    kern_return_t           kr;
    int                     i;
    char                    *procName;
    float                   scaledUsage;
    task_t                  task = 0;
    pid_t                   pid = -1;
    thread_basic_info_t     tbi;
    thread_basic_info_data_t tbiData;
    double                  waitSeconds = 0.0;
    struct timespec         tv;

    
    // Setup a Ctrl-C handler.
    //act.sa_handler = intHandler;
    //sigaction(SIGINT, &act, NULL);

    //
    // PARSE PARAMETERS
    //
    
    while ((i = getopt(argc, argv, "dn:p:vw:")) != -1) {
        
        switch (i) {

            case 'd':
                gFmpFormat = true;
                break;

            case 'h':
                print_usage();
                break;
            case 'n':
                procName = optarg;
                //printf ("Name: %s\n", procName);
                pid = get_pid_for_process(procName);
                break;

            case 'p':
                pid = atoi (optarg);
                break;

            case 'v':
                gVerbose = true;
                break;

            case 'w':
                //printf ("Wait: %s\n", argv[i+1]);
                waitSeconds = atof (optarg);
                
                if (waitSeconds <= 0.0)
                    waitSeconds = 0.0;
                if (waitSeconds > 0.0 && waitSeconds < 0.1)
                    waitSeconds = 0.1;
                break;

            default:
                printf ("Invalid arguments, please try again.\n");
                print_usage();
                exit(1);
                break;
        }
    }

    if (gVerbose) {
        print_verbose_info();
    }
    
    //printf ("PID: %i\n", pid);

    if (pid < 1) {
        printf ("No process found, please try again.\n");
        print_usage();
        return 1;
    }

    if (waitSeconds > 0.0) {
        /* Construct the timespec from the number of whole seconds...  */
        tv.tv_sec = (time_t) waitSeconds;
        /* ... and the remainder in nanoseconds.  */
        tv.tv_nsec = (long) ((waitSeconds - tv.tv_sec) * 1e+9);
    } else {
        // Default time if none specified.
        tv.tv_sec = (time_t) 1;
        tv.tv_nsec = 0;
    }
    
    //
    //  GET PROCESS INFO
    //
    
    task_basic_info_data_t basic_info;
    mach_msg_type_number_t count;

    // Get port for accessing system CPU stats.
	gHostPort = mach_host_self();
    
    do {

        // Need to load the task number?
        if (task == 0 && pid > 0) {
            if ((kr = task_for_pid(mach_task_self(), pid, &task)) != KERN_SUCCESS) {
                mach_error("processdetail", kr);
                perror ("Could not get Mach task for pid. Did you run with sudo?\n");
                exit (1);
            }
            
        }
        
        count = TASK_BASIC_INFO_COUNT;
        
        if ((kr = task_info(task, TASK_BASIC_INFO,
                            (task_info_t)&basic_info, &count)) != KERN_SUCCESS) {
                        
            if (gHasRunOnce && procName != NULL) {
                // We found the process originally, so we'll wait the regular interval and
                // see if the process restarts.
                gKeepRunning = nanosleep(&tv, NULL) == 0;
                if (!gKeepRunning) printf ("\n"); // Return after Ctrl-C
                
                pid = get_pid_for_process(procName);
                
                // Force the task number to be reloaded.
                task = 0;
                
                continue;
                
            } else {
                // Shouldn't happen unless process died in between getting pid and here.
                perror ("Process being monitored died!?\n");
                mach_error("processdetail", kr);
                exit(1);
            }
        }
        
        if (count != TASK_BASIC_INFO_COUNT) {
            fprintf(stderr, "size mismatch");
            exit(1);
        }
    
        // CPU STATS
        
        cpuTime = cpu_statistics();
        
        // NET STATS
        
        if (net_stats() != 0) {
            perror ("Error getting network stats");
            exit (1);
        }
        
         // There is a wait interval and this is the very first run?
        if (gHasRunOnce == false) {
            // Skip printing until we have a full interval.
            // We can then give difference between before & after.
            gHasRunOnce = true;
            
            // Update values but don't print.
            net_print_stats(false);
            
            gKeepRunning = nanosleep(&tv, NULL) == 0;
            if (!gKeepRunning) printf ("\n");
            continue;
        }

        //
        //  PRINT SYSTEM STATS
        //
        
        print_timestamp();
        
        cpu_print_statistics(cpuTime);
        
        net_print_stats(true);
        
        //
        //  PRINT PROCESS SPECIFIC STATS
        //
        
        if (gFmpFormat) {
            printf ("$pd.pid=%i;\n$pd.vmsize=%li;\n$pd.residentmemory=%li;\n", pid, basic_info.virtual_size,basic_info.resident_size);
        } else {
            printf ("%i, %li, %li, ", pid, basic_info.virtual_size,basic_info.resident_size);
            //printf("Suspend count: %d\n", basic_info.suspend_count);
        }
        
        task_events_info_data_t events_info;
        
        if ((kr = task_info(task, TASK_EVENTS_INFO,
                            (task_info_t)&events_info, &count)) != KERN_SUCCESS) {
            mach_error("processdetail", kr);
            exit(1);
        }
        if (count != TASK_EVENTS_INFO_COUNT) {
            fprintf(stderr, "size mismatch");
            exit(1);
        }
        
        if (gFmpFormat) {
            printf ("$pd.pagefaults=%d;\n$pd.pageins=%d;\n$pd.copyonwritefaults=%d;\n", events_info.faults, events_info.pageins, events_info.cow_faults);
        } else {
            //printf("Page faults: %d\n", events_info.faults);
            printf("%d, ", events_info.faults);
            //printf("Pageins: %d\n", events_info.pageins);
            printf("%d, ", events_info.pageins);
            //printf("Copy-on-write faults: %d\n", events_info.cow_faults);
            printf("%d, ", events_info.cow_faults);
            //printf("Messages sent: %d\n", events_info.messages_sent);
            //printf("Messages received: %d\n", events_info.messages_received);
            //printf("Mach system calls: %d\n", events_info.syscalls_mach);
            //printf("Unix system calls: %d\n", events_info.syscalls_unix);
            //printf("Context switches: %d\n", events_info.csw);
        }
        
        if (gFmpFormat) {
            switch (basic_info.policy) {
                case THREAD_STANDARD_POLICY:
                    printf("$pd.threadpolicy=Standard;\n");
                    break;
                case THREAD_TIME_CONSTRAINT_POLICY:
                    printf("$pd.threadpolicy=\"Time Constraint\";\n");
                    break;
                case THREAD_PRECEDENCE_POLICY:
                    printf("$pd.threadpolicy=Precendence;\n");
                    break;
                default:
                    printf("$pd.threadpolicy=Unknown;\n");
                    break;
            }
    
        } else {
            switch (basic_info.policy) {
                case THREAD_STANDARD_POLICY:
                    printf("Standard, ");
                    break;
                case THREAD_TIME_CONSTRAINT_POLICY:
                    printf("\"Time Constraint\", ");
                    break;
                case THREAD_PRECEDENCE_POLICY:
                    printf("Precendence, ");
                    break;
                default:
                    printf("Unknown, ");
                    break;
            }
        }

        kr = task_threads (task, &actList, &actListCnt);
        if (kr != KERN_SUCCESS) {
            fprintf(stderr, "task_threads failed");
            exit (1);
        }
        
        if (gFmpFormat) {
            printf ("$td.threadcount=%i;\n", actListCnt);
        } else {
            printf ("%i, ", actListCnt);
        }

        mach_msg_type_number_t threadInfoCnt;
        float cpuUsage;
        
        
        /*
        
         Never a pthread (!?) so this doesn't work.
        for (int i = 0; i < actListCnt; ++i) {
            pthread_t pt = pthread_from_mach_thread_np(actList[i]);
            if (pt) {
                name[0] = '\0';
                int rc = pthread_getname_np(pt, name, sizeof name);
                printf("mach thread %u: getname returned %d: %s\n", actList[i], rc, name);
            } else {
                printf("mach thread %u: no pthread found\n", actList[i]);
            }
        }
    */


        tbi = &tbiData;
        cpuUsage = 0.0;
        
        for (i = 0; i < actListCnt; i++) {
            threadInfoCnt = THREAD_BASIC_INFO_COUNT;
            kr = thread_info(
                             actList[i],                            //thread_act_t target_act
                             THREAD_BASIC_INFO,                     //thread_flavor_t flavor
                             (thread_info_t) tbi,                   //thread_info_t thread_info_out
                             &threadInfoCnt);                       //mach_msg_type_number_t *thread_info_outCnt

            
            cpuUsage += tbi->cpu_usage;
            //printf ("CPU Usage: %i\n", tbi->cpu_usage);
            scaledUsage = tbi->cpu_usage / (float)TH_USAGE_SCALE * 100.0;
            
            if (gFmpFormat) {
                printf ("$pd.threadcpu[%i]=%.2f;\n", i+1, scaledUsage);
            } else {
                printf ("%.2f, ", scaledUsage);
            }
        }

        if (gFmpFormat) {
            printf ("$pd.totalcpu=%.2f\n\n", cpuUsage / (float)TH_USAGE_SCALE * 100.0);
        } else {
            printf ("%.2f\n", cpuUsage / (float)TH_USAGE_SCALE * 100.0);
        }
        
        // If user is tailing the piped result we want them to see results quickly.
        fflush (stdout);
        
        // We've done the whole process at least once.
        gHasRunOnce = true;
        
        if (waitSeconds != 0.0) {
            gKeepRunning = nanosleep(&tv, NULL) == 0;
        } else {
            gKeepRunning = false;
        }
        
    } while (gKeepRunning);
    
    return 0;
    
}


/*
 kern_return_t task_threads
 (
 task_t target_task,
 thread_act_array_t *act_list,
 mach_msg_type_number_t *act_listCnt
 );
*/


