## processdetail

### Introduction
This is an executable that prints out the CPU usage for a given process, including its threads. The format is designed to be easily consumed by other scripts or FileMaker (the monitoring of FileMaker Server script processes was a key goal).

### Installation
The suggested installation (after building if required) is to simply copy the file to /usr/local/bin.

To ensure the proper execute permissions are in place, you may need to run the following command:
```
sudo chmod ugo+x /usr/local/bin/processdetail
```

### Options

option	| description
------- | -----------
-d	| format so that output can be used in a FileMaker Evaluate function when wrapped in Let
-n	| process name to montor, eg "-n fmsased"
-p	| pid (process number) to monitor
-v	| version information
-w	| wait time, in seconds (can use fractions)

It expects either a -p or -n option to specify the process to monitor. If using the name, it always uses the process with the highest process ID.

Output In the default format a comma delimited list of values is provided. If you were to check on Finder you might get:
```
$ sudo /opt/local/bin/processdetail -n Finder
19:00:37.272129, 4.00, 7.00, 89.00, 330, 3043696640, 121090048, 1415607, 1744, 4683, Standard, 5, 1.00, 0.00, 0.00, 0.00, 0.00, 1.00
19:00:39.273273, 7.00, 7.00, 86.00, 330, 3044233216, 121155584, 1415721, 1746, 4683, Standard, 6, 0.30, 0.00, 0.00, 0.00, 0.00, 0.00, 0.30
19:00:41.274397, 6.00, 7.00, 88.00, 330, 3046682624, 122781696, 1416186, 1746, 4683, Standard, 7, 0.80, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.80
```

These values correspond to the following fields:

1.  time stamp w/ microseconds
2.  cpu usage for user processes
3.  cpu usage for system processes
4.  cpu idle
5.  network bytes in
6.  network bytes out
7.  process ID being monitored
8.  VM size
9.  resident memory used
10.  page fault count
11.  page in count
12.  copy-on-write fault count
13.  thread policy
14.  thread count
15.  cpu usage for thread 1
16.  cpu usage for thread 2
17.  cpu usage for thread 3
18.  cpu usage for thread 4
19.  cpu usage for thread 5
20.  total cpu usage

With the alternate format specified with -d you get output like this example:

```
$ sudo /opt/local/bin/processdetail -n Finder -d
$pd.timestamp="19:09:29.285017";
$pd.user=6.00;
$pd.system=8.00;
$pd.idle=86.00;
$pd.netin=42984;
$pd.netout=20207
$pd.pid=330;
$pd.vmsize=3040100352;
$pd.residentmemory=116277248;
$pd.pagefaults=1425079;
$pd.pageins=1752;
$pd.copyonwritefaults=4728;
$pd.threadpolicy=Standard;
$td.threadcount=8;
$pd.threadcpu[1]=7.30;
$pd.threadcpu[2]=0.00;
$pd.threadcpu[3]=0.00;
$pd.threadcpu[4]=0.00;
$pd.threadcpu[5]=0.00;
$pd.threadcpu[6]=0.00;
$pd.threadcpu[7]=0.60;
$pd.threadcpu[8]=0.30;
$pd.totalcpu=8.20
```

### Monitoring with the processdetail command

This command by itself will work well for monitoring for short periods of time, but if you'd like to monitor while the system is unattended or integrate with your FileMaker system it is insufficient. We'll need a few other pieces.

First, a script to start the needed command and capture its output. We'll create this in a file via sudo nano /usr/local/sbin/processdetail_logger.sh.
```
#!/bin/bash

# PURPOSE
#
# Startup the processdetail command to log system and FMS script engine activity to a log file.
#
# HISTORY
#
# 2014-01-16 simon_b: created file
#
# NOTES
#
# We use a newsyslog .conf file entry to handle log rotation:
#    /var/log/processdetail.log        644        5        5120        *        J
#

logPath=/var/log/
logName=processdetail.log

# Stop any current processdetail process to avoid having two at once.
killall processdetail 2>/dev/null

# Start the processdetail command, appending one line's output to log every 30 seconds
/usr/local/bin/processdetail -n fmsased -w 30 >> $logPath/$logName &
Next up is to create a crontab entry to start the log every hour (which may restart it, but that's ok). Enter the following with sudo crontab -e:

0 * * * *    /usr/local/sbin/processdetail_logger.sh
If desired, this command will allow a FileMaker server-side schedule to import the log data:

cd "/Library/FileMaker Server/Data/Documents"; sudo ln -s /var/log/processdetail.log
Now, make the logs get rotated by creating a newsyslog entry with sudo nano /etc/newsyslog.d/processdetail.conf:

/var/log/processdetail.log        644        5        5120        *        J
```
