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
#  include <sys/types.h>
#  include <sys/sysctl.h>
#  include <mach/mach_host.h>
#  include <mach/vm_page_size.h>
#  include <mach/vm_statistics.h>
#endif // __APPLE__


//
// Local functions...
//

static bool	get_system_info(void);


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
  int		ncpu;			// Number of CPUs
  struct loadavg loadavg;		// Load average
  size_t	size;			// Sysctl size
  vm_statistics64_data_t vminfo;	// Memory/swap usage
  mach_msg_type_number_t vmcount = HOST_VM_INFO64_COUNT;
					// ... size
  int		vmerr;			// Kernel error code
  int		pidsize;		// Size of process IDs


  size = sizeof(ncpu);
  if (sysctlbyname("hw.ncpu", &ncpu, &size, /*newp*/NULL, /*newlen*/0))
    ncpu = 1;

  MSYSMON_DEBUG("ncpu=%d\n", ncpu);

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