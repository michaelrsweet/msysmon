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
#  include <ctype.h>
#  include <errno.h>
#  include <poll.h>
#  include <cups/cups.h>
#  include <cups/thread.h>
#  ifdef __APPLE__
#    include <sys/param.h>
#  endif // __APPLE__


//
// Macros...
//

#  ifdef DEBUG
#    define MSYSMON_DEBUG(...) fprintf(stderr, __VA_ARGS__)
#  else
#    define MSYSMON_DEBUG(...)
#  endif // DEBUG


//
// Constants...
//

#  define DEFAULT_CPU_LIMIT	25	// Default CPU limit for monitoring
#  define DEFAULT_INTERVAL	300	// Default interval between data samples
#  define DEFAULT_MEM_LIMIT	5	// Default memory limit (percent) for monitoring
#  define DEFAULT_PORT		8080	// Default port number
#  define MAX_COMMAND 		128	// Maximum length of command name
#  define MAX_COMMANDS		10	// Maximum number of commands to follow
#  define MAX_DATA		2000	// Maximum number of data samples, enough for about 1 week with 5 minute samples
#  define MAX_PROCS		100	// Maximum number of processes to follow


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
  bool		seen;			// Has the process been seen?
  char		command[MAX_COMMAND];	// Command name
  time_t	start_time;		// Start time for this process
  time_t	end_time;		// End time for this process
  time_t	data_start;		// Start time for data samples
  unsigned	num_data;		// Number of system data samples
  msysmon_data_t data[MAX_DATA];	// Process data samples
} msysmon_proc_t;

typedef struct msysmon_db_s		// Record of all data
{
  int		interval;		// Sampling interval
  uint16_t	cpu_limit;		// CPU monitoring limit (percent)
  uint32_t	mem_limit;		// Memory limit (kibibytes)
  int		port;			// Listen port
  unsigned	num_commands;		// Number of commands
  char		commands[MAX_COMMANDS][MAX_COMMAND];
					// Command names

  cups_rwlock_t	rwlock;			// Reader/writer lock
  nfds_t	num_listeners;		// Number of listeners
  struct pollfd	listeners[2];		// Listeners
  time_t	data_start;		// Start time for data samples
  unsigned	num_data;		// Number of system data samples
  msysmon_data_t data[MAX_DATA];	// System data samples
  unsigned	num_processes;		// Number of processes being followed
  msysmon_proc_t processes[MAX_PROCS];	// Per-process data
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
