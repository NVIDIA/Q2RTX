Files included
==============

pak1.pak - Updated data file.  Do not overwrite your pak0.pak with this file!
gamex86.dll - New game code for Windows machines
gamei386.so - New game code for Linux (Compiled on Redhat 5.1)

Installation
============

Unzip zaero-1.1.zip into your Zaero directory.  gamex86.dll should be overwritten.
As well, a new file called pak1.pak should be created.

If you're used to having Zaero items bound to the traditional weapon keys 
(keys 1 to 0), then see the section "zwepas" below about alternate
weapon selection changes.  You may need to run the zwepas_on macro.

Changes
=======

Please see Changelog.txt for a complete rundown of all the changes

zwepas
------

This variable, which was used to toggle "alternate weapon selections" on/off has been
removed.  In it's stead, we've added 10 new 'use' directives.  They being "use weapon x"
where x = 0 to 9.  In version Zaero 1.0, "use blaster" would alternate between the Blaster
and the Flaregun.  Now, "use weapon 1" executes this alternation and "use blaster" 
selects the blaster now.

So, to use alternate weapons, bind 1 to "use weapon 1", bind 2 to "use weapon 2", etc. all
the way up to 0.

To turn off alternate weapons, bind 1 to "use blaster", bind 2 to "use shotgun", etc. all
the way up to 0.

To make things easier, we've added macros "zwepas_on" and "zwepas_off" to automate
these processes.
