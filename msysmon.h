//
// Header file for Mike's System Monitor.
//
// Copyright © 2026 by Michael R Sweet
//
// Licensed under Apache License v2.0.  See the file "LICENSE" for more
// information.
//

#ifndef MSYSMON_H
#  define MSYSMON_H
#  include <stdio.h>
#  include <stdlib.h>
#  include <stdint.h>
#  include <stdbool.h>
#  include <string.h>
#  include <poll.h>
#  include <cups/cups.h>
#  include <cups/thread.h>
#  ifdef __APPLE__
#    include <sys/param.h>
#  endif // __APPLE__


//
// Constants...
//

#  define DATA_INTERVAL		60	// Interval between data samples
#  define DATA_WEEKS		4	// How many weeks worth of data
#  define DEFAULT_CPU_LIMIT	10	// Default CPU limit for monitoring
#  define DEFAULT_MEM_LIMIT	5	// Default memory limit (percent) for monitoring
#  define DEFAULT_PORT		10080	// Default port number
#  ifdef MAXCOMLEN
#    define MAX_COMMAND MAXCOMLEN	// Maximum length of command name
#  else
#    define MAX_COMMAND 128
#  endif // MAXCOMLEN
#  define MAX_COMMANDS		10	// Maximum number of commands to follow
#  define MAX_PROCESSES		10000	// Maximum number of processes to follow


//
// Types...
//

typedef struct msysmon_data_s		// Common monitoring data
{
  uint16_t 	cpu_percent;		// CPU as a percentage
  uint16_t	tp_count;		// Thread/process count
  uint32_t	mem_k;			// Memory in kibibytes
} msysmon_data_t;

typedef struct msysmon_proc_s		// Record of per-process data
{
  pid_t		pid;			// Process ID
  char		command[MAX_COMMAND];	// Command name
  time_t	start_time;		// Start time for this process
  unsigned	num_data;		// Number of system data samples
  msysmon_data_t data[DATA_WEEKS * 7 * 24 * 60 * 60 / DATA_INTERVAL];
					// Process data samples
} msysmon_proc_t;

typedef struct msysmon_db_s		// Record of all data
{
  uint16_t	cpu_limit;		// CPU monitoring limit (percent)
  uint32_t	mem_limit;		// Memory limit (kibibytes)
  int		port;			// Listen port
  unsigned	num_commands;		// Number of commands
  char		commands[MAX_COMMANDS][MAX_COMMAND];
					// Command names

  cups_rwlock_t	rwlock;			// Reader/writer lock
  time_t	start_time;		// Start time for all records
  nfds_t	num_listeners;		// Number of listeners
  struct pollfd	listeners[2];		// Listeners
  unsigned	num_data;		// Number of system data samples
  msysmon_data_t data[DATA_WEEKS * 7 * 24 * 60 * 60 / DATA_INTERVAL];
					// System data samples
  unsigned	num_processes;		// Number of processes being followed
  msysmon_proc_t processes[MAX_PROCESSES];
					// Per-process data
} msysmon_db_t;


//
// Globals...
//

extern msysmon_db_t	msysmonData;


//
// Functions...
//

extern bool	msysmonCollectData(void);
extern uint32_t	msysmonGetSystemMemory(void);
extern void	*msysmonRunWebIf(http_t *http);


#endif // !MSYSMON_H
