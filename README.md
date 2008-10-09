# Teletext EPG grabber

This EPG grabber allows extracting TV programme listings in
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

Alternatively, you can also trivially install the program manually simply by
copying `tv_grab_ttx.pl` to `/usr/bin` (note normally the ".pl" appendix is
stripped when installing.) The manual page can be extracted from the Perl
script and copied to `/usr/share/man/man1`:

```console
    pod2man -section 1 tv_grab_ttx.pl > /usr/share/man/man1/tv_grab_ttx.1
```

# Dependencies

The grabber is implemented as Perl scripts. Hence you'll need to have a
[Perl interpreter](http://cpan.org). On UNIX and Linux systems this
is usually part of the core installation. For Windows you can get
[ActivePerl](http://www.ActiveState.com/ActivePerl).

Additionally, when not used inside of nxtvepg, you'll need the
[Video::ZVBI](https://metacpan.org/pod/Video::ZVBI) Perl extension module for
capturing teletext. Currently this module only supports Linux, and the BSDs.
Note this module in turn requires the
[ZVBI library](http://zapping.sourceforge.net/ZVBI/index.html)
(a VBI capturing library).

# Usage

The following is a usage example: First the channel is switched to network ARD
using xawtv's tool v4lctl. The following command captures teletext for 1.5
minutes and then extracts EPG data from the captured pages into an XMLTV file.
Finally the XMLTV file is loaded into nxtvepg for browsing the programme
schedules. (Any other XMLTV-capable browser will work, too.)

```console
  v4lctl -c /dev/vbi0 setstation ARD
  ./cap.pl -duration 90 | ttxacq.pl -page 301-309 > ARD.xml
  nxtvepg ARD.xml
```

Note capturing from DVB devices requires option "`-dvbpid`".
For additional options invoke the scripts with command line option
"`-help`".

Using the above will produce a separate XMLTV file per channel, which is not
practical for browsing. Use script `merge.pl` for merging XML files of all
channels into a single XMLTV file. See script `capall.pl` for an example how to
automate capturing and merging EPG data from multiple channels.

# Copyright

**Copyright 2006 - 2008 by T. Zoerner**

This program is free software; you can redistribute it and/or modify
it under the terms of the
[GNU General Public License](http://www.fsf.org/copyleft/gpl.html)
as published by the [Free Software Foundation](http://www.fsf.org/).
This program is distributed in the hope that it will be useful,
but **without any warranty**; without even the implied warranty of
merchantability or fitness for a particular purpose.

**Content copyright**: Please note that content providers do hold a
copyright on the programme data which can be received by means of this
software.  The data is free for personal use, but you must not publically
redistribute it (e.g. make it available on the Internet) without prior
permission.
