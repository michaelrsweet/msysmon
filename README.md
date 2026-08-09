Mike's System Monitor (msysmon)
===============================

Mike's System Monitor (msysmon) is an administrator/developer tool for
monitoring the CPU, memory, network, and storage usage on Linux systems via the
`/proc` filesystem.

> *Note:* There is also some limited, experimental support for macOS - most
> stuff works except per-process CPU usage.


Goals
-----

1. Monitor embedded Linux systems over a long period of time, i.e., 1 month or
   more, to see trends in memory, CPU, network, or storage use that could impact
   reliability/availability.

2. Correlate usage to individual processes and identify problematic programs
   automatically.

3. Make easy-to-read reports on current and historical usage, with "top 10" and
   explicitly-identified processes listed.

4. Have low overhead to run even on production systems.

5. Be able to save overall and per-process data for further analysis.


Building and Installing
-----------------------

Use the usual autoconf stuff:

    ./configure
    make
    make install


Running
-------

Just run the "msysmon" program:

    msysmon

The `-I` option sets the sampling interval (default is every 5 minutes), for
example to sample every 10 minutes:

    msysmon -I 10m

By default msysmon will automatically watch processes that use excessive CPU or
memory, but you can force watching of specific (named) processes with the `-w`
option, for example:

    msysmon -w myprogram

Other options can be shown with the man page or `--help` option:

    msysmon --help


Legal Stuff
-----------

Mike's System Monitor is Copyright © 2026 by Michael R Sweet.

This software is licensed under the Apache License Version 2.0.  See the files
"LICENSE" and "NOTICE" for more information.
