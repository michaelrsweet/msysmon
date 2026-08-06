//
// Main entry for Mike's System Monitor.
//
// Copyright © 2026 by Michael R Sweet
//
// Licensed under Apache License v2.0.  See the file "LICENSE" for more
// information.
//

#include "msysmon.h"


//
// Globals...
//

msysmon_db_t	msysmonData;		// Monitoring data/settings


//
// Local functions...
//

static int	usage(FILE *out);


//
// 'main()' - Main entry.
//

int					// O - Exit status
main(int  argc,				// I - Number of command-line arguments
     char *argv[])			// I - Command-line arguments
{
  int		i;			// Looping var
  const char	*opt;			// Current option
  double	val;			// Value
  char		*end;			// Pointer into value
  char		service[255];		// Service port
  http_addrlist_t *addrlist;		// Listen address
  time_t	collect_time;		// Next data collection time


  // Initialize data...
  memset(&msysmonData, 0, sizeof(msysmonData));

  cupsRWInit(&msysmonData.rwlock);

  msysmonData.port      = DEFAULT_PORT;
  msysmonData.cpu_limit = DEFAULT_CPU_LIMIT;
  msysmonData.mem_limit = DEFAULT_MEM_LIMIT * msysmonGetSystemMemory() / 100;

  // Parse command-line...
  for (i = 1; i < argc; i ++)
  {
    if (!strcmp(argv[i], "--help"))
    {
      return (usage(stdout));
    }
    else if (!strcmp(argv[i], "--version"))
    {
      puts(VERSION);
      return (0);
    }
    else if (!strncmp(argv[i], "--", 2) || argv[i][0] != '-')
    {
      fprintf(stderr, "msysmon: Unknown option '%s'.\n", argv[i]);
      return (usage(stderr));
    }
    else
    {
      for (opt = argv[i] + 1; *opt; opt ++)
      {
        switch (*opt)
        {
          case 'C' : // -C CPU-LIMIT
              i ++;
              if (i >= argc)
              {
                fputs("msysmon: Missing port number after '-p'.\n", stderr);
                return (usage(stderr));
              }

	      if ((val = strtod(argv[i], &end)) < 1 || val > 100 || *end)
	      {
	        fprintf(stderr, "msysmon: Bad CPU limit '%s'.\n", argv[i]);
	        return (usage(stderr));
	      }

	      msysmonData.cpu_limit = (int)val;
              break;

          case 'M' : // -M MEM-LIMIT
              i ++;
              if (i >= argc)
              {
                fputs("msysmon: Missing port number after '-p'.\n", stderr);
                return (usage(stderr));
              }

	      if ((val = strtod(argv[i], &end)) < 1)
	      {
	        fprintf(stderr, "msysmon: Bad memory limit '%s'.\n", argv[i]);
	        return (usage(stderr));
	      }
	      else if (*end == '%')
	      {
	        msysmonData.mem_limit = (uint32_t)(val * msysmonGetSystemMemory() / 100.0);
	      }
	      else if (*end == 'k')
	      {
	        msysmonData.mem_limit = (uint32_t)val;
	      }
	      else if (*end == 'm')
	      {
	        msysmonData.mem_limit = (uint32_t)(val / 1024.0);
	      }
	      else if (*end == 'g')
	      {
	        msysmonData.mem_limit = (uint32_t)(val / 1024.0 / 1024.0);
	      }
	      else if (*end)
	      {
	        fprintf(stderr, "msysmon: Bad memory limit '%s'.\n", argv[i]);
	        return (usage(stderr));
	      }
	      else
	      {
	        msysmonData.mem_limit = (uint32_t)(val / 1024.0);
	      }
              break;

          case 'p' : // -p PORT-NUMBER
              i ++;
              if (i >= argc)
              {
                fputs("msysmon: Missing port number after '-p'.\n", stderr);
                return (usage(stderr));
              }

	      if ((val = strtod(argv[i], &end)) < 1 || val > 65535 || *end)
	      {
	        fprintf(stderr, "msysmon: Bad port number '%s'.\n", argv[i]);
	        return (usage(stderr));
	      }

	      msysmonData.port = (int)val;
              break;

          case 'w' : // -w COMMAND
              i ++;
              if (i >= argc)
              {
                fputs("msysmon: Missing command after '-w'.\n", stderr);
                return (usage(stderr));
              }

	      if (msysmonData.num_commands < MAX_COMMANDS)
	      {
	        strncpy(msysmonData.commands[msysmonData.num_commands], argv[i], sizeof(msysmonData.commands[0]) - 1);
	      }
	      else
	      {
	        fputs("msysmon: Too many commands to watch.\n", stderr);
	        return (usage(stderr));
	      }
              break;

          default :
              fprintf(stderr, "msysmon: Unknown option '-%c'.\n", *opt);
              return (usage(stderr));
        }
      }
    }
  }

  // Create listeners...
  snprintf(service, sizeof(service), "%d", msysmonData.port);

  if ((addrlist = httpAddrGetList(/*name*/NULL, AF_INET, service)) == NULL)
  {
    fprintf(stderr, "msysmon: Unable to lookup IPv4 listen address: %s\n", cupsGetErrorString());
    return (1);
  }
  else
  {
    if ((msysmonData.listeners[msysmonData.num_listeners].fd = httpAddrListen(&(addrlist->addr), msysmonData.port)) >= 0)
    {
      msysmonData.listeners[msysmonData.num_listeners].events = POLLIN | POLLERR | POLLHUP;
      msysmonData.num_listeners ++;
    }

    httpAddrFreeList(addrlist);
  }

  if ((addrlist = httpAddrGetList(/*name*/NULL, AF_INET6, service)) == NULL)
  {
    fprintf(stderr, "msysmon: Unable to lookup IPv4 listen address: %s\n", cupsGetErrorString());
    return (1);
  }
  else
  {
    if ((msysmonData.listeners[msysmonData.num_listeners].fd = httpAddrListen(&(addrlist->addr), msysmonData.port)) >= 0)
    {
      msysmonData.listeners[msysmonData.num_listeners].events = POLLIN | POLLERR | POLLHUP;
      msysmonData.num_listeners ++;
    }

    httpAddrFreeList(addrlist);
  }

  if (msysmonData.num_listeners == 0)
  {
    fputs("msysmon: Unable to create IPv4/6 listeners.\n", stderr);
    return (1);
  }

  // Run...
  msysmonData.start_time = time(NULL);
  collect_time           = msysmonData.start_time + DATA_INTERVAL;

  for (;;)
  {
    if (time(NULL) >= collect_time)
    {
      if (!msysmonCollectData())
        break;

      collect_time += DATA_INTERVAL;
    }

    if (poll(msysmonData.listeners, msysmonData.num_listeners, 1000) > 0)
    {
      // Accept new connections...
      for (i = 0; i < msysmonData.num_listeners; i ++)
      {
        if (msysmonData.listeners[i].revents & POLLIN)
        {
	  http_t *http = httpAcceptConnection(msysmonData.listeners[i].fd, /*blocking*/true);
					// New connection

          if (http)
          {
            cups_thread_t tid = cupsThreadCreate((cups_thread_func_t)msysmonRunWebIf, http);
            cupsThreadDetach(tid);
          }
        }
      }
    }
  }

  return (0);
}


//
// 'usage()' - Show program usage.
//

static int				// O - Exit status
usage(FILE *out)			// I - Output file
{
  fputs("Usage: msysmon [OPTIONS]\n", out);
  fputs("Options:\n", out);
  fputs("  --help             Show help.\n", out);
  fputs("  --version          Show version.\n", out);
  fputs("  -C CPU-LIMIT       Set CPU percentage for auto-monitoring.\n", out);
  fputs("  -M MEM-LIMIT       Set memory usage for auto-monitoring.\n", out);
  fputs("  -p PORT-NUMBER     Set web interface port.\n", out);
  fputs("  -w COMMAND         Add command to monitor.\n", out);

  return (out == stdout ? 0 : 1);
}
