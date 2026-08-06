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
#endif // __APPLE__


//
// 'msysmonCollectData()' - Collect data.
//

bool					// O - `true` on success, `false` on error
msysmonCollectData(void)
{
  return (true);
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
    memsize = 16UL * 1024UL * 1024UL * 1024UL;

  return ((uint32_t)(mem / 1024));

#else
#endif // __APPLE__
}
