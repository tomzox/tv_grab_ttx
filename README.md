# Teletext EPG grabber

This EPG grabber or "scraper" allows extracting TV programme listings in
[XMLTV format](http://wiki.xmltv.org) from teletext pages as broadcast by
most European TV networks. The grabber works by reading teletext pages via
a TV card from all individual networks, then extracting programme title
and starting times from programme tables (e.g. in Germany usually pages
301-304) and finally adding description texts from pages which are
referenced in the overviews.

The grabber can be used stand-alone, but is best used as addition to the
Nextview EPG grabber [nxtvepg](http://nxtvepg.sourceforge.net) (version
2.8.0 or later), which can also be used for immediately browsing the
programme listings in the generated XMLTV files. The grabber is integrated
into the nxtvepg acquisition control and can be activated via the
configuration menus. (Currently nxtvepg still defaults to acquisition via
Nextview EPG, but that service is defunct now.)

Since the grabber has to read each individual network's teletext stream and
many networks use sub-pages at least for descriptions, the process
unfortunately takes relatively long. For full EPG grabbing, at least 90 seconds
per network are required. There's a fast scan mode which requires only 10-20
seconds per network in average, however in this mode overview and descriptions
may be incomplete if sub-pages are used by the respective network.

For now, only nation-wide German networks are supported. For other networks the
parser will need to be adapted to different page formats. That's because almost
every network formats tables, dates, times and descriptions slightly
differently, so that the parser will have trouble locating the EPG data among
all the advertisements and other content in teletext. 

# Installation

If you got the Debian package (or created one yourself using shell script
`mkdebian.pl`), you can install the executable and manual page by typing:

```console
    dpkg -i PACKAGE
```

Alternatively, you can compile and install the package from sources directly by
typing `make install prefix=/usr/local` (optionally replacing `/usr/local` with
another directory which contains bin/ and man/, where to install the executable
and manual page respectively.)

# Dependencies

This grabber captures EPG data from live TV broadcast, therefore a TV capture
device (e.g. a DVB capture device) is required.

When not used from within [nxtvepg](http://nxtvepg.sourceforge.net), the grabber
requires the [ZVBI library](http://zapping.sourceforge.net/ZVBI/index.html) for
capturing teletext from a TV capture device. The library is available for Linux
and BSD only. Most Linux distributions provide the library as a package (e.g.
for Debian, Ubuntu et.al. use `apt-get install libzvbi-dev`).

For building the executable, a C++ compiler supporting release C++11 or later
is required.

Note up to version 1.x of this package [Perl](https://www.perl.org/) and the
[Video::ZVBI](https://metacpan.org/pod/Video::ZVBI) Perl extension module were
used for implementation (actually that extension was developed for this
purpose). Due to increased size and complexity of the implementation however,
C++ is used instead since version 2.0, so these dependencies are moot.

# Usage

The following is a usage example: First the channel is switched to network ARD
using `mplayer` (you could use other tools such as `dvb5-zap` for this purpose,
depending on your DVB setup.) Then the grabber is run: With the shown options,
it will capture pages 300-399 from DVB stream 104 (which is the teletext PID of
channel ARD; you can try guessing it from the video PID in mplayer et.al.'s
`channels.conf` as it's value usually in range of video PID plus 3 to 30, or
via Internet search); capturing will stop after all sub-pages of the page range
have been acquired. Afterwards the grabber extracts EPG data and generates an
XMLTV file which is written to *stdout*, which is redirected to a file in the
example.  Finally the XMLTV file is loaded into nxtvepg for browsing the
programme schedules. (Any other XMLTV-capable browser will work, too.)

```console
  mplayer dvb://"Das Erste" &
  ./tv_grab_ttx -dvbpid 104 -page 301-399 > ttx-ARD.xml
  nxtvepg ttx-ARD.xml
```

For additional options invoke the grabber with command line option "`-help`".

Using the above will produce a separate XMLTV file per channel, which is not
practical for browsing. Use script `merge.pl` for merging XML files of all
channels into a single XMLTV file. See script `capall.pl` for an example how to
automate capturing and merging EPG data from multiple channels. When using the
grabber from within nxtvepg, the merge can be configured to be done automatically.

# Copyright

**Copyright 2006-2011, 2020 by T. Zoerner**

This program is free software; you can redistribute it and/or modify
it under the terms of the
[GNU General Public License](http://www.fsf.org/copyleft/gpl.html)
as published by the [Free Software Foundation](http://www.fsf.org/).
This program is distributed in the hope that it will be useful,
but **without any warranty**; without even the implied warranty of
merchantability or fitness for a particular purpose.

**Content copyright**: Please note that content providers do hold a copyright
on the programme schedules which can be extracted by means of this software.
The data is free for personal use, but must not by publicly redistributed (e.g.
made available on the Internet) without prior permission.
