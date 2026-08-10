Mike's System Monitor (msysmon)
===============================

![Version](https://img.shields.io/github/v/release/michaelrsweet/msysmon?include_prereleases)
![Apache 2.0](https://img.shields.io/github/license/michaelrsweet/msysmon)

`msysmon` is a lightweight administrator/developer tool for monitoring the CPU,
memory, and threads used by processes on a Linux or macOS system.  I wrote it
primarily to do long-term testing of embedded Linux systems where memory leaks,
crashes, and/or CPU spins can cause problems but are often hard to track down.

`msysmon` runs in the background to collect usage information and provides a
simple web interface for viewing it complete with graphs.

> *Note:* This is a developer tool and is not provided or distributed for
> production purposes.  It comes with absolutely no warranty.


Requirements
------------

- macOS or Linux system
- C99 compiler (clang and GCC work fine)
- libcups (3.0 or later)


Building and Installing
-----------------------

`msysmon` uses an autoconf-based configure script to produce a makefile that
builds and installs the software:

    ./configure
    make
    make install


Running
-------

Just run the `msysmon` program:

    msysmon

The `-I` (interval) option sets the sampling interval - the default is every 5
minutes.  For example, use the following command to sample processes every 10
minutes:

    msysmon -I 10m

By default `msysmon` will automatically watch processes that use excessive CPU
or memory, but you can force watching of specific (named) processes with the
`-w` (watch) option, for example:

    msysmon -w myprogram

Other options can be shown with the man page or `--help` option:

    msysmon --help


Legal Stuff
-----------

Mike's System Monitor is Copyright © 2026 by Michael R Sweet.

This software is licensed under the Apache License Version 2.0.  See the files
"LICENSE" and "NOTICE" for more information.
