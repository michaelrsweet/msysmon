//
// Data collection code for Mike's System Monitor.
//
// Copyright © 2026 by Michael R Sweet
//
// Licensed under Apache License v2.0.  See the file "LICENSE" for more
// information.
//

#include "msysmon.h"
#ifdef __APPLE__
#  include <libproc.h>
#  include <sys/proc_info.h>
#  include <sys/types.h>
#  include <sys/sysctl.h>
#  include <mach/mach_host.h>
#  include <mach/vm_page_size.h>
#  include <mach/vm_statistics.h>
#endif // __APPLE__


//
// Local functions...
//

static msysmon_proc_t	*add_process(pid_t pid, const char *command);
static msysmon_proc_t	*find_process(pid_t pid);
static int		get_num_cpus(void);
static bool		get_process_info(void);
static bool		get_system_info(void);


//
// 'msysmonCollectData()' - Collect data.
//

bool					// O - `true` on success, `false` on error
msysmonCollectData(void)
{
  bool	ret = true;			// Return value


  MSYSMON_DEBUG("Collecting data...\n");

  cupsRWLockWrite(&msysmonData.rwlock);
  MSYSMON_DEBUG("Write lock...\n");

  ret &= get_system_info();

  ret &= get_process_info();

  cupsRWUnlock(&msysmonData.rwlock);
  MSYSMON_DEBUG("Unlocked...\n");

  return (ret);
}


//
// 'msysmonGetSystemMemory()' - Get the amount of real memory in kibibytes.
//

uint32_t				// O - Real memory in kibibytes
msysmonGetSystemMemory(void)
{
#ifdef __APPLE__
  int64_t	mem;			// Memory in bytes
  size_t	memsize;		// Size of memory

  memsize = sizeof(mem);

  if (sysctlbyname("hw.memsize", &mem, &memsize, /*newp*/NULL, /*newlen*/0))
  {
    fprintf(stderr, "msysmon: Unable to get hw.memsize: %s\n", strerror(errno));
    memsize = 16UL * 1024UL * 1024UL * 1024UL;
  }

  return ((uint32_t)(mem / 1024));

#else
#endif // __APPLE__
}


//
// 'add_process()' - Add a process to the list of processes.
//

static msysmon_proc_t *			// O - Process data or `NULL` if there is no room
add_process(pid_t      pid,		// I - Process ID
            const char *command)	// I - Command name
{
  unsigned	i;			// Looping var
  msysmon_proc_t *proc;			// Process data


  if (msysmonData.num_processes < MAX_PROCS)
  {
    // Add process to the end of the list...
    proc = msysmonData.processes + msysmonData.num_processes;
    msysmonData.num_processes ++;
  }
  else
  {
    // Find a completed process we can "forget"...
    for (i = msysmonData.num_processes, proc = msysmonData.processes; i > 0; i --, proc ++)
    {
      if (proc->end_time)
        break;
    }

    if (i == 0)
      return (NULL);

    // Remove the process and add the new one at the end...
    if (i > 1)
      memmove(proc, proc + 1, (i - 1) * sizeof(msysmon_proc_t));

    proc = msysmonData.processes + MAX_PROCS - 1;
  }

  // Initialize the process data...
  memset(proc, 0, sizeof(msysmon_proc_t));

  proc->pid = pid;

  strncpy(proc->command, command, sizeof(proc->command) - 1);

  proc->seen       = true;
  proc->start_time = proc->data_start = time(NULL);

  return (proc);
}


//
// 'find_process()' - Find a process in the list of processes.
//

static msysmon_proc_t *			// O - Process data or `NULL` if not found
find_process(pid_t pid)			// I - Process ID
{
  unsigned	i;			// Looping var
  msysmon_proc_t *proc;			// Process data


  for (i = msysmonData.num_processes, proc = msysmonData.processes; i > 0; i --, proc ++)
  {
    if (proc->pid == pid)
      return (proc);
  }

  return (NULL);
}


//
// 'get_num_cpus()' - Get the number of CPUs/cores.
//

static int				// O - Number of CPUs
get_num_cpus(void)
{
  int		ncpu = 1;		// Number of CPUs
#ifdef __APPLE__
  size_t	size;			// Sysctl size


  size = sizeof(ncpu);
  sysctlbyname("hw.ncpu", &ncpu, &size, /*newp*/NULL, /*newlen*/0);
#else // Linux
#endif // __APPLE__

  MSYSMON_DEBUG("ncpu=%d\n", ncpu);

  return (ncpu);
}


//
// 'get_process_info()' - Get information about processes on the system.
//

static bool				// O - `true` on success, `false1` on error
get_process_info(void)
{
  unsigned	i, j;			// Looping vars
  bool		ret = true;		// Return value
  msysmon_proc_t *proc;			// Current process
  time_t	curtime = time(NULL);	// Current time
  static bool	reported_too_many = false;
					// Have we reported there are too many processes?


  cupsRWLockWrite(&msysmonData.rwlock);

  // Flag all running processes as "not seen"
  for (i = msysmonData.num_processes, proc = msysmonData.processes; i > 0; i --, proc ++)
    proc->seen = proc->end_time > 0;

#ifdef __APPLE__
  int		ncpu;			// Number of CPUs
  int		pidsize;		// Size of process IDs
  unsigned	num_pids;		// Number of process IDs
  pid_t		*pids = NULL;		// Processes
  char		command[MAX_COMMAND];	// Process name
  struct proc_taskinfo pinfo;		// Process task information
  struct proc_threadinfo tinfo;		// Process thread information
  uint16_t	cpu_percent;		// CPU use in percent
  uint32_t	mem_k;			// Memory use in kibibytes
  uint16_t	tp_count;		// Thread count

  if ((pidsize = proc_listpids(PROC_ALL_PIDS, /*typeinfo*/0, /*buffer*/NULL, /*buffersize*/0)) <= 0 || (pids = calloc(1, pidsize)) == NULL)
  {
    ret = false;
  }
  else
  {
    proc_listpids(PROC_ALL_PIDS, /*typeinfo*/0, pids, pidsize);

    ncpu = get_num_cpus();

    for (i = 0, num_pids = pidsize / sizeof(pid_t); i < num_pids; i ++)
    {
      // Stop early if a process disappears...
      if (!pids[i])
        break;

      if ((proc = find_process(pids[i])) != NULL)
        proc->seen = true;

      if (proc_name(pids[i], command, sizeof(command)) <= 0)
      {
        MSYSMON_DEBUG("PID-%d: Unable to get command name (%s)\n", (int)pids[i], strerror(errno));
        continue;
      }

      MSYSMON_DEBUG("PID-%d: command=\"%s\"\n", (int)pids[i], command);

      if (proc_pidinfo(pids[i], PROC_PIDTASKINFO, /*arg*/0, &pinfo, PROC_PIDTASKINFO_SIZE) <= 0)
      {
        MSYSMON_DEBUG("PID-%d: Unable to get task information (%s)\n", (int)pids[i], strerror(errno));
        mem_k    = 0;
        tp_count = 1;
      }
      else
      {
	MSYSMON_DEBUG("PID-%d: pinfo.pti_virtual_size = %lu\n", (int)pids[i], (unsigned long)pinfo.pti_virtual_size);
	MSYSMON_DEBUG("PID-%d: pinfo.pti_resident_size = %lu\n", (int)pids[i], (unsigned long)pinfo.pti_resident_size);
	MSYSMON_DEBUG("PID-%d: pinfo.pti_threadnum = %d\n", (int)pids[i], (int)pinfo.pti_threadnum);

	mem_k    = (uint32_t)(pinfo.pti_resident_size / 1024);
	tp_count = (uint16_t)pinfo.pti_threadnum;
      }

      if (proc_pidinfo(pids[i], PROC_PIDTHREADINFO, /*arg*/0, &tinfo, PROC_PIDTHREADINFO_SIZE) <= 0)
      {
        MSYSMON_DEBUG("PID-%d: Unable to get thread information (%s)\n", (int)pids[i], strerror(errno));

        cpu_percent = 0;
      }
      else
      {
	MSYSMON_DEBUG("PID-%d: tinfo.pth_cpu_usage = %d\n", (int)pids[i], tinfo.pth_cpu_usage);

	cpu_percent = tinfo.pth_cpu_usage / ncpu;
      }

      // See if we need to follow this process...
      if (!proc)
      {
        for (j = 0; j < msysmonData.num_commands; j ++)
        {
          if (!strcmp(command, msysmonData.commands[j]))
            break;
        }

        if (j < msysmonData.num_commands || cpu_percent >= msysmonData.cpu_limit || mem_k >= msysmonData.mem_limit)
        {
          if ((proc = add_process(pids[i], command)) == NULL && !reported_too_many)
          {
            fputs("msysmon: Too many interesting processes.\n", stderr);
            reported_too_many = true;
          }
        }
      }

      if (proc)
      {
        msysmon_data_t *data;		// Current data sample

        if (proc->num_data >= MAX_DATA)
        {
          memmove(proc->data, proc->data + 1, (MAX_DATA - 1) * sizeof(msysmon_data_t));
          data = proc->data + MAX_DATA - 1;

          proc->data_start += msysmonData.interval;
        }
        else
        {
          data = proc->data + proc->num_data;
          proc->num_data ++;
        }

        data->cpu_percent = cpu_percent;
        data->tp_count    = tp_count;
        data->mem_k       = mem_k;
      }
    }

    free(pids);
  }

#else // Linux
#endif // __APPLE__

  // Mark all unseen processes as terminated/done/ended
  for (i = msysmonData.num_processes, proc = msysmonData.processes; i > 0; i --, proc ++)
  {
    if (!proc->seen)
      proc->end_time = curtime;
  }

  cupsRWUnlock(&msysmonData.rwlock);

  return (ret);
}


//
// 'get_system_info()' - Get the system load and memory usage.
//

static bool				// O - `true` on success, `false1` on error
get_system_info(void)
{
  bool		ret = true;		// Return value
  uint16_t	cpu = 0;		// CPU load as a percentage
  uint32_t	mem = 0;		// Real memory usage in kibibytes
  uint16_t	nprocesses = 0;		// Process count


#ifdef __APPLE__
  // Get system values via sysctlbyname...
  size_t	size;			// Sysctl size
  int		ncpu;			// Number of CPUs
  struct loadavg loadavg;		// Load average
  vm_statistics64_data_t vminfo;	// Memory/swap usage
  mach_msg_type_number_t vmcount = HOST_VM_INFO64_COUNT;
					// ... size
  int		vmerr;			// Kernel error code
  int		pidsize;		// Size of process IDs


  ncpu = get_num_cpus();

  size = sizeof(loadavg);
  if (sysctlbyname("vm.loadavg", &loadavg, &size, /*newp*/NULL, /*newlen*/0))
  {
    fprintf(stderr, "msysmon: Unable to get vm.loadavg: %s\n", strerror(errno));
    ret = false;
  }
  else
  {
    MSYSMON_DEBUG("loadavg.ldavg  = [%d %d %d]\n", loadavg.ldavg[0], loadavg.ldavg[1], loadavg.ldavg[2]);
    MSYSMON_DEBUG("loadavg.fscale = %ld\n", loadavg.fscale);

    cpu = (uint16_t)(100 * loadavg.ldavg[0] / loadavg.fscale / ncpu);
  }

  if ((vmerr = host_statistics64(mach_host_self(), HOST_VM_INFO64, (host_info64_t)&vminfo, &vmcount)) != KERN_SUCCESS)
  {
    fprintf(stderr, "msysmon: Unable to get VM statistics: %d\n", vmerr);
    ret = false;
  }
  else
  {
    MSYSMON_DEBUG("vminfo.free_count     = %u\n", (unsigned)vminfo.free_count);
    MSYSMON_DEBUG("vminfo.active_count   = %u\n", (unsigned)vminfo.active_count);
    MSYSMON_DEBUG("vminfo.inactive_count = %u\n", (unsigned)vminfo.inactive_count);
    MSYSMON_DEBUG("vminfo.wire_count     = %u\n", (unsigned)vminfo.wire_count);
    MSYSMON_DEBUG("vm_kernel_page_size   = %u\n", (unsigned)vm_kernel_page_size);

    mem = (uint32_t)(vminfo.active_count * (vm_kernel_page_size / 1024));
  }

  if ((pidsize = proc_listpids(PROC_ALL_PIDS, /*typeinfo*/0, /*buffer*/NULL, /*buffersize*/0)) > 0)
    nprocesses = (uint16_t)(pidsize / sizeof(pid_t));

#else // Linux
  FILE		*fp;			// File in /proc
  char		line[1024],		// Line from file
		*value;			// Value in line


#endif // __APPLE__

  if (ret)
  {
    // Save current cpu/memory/process values
    MSYSMON_DEBUG("cpu_percent=%u, tp_count=%u, mem_k=%u\n", cpu, nprocesses, mem);

    if (msysmonData.num_data >= MAX_DATA)
    {
      // Drop first data sample...
      msysmonData.num_data --;
      memmove(msysmonData.data, msysmonData.data + 1, msysmonData.num_data * sizeof(msysmon_data_t));
    }

    msysmon_data_t *data = msysmonData.data + msysmonData.num_data;

    data->cpu_percent = cpu;
    data->tp_count    = nprocesses;
    data->mem_k       = mem;

    msysmonData.num_data ++;
  }

  // Return...
  return (ret);
}