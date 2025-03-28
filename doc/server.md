Q2RTX Server Manual
===================
Andrey Nazarov <skuller@skuller.net>
<br>and<br>
Copyright (c) 2019, NVIDIA Corporation. All right reserved.

About
-----
Q2RTX is built upon Q2VKPT and Q2PRO source ports of Quake 2 and inherits
most of their settings and commands, listed in this manual. It also adds
many settings and commands of its own, also listed here. Many of them
are primarily intended for renderer development and debugging.

Q2PRO is an enhanced, multiplayer oriented Quake 2 server, compatible
with existing Quake 2 ports and licensed under GPLv2. This document provides
descriptions of console variables and commands added to or modified by Q2PRO
since the original Quake 2 release. Cvars and commands inherited from original
Quake 2 are not described here (yet).

Variables
---------

### Network

#### `net_enable_ipv6`
Enables IPv6 support. Default value is 1 on systems that support IPv6 and 0
otherwise.

  - 0 — disable IPv6, use IPv4 only
  - 1 — enable IPv6, but do not listen for incoming IPv6 connections and
  prefer IPv4 over IPv6 when resolving host names with multiple addresses
  - 2 — enable IPv6, listen for incoming IPv6 connections and use normal
  address resolver priority configured by OS

#### `net_ip`
Specifies network interface address server should listen on for UDP and TCP
connections using IPv4. The same interface is also used for outgoing TCP
connections (these include MVD/GTV and anticheat connections).  Default
value is empty, which means listening on all interfaces.

*NOTE*: There is a limitation preventing anticheat to work correctly on servers
accessible from multiple IP addresses. If you are running the server on multi-ip
system and plan to use anticheat, you need to explicitly bind the server to
one of your network interfaces using the `net_ip` cvar, otherwise expect any
kinds of problems.

#### `net_ip6`
Specifies network interface address server should listen on for UDP and TCP
connections using IPv6. The same interface is also used for outgoing TCP
connections (these include MVD/GTV connections).  Default value is empty,
which means listening on all interfaces. Has no effect unless
`net_enable_ipv6` is set to non-zero value.

#### `net_port`
Specifies port number server should listen on for UDP and TCP connections
(using IPv4 or IPv6).  Default value is 27910.

#### `net_ignore_icmp`
On Win32 and Linux, server is able to receive ICMP
‘destination-unreachable’ packets from clients. This enables intelligent
detection of crashed clients, allowing the server to quickly re-use their
slots. If this behavior is not wanted for some reason, then this variable
can be used to turn it off. Default value is 0 (don't ignore ICMP packets).

#### `net_maxmsglen`
Specifies maximum server to client packet size clients may request from
server. 0 means no hard limit. Default value is conservative 1390 bytes. It
is nice to have this variable as close to your network link MTU as possible
(accounting for headers). Thus for normal Ethernet MTU of 1500 bytes 1462
can be specified (10 bytes quake header, 8 bytes UDP header, 20 bytes IPv4
header). Higher values may cause IP fragmentation for misconfigured clients
which is better to avoid. Please don't change this variable unless you know
exactly what you are doing.

### Generic

#### `sv_iplimit`
Maximum number of simultaneous connections allowed from single IP address
(per connection type, TCP and UDP client lists are separate).  Setting this
variable to 0 disables the limit. Default value is 3.

#### `sv_status_show`
Specifies how the server should respond to status queries. Default value is 2.

   - 0 — do not respond at all
   - 1 — respond with server info only
   - 2 — respond with server info and player list

#### `sv_status_limit`
Limits the rate at which server responds to status queries. Default value
is 15 queries per second.

#### Rate limits specification
Rate limiting is implemented as a simple token bucket filter. Full syntax for
specifying rate limits is: `<limit>[/<period>[sec|min|hour]][*<burst>]`. Only
the `limit` argument is mandatory. Zero `limit` means rate is not limited.

Default period is one second. Custom _period_ can be specified after a slash,
with optional `sec`, `min`, or `hour` suffix (default units are seconds).

Default burst value is 5. Custom `burst` can be specified after an asterisk.
Burst specifies initial number of extra packets that are permitted even if
they arrive at rate higher than allowed.

#### `sv_auth_limit`
Limits the rate of client connection attempts with invalid password.
Default value is 1 invalid authentication attempt per second.

#### `sv_rcon_limit`
Limits the rate at which server responds to invalid rcon commands. Default
value is 1 invalid command per second.

#### `sv_namechange_limit`
Limits the rate at which clients are permitted to change their name.
Default value is 5 name changes per minute.

#### `sv_password`
If not empty, allows only authenticated clients to connect.  Authenticated
clients are allowed to occupy reserved slots, see below.  Clients set their
passwords via `password` userinfo variable. Default value is empty (no
password).

*TIP*: If password protection is needed for a server, it is preferable to use
`sv_password` instead of going the game mod way and using `password` variable.
The latter will prevent MVD/GTV features from working.

*NOTE*: If `sv_password` is set, then game mod's `password` variable must be empty.
Otherwise clients will be unable to connect.

#### `sv_cinematics`
If set to 0, server will skip cinematics even if they exist. Default value
is 1.

#### `sv_max_packet_entities`
Maximum number of entities in client frame. 0 means unlimited. Default
value is 128. Some non-standard maps with large open areas may need this
value increased. Consider however that default Quake 2 client can only
render 128 entities maximum. Other clients may support more.

#### `sv_reserved_slots`
Number of client slots reserved for clients who know `sv_reserved_password`
or `sv_password`. Must be less than `maxclients` value. Default value is 0
(don't reserve slots).

*NOTE*: If `sv_mvd_enable` is non-zero and `sv_reserved_slots` is zero,
`sv_reserved_slots` is automatically set to one to reserve a slot for dummy MVD
observer.

*NOTE*: Value of `sv_reserved_slots` cvar is subtracted from `maxclients` value
visible in the server info.

#### `sv_reserved_password`
The password to use for reserved slots. Default value is empty, which means
no one is allowed to occupy reserved slot(s), except of dummy MVD observer.
Clients set their passwords via `password` userinfo variable.

#### `sv_locked`
Locks the server, preventing new clients from connecting. Default value is
0 (server unlocked).

#### `sv_lan_force_rate`
When enabled, do not enforce any rate limits on clients whose IP is from
private address space (`127.x.x.x`, `10.x.x.x`, `192.168.x.x`, `172.16.x.x`).
Default value is 0 (disabled).

#### `sv_min_rate``
Server clamps minimum value of `rate` userinfo parameter to this value.
Default value is 1500 bytes/sec. This parameter can't be greater than
`sv_max_rate` value or less than 1500 bytes/sec.

#### `sv_max_rate``
Server clamps maximum value of `rate` userinfo parameter to this value.
Default value is 15000 bytes/sec.

#### `sv_calcpings_method`
Specifies the way client pings are calculated. Default ping calculation
algorithm is very client frame and packet rate dependent, and may give
vastly inaccurate results, depending on client settings. Using improved
algorithm is in fact recommended, it should always give stable results
similar to ones obtained by the `ping` command line utility. Default value
is 2.

- 0 — disable ping calculation entirely
- 1 — use default ping calculation algorithm based on averaging
- 2 — use improved algorithm based on minimum round trip times

#### `sv_ghostime`
Maximum time, in seconds, before dropping clients which have passed initial
challenge-response connection stage but have not yet sent any data over
newly established connection.  This also applies to MVD/GTV clients in
request processing stage.  Helps to avoid attacks flooding server with
zombie clients, combined with `sv_iplimit` variable. Default value is 6.

#### `sv_idlekick`
Time, in seconds, before dropping inactive clients. Default value is 0
(don't kick idling clients). Moving, pressing buttons while in game and
issuing chat commands counts as activity.

*NOTE*: Don't set `sv_idlekick` too low to avoid kicking clients that are
downloading or otherwise taking long time to enter the game.

#### `sv_force_reconnect`
When set to an address string, forces new clients to quickly reconnect to
this address as an additional proxy protection measure. Default value is
empty (do not enforce reconnection).

#### `sv_redirect_address`
When server becomes full, redirects new clients to the specified address.
Default value is empty (don't redirect).

#### `sv_downloadserver`
Specifies the URL clients should use for HTTP downloading. URL must begin
with a `http://` prefix and end with a trailing slash. Default value is
empty (no download URL).

TIP: It is highly advisable to setup HTTP downloading on any public Quake 2
server. The easiest way to do so is to run
[pakserve](https://github.com/skullernet/pakserve) on the same machine as Quake 2
server.

#### `sv_show_name_changes`
Broadcast player name changes to everyone. You should probably enable this
unless game mod already shows name changes. Default value is 0 (don't
show).

#### `sv_allow_nodelta`
Enables automatic removal of clients that abuse the server with requests
for too many `nodelta` (uncompressed) frames, hogging network resources.
Default value is 1 (don't remove clients), since this may sometimes
legitimately happen on very poor client connections.

#### `sv_allow_unconnected_cmds`
Controls whether client command strings are processed by the game mod even
when the client is not fully spawned in game. Originally, Quake 2 server
forwarded all commands to the game mod even for connecting clients, but
this is known to cause problems with some (broken) mods that don't perform
their own client state checks. Default value is 0 (ignore commands unless
fully connected).

#### `sv_uptime`
Include `uptime` key/value pair in server info. Default value is 0.

- 0 — do not display uptime at all
- 1 — display uptime in compact format
- 2 — display uptime in verbose format

#### `sv_enhanced_setplayer`
Enable partial client name matching for certain console commands like
`kick` and `stuff`. Default value is 0 (use original matching algorithm).

#### `sv_recycle`
Temporary variable useful for automatically upgrading the server at the
next map change.  Default value is 0.

- 0 — handle `gamemap` command normally
- 1 — turn the next `gamemap` into `map` and reload the game module
- 2 — turn the next `gamemap` into `recycle` and reload entire server

**WARNING**: Be sure to read `recycle` command description below before enabling `sv_recycle`.

#### `sv_allow_map`
Controls the `map` command behavior. `map` is often mistakingly used by
server operators to change maps instead of the more lightweight `gamemap`.
Thus, this variable exists to prevent misuse of `map`. Default value is 0.

- 0 — disallow `map` and print a warning unless there are pending latched cvars
- 1 — handle `map` command normally
- 2 — turn `map` into `gamemap` unless there are pending latched cvars

#### `sv_changemapcmd`
Specifies command to be executed each time server finishes loading a new map.
Default value is empty.

#### `sv_fps`
Specifies native server frame rate.  Only used when game mod advertises
support for variable server FPS. Specified rate should be a multiple of 10
(maximum rate is 60). Default value is 10 frames per second. Only clients
that support Q2PRO protocol will be able to take advantage of higher FPS.
Other clients will receive updates at default rate of 10 packets per
second.

### Downloads

These variables control legacy server UDP downloads.

#### `allow_download`
Globally allows or disallows server UDP downloads. Remaining variables listed
below are effective only when downloads are globally enabled. Default value
is 0.

- 0 — downloads are disabled
- 1 — downloads are enabled

#### `allow_download_maps`
Enables downloading of files from ‘maps/’ subdirectory. Default value is 1.

- 0 — map downloads are disabled
- 1 — map downloads are enabled for physical files and disabled for files from packs
- 2 — map downloads are enabled for all files

#### `allow_download_models`
Enables downloading of files from `models/` and `sprites/` subdirectories.
Default value is 1.

#### `allow_download_sounds`
Enables downloading of files from `sound/` subdirectory. Default value is 1.

#### `allow_download_pics`
Enables downloading of files from `pics/` subdirectory. Default value is 1.

#### `allow_download_players`
Enables downloading of files from `players/` subdirectory. Default value is 1.

#### `allow_download_textures`
Enables downloading of files from `textures/` and `env/` subdirectories.
Default value is 1.

#### `allow_download_others`
Enables downloading of files from any subdirectory other than those listed
above. Default value is 0.

#### `sv_max_download_size`
Maximum size of UDP download in bytes. Value of 0 disables the limit.
Default value is 8388608 (8 MiB).


### MVD/GTV server

MVD stands for "Multi View Demo"

GTV stands for "Game TeleVision"

#### Server modes

Q2PRO server can run in either MVD/GTV `server` or `client` mode. `Server` mode
is just a regular game server mode. In this mode, server functions as a primary
source of MVD data, which can be either locally recorded to disk in form of a
demo file, or sent to GTV clients over the network.  `Client` mode is more
special. In this mode, game mod is not run and server acts as a GTV `relay` node,
reading MVD data from local demo files, receiving live data from remote GTV
servers, or both at the same time. MVD data is then served to regular Quake 2
clients (spectators).

As a convention, cvars related to server mode have `sv_mvd_` prefix. Cvars related
to client mode have `mvd_` prefix (see the next section).

#### `sv_mvd_enable`
Enables MVD/GTV server functionality. Default value is 0.

- 0 — MVD server is disabled
- 1 — local MVD recording is allowed
- 2 — local MVD recording and remote GTV connections are allowed

#### `sv_mvd_maxclients`

Total number of MVD/GTV client slots on the server. Default value is 8.

#### `sv_mvd_password`

If not empty, allows only authenticated MVD/GTV clients to connect.
Default value is empty (any neutral host can connect).

*NOTE*: Password check only applies to MVD/GTV clients that are neither
whitelisted nor blacklisted (see `addgtvhost` and `addgtvban` commands
description for more information).

#### `sv_mvd_nogun`
Reduce bandwidth usage by filtering on-screen gun updates out of MVD
stream.  Default value is 0 (filtering disabled).

#### `sv_mvd_noblend`
Reduce bandwidth usage by filtering on-screen blend effects out of MVD
stream.  Default value is 0 (filtering disabled).

#### `sv_mvd_nomsgs`
When enabled, MVD/GTV spectators in chasecam mode will not receive any text
messages routed to their chase targets (which are normally on a team), but
will receive messages routed to the dummy MVD observer. Default value is 1
(enabled). This variable is only effective when dummy client is spawned,
see `sv_mvd_spawn_dummy` variable description for more information.

#### `sv_mvd_maxtime`
Maximum duration, in minutes, of the locally recorded MVD.  Default value
is 0 (unlimited).

#### `sv_mvd_maxsize`
Maximum size, in kB, of the locally recorded MVD. Default value is 0
(unlimited).

#### `sv_mvd_maxmaps`
Specifies number of map changes local MVD recording is stopped after.
Default value is 1. Setting this to 0 disables the limit.

#### `sv_mvd_begincmd`
This command is issued on behalf of dummy MVD observer as soon as it enters
the game. Do whatever preparations are needed here to make sure MVD
observer enters an appropriate observing mode, opens the scoreboard, etc.
MVD observer has it's own command buffer and each `wait` cycle lasts 100 ms
there. Default value is `wait 50; putaway; wait 10; help;`.

#### `sv_mvd_scorecmd`
This command is issued on behalf of dummy MVD observer each time no layout
updates are detected for more than 9 seconds. Useful for reopening the
scoreboard if the game mod closes it for some reason.  MVD observer has
its own command buffer and each `wait` cycle lasts 100 ms there. Default
value is `putaway; wait 10; help;`.

#### `sv_mvd_suspend_time`
GTV connections are suspended after this period of time, in minutes,
counted from the moment last active player disconnects or becomes inactive.
Setting this to zero disables server side suspending entirely. Default
value is 5.

#### `sv_mvd_disconnect_time`
Dummy MVD observer is disconnected after this period of time, in minutes,
counted from the moment last GTV client disconnects or becomes inactive.
Setting this to zero makes dummy client persistent. Default value is 15.

#### `sv_mvd_spawn_dummy`
Specifies if dummy MVD observer needs to be spawned. Default value is 1.

- 0 — never spawn dummy client
- 1 — only spawn if game mod advertises support for MVD
- 2 — always spawn dummy client


### MVD/GTV client

#### `mvd_username`
Default username to use for outgoing GTV connections. Default value is
`unnamed`.

#### `mvd_password`
Default password to use for outgoing GTV connections. Default value is
empty.

#### `mvd_timeout`
Specifies MVD connection timeout value, in seconds. Default value is 90.

#### `mvd_suspend_time`
GTV connections are suspended after this period of time, in minutes,
counted from the last moment of MVD spectator(s) activity. Setting this to
zero disables client side suspending entirely. Default value is 5.

#### `mvd_wait_delay`
Time, in seconds, for MVD channel to buffer data initially.  This
effectively specifies MVD stream delay seen by observers. Default value is
20.

#### `mvd_buffer_size`
Size of delay buffer, in multiplies of MAX_MSGLEN (32 KiB). Default value
is 8. You may need to increase this when also increasing ‘mvd_wait_delay’.

#### `mvd_wait_percent`
Maximum inuse percentage of the delay buffer when MVD channel stops
buffering data to prevent overrun, ignoring `mvd_wait_delay` value.
Default value is 50.

#### `mvd_default_map`
Specifies default map used for the Waiting Room channel. Default value is
`q2dm1`.

#### `mvd_chase_prefix`
Specifies POV info string position on the screen. This should be a valid
fragment of the Quake 2 layout script. Default value is "xv 0 yb -64".

#### `mvd_stats_score`
Specifies what `score` stats field should contain for MVD observers.
This field is externally visible to server browsers. Default value is 0.

- 0 — always zero
- 1 — MVD channel ID spectator is on
- 2 — score of the chase target

#### `mvd_snaps`
Specifies time interval, in seconds, between saving `snapshots` in memory
during MVD playback.  Snapshots enable backward seeking in demo (see `mvdseek`
command description), and speed up repeated forward seeks. Setting this
variable to 0 disables snapshotting entirely. Default value is 10.

### Hacks

#### `sv_strafejump_hack`
Enables FPS-independent strafe jumping mode for clients using R1Q2 and
Q2PRO protocols. Values higher than 1 will force this mode for all clients,
regardless of their protocol version. Default value is 1 (enable strafe
jumping hack only for compatible clients).

#### `sv_waterjump_hack`
Makes underwater movement speed equal in all directions for clients using
Q2PRO protocol. Values higher than 1 will force this mode for all clients,
regardless of their protocol version.  Default value is 1 (enabled).

#### Water jump bug
Quake 2 player movement code contains a bug that causes surfacing velocity
produced by holding the jump button underwater to be severely limited,
comparing to movement in other directions. Even worse, resulting velocity is
calculated differently on client and server sides and that causes prediction
errors and jerky movement.  Q2PRO is able to work around this bug and make
player movement speed equal in all directions underwater. However, this fix is
disabled by default as it is yet unknown if this can be considered an unfair
advantage over non-Q2PRO clients.

### System

#### `sys_console`
On UNIX-like systems, specifies how system console is initialized. Default
value is 2 if both stdin and stdout descriptors refer to a TTY, 1 if
running a dedicated server and 0 otherwise.
- 0 — don't write anything to stdout and don't read anything from stdin
- 1 — print to stdout and read commands from stdin, but don't assume it is a terminal
- 2 — enable command line editing and colored text output

##### System console key bindings
The following key bindings are available in Windows console and in TTY console
when command line editing is enabled:

* HOME, Ctrl+A — move cursor to start of line
* END, Ctrl+E — move cursor to end of line
* Left arrow, Ctrl+B — move cursor one character left
* Right arrow, Ctrl+F — move cursor one character right
* Alt+B — move cursor one word left
* Alt+F — move cursor one word right
* DEL, Ctrl+D — delete character under cursor
* Backspace, Ctrl+H — delete character left of cursor
* Ctrl+W — delete word left of cursor
* Ctrl+U — delete all characters left of cursor
* Ctrl+K — delete all characters right of cursor
* Ctrl+L — erase screen
* Ctrl+C — quit
* Down arrow, Ctrl+N — next line in command history
* Up arrow, Ctrl+P — previous line in command history
* Ctrl+R — reverse search in command history
* Ctrl+S — forward search in command history
* Tab — complete command

In Windows console additional key bindings are supported:

* PGUP — scroll console buffer up
* PGDN — scroll console buffer down
* Ctrl+PGUP — scroll to console top
* Ctrl+PGDN — scroll to console bottom

#### `sys_parachute`
On UNIX-like systems, specifies if a fatal termination handler is
installed. Default value is 1, which means Q2PRO will do some cleanup when
it crashes, like restoring terminal settings.  However, this will prevent
core dump from being generated. To enable core dumps, set this variable to
0.

#### `sys_forcegamelib`
Specifies the full path to the game library server should attempt to load
first, before normal search paths are tried. Useful mainly for debugging or
mod development.  Default value is empty (use normal search paths).


### Console Logging

#### `logfile`
Specifies if console logging to file is enabled. Default value is 0.

- 0 — logging disabled
- 1 — logging enabled, overwrite previous file
- 2 — logging enabled, append to previous file

*NOTE*: Log file is not automatically reopened when game directory is changed.

#### `logfile_flush`
Specifies if log file data is buffered in memory or flushed to disk
immediately. Default value is 0. See `setvbuf(3)` manual page for more
details.

- 0 — system default mode (block buffered)
- 1 — line buffered mode
- 2 — unbuffered mode

#### `logfile_name`
Specifies base name of the log file. Should not include any extension part
or path components. `logs/` prefix and `.log` suffix are automatically
appended.  Default value is `console`.

#### `logfile_prefix`
Specifies the time/date template each line of log file is prefixed with.
Default value is `[%Y-%m-%d %H:%M] `. See `strftime(3)` manual page for
syntax description. In addition, the first `@` character in the template,
if found, is replaced with a single character representing message type
(T — talk, D — developer, W — warning, E — error, N — notice, A — default).

#### `console_prefix`
Analogous to `logfile_prefix`, but for system console. Additionally,
sequence `<?>`, if present at the beginning of prefix, is replaced with
printk()-style severity level based on message type. This is intended for
logging server stdout with systemd(1). Default value is empty (no prefix).

### Miscellaneous

#### `map_override_path`
Specifies the directory from which override files with extensions `.ent` or
`.bsp.override` are loaded. Default value is empty (don't try to override
entity strings). Typical value for this is `maps`, but can be customized
per server port.

#### Entity overrides
Override files with `.ent` extension allow the entity string of the map being
loaded to be replaced by a custom data supplied by server operator. This makes
it possible to change the layout of entities on the map (thus creating a new
version of the map) without requiring clients to download anything. Entity
string can be dumped from the current map using `dumpents` server command and
later changed with a text editor.

Override files with `.bsp.override` extension are more complex: they are binary
files that can replace map entity string or checksum. They can also create an
alias for the map. How to create such files is out of scope of this manual
(search the internet for ‘r1q2 map override file generator’).

#### `map_visibility_patch`
Attempt to patch miscalculated visibility data for some well-known maps
(`q2dm1`, `q2dm3` and `q2dm8` are patched so far), fixing disappearing walls and
entities. Default value is 1 (enabled).

*NOTE*: Q2RTX makes further adjustments to the visibility data in order to
make water properly transparent. The adjustments happen in the RTX renderer,
and the patched PVS data is saved into `maps/pvs/<mapname>.bin` files so that
the dedicated server could use it too.

#### `com_fatal_error`
Turns all non-fatal errors into fatal errors that cause server process exit.
Default value is 0 (disabled).

#### `com_debug_break`
Development variable that turns all errors into debug breakpoints. Default
value is 0 (disabled).

#### `rcon_password`
Password for the remote console (rcon). When set to an empty string, rcon 
is disabled. Default value is empty string.

#### `sv_novis`
Disables visibility culling of entities that are transmitted to clients, 
which effectively means that clients see the entire map and everything in it.
Q2RTX sets `sv_novis` to 1 when there are security cameras in the map.
Default value is 0.

#### `sv_restrict_rtx`
When set to 1, the server will reject any client that does not have "q2rtx"
in their userinfo version parameter. Default value is 1.

#### `sv_savedir`
Path within the `gamedir` where save game files should be stored. When hosting
a dedicated server with cooperative mode games, `sv_savedir` should be set 
to different paths on different instances of the server. Default value is `save`,
which maps to `baseq2/save` when playing the base game.

#### `sv_flaregun`
Switch for flare gun, which is a custom weapon added in Q2RTX. Default value is 2.

- 0 — no flare gun
- 1 — spawn with the flare gun
- 2 — spawn with the flare gun and some grenades for it

Commands
--------

### Generic

#### `status [mode]`
Show information about connected clients. Optional _mode_ argument may be
provided to show different kind of information.

* `t(ime)`: show connection times
* `d(ownload)`: show current downloads
* `l(ag)`: show connection quality statistics
* `p(rotocol)`: show network protocol information
* `v(ersion)`: show client executable versions

#### `stuff <userid> <text ...>`
Stuff the given raw _text_ into command buffer of the client identified by
_userid_.

#### `stuffall <text ...>`
Stuff the given raw _text_ into command buffers of all connected clients.

#### `stuffcvar <userid> <variable> [...]`
Stuff a command to query value of console _variable_ into command buffer of
the client identified by _userid_. Result of the query is printed in server
console once a reply is received. More than one variable can be specified on
command line.

#### `dumpents [filename]`
Dumps the entity string of current map into `entdumps/_filename_.ent` file.
Original map entity string is dumped, even if override is in effect.
See also `map_override_path`s variable description.

#### `pickclient <address:port>`
Send `passive_connect` packet to the client at specified _address_ and
_port_.  This is useful if the server is behind NAT or firewall and can not
accept remote connections. Remote client must support passive connections
(R1Q2 and Q2PRO clients do), must be in passive connection mode and the
specified _port_ must be reachable. See `passive` [[client]] command for
more details.

#### `addban <address[/mask]> [comment ...]`
Adds specified _address_ to the ban list. Specify _mask_ to ban entire
subnetwork.  If specified, _comment_ will be printed to banned user(s) when
they attempt to connect.

#### `delban <address[/mask]|id|all>`
Deletes exactly matching _address_/_mask_ pair from the ban list. You can
also specify numeric _id_ of the address/mask pair to delete, or use
special keyword _all_ to clear the entire list.

#### `listbans`
Displays all address/mask pairs added to the ban list along with their IDs,
last access times and comments.

#### `kickban <userid>`
Kick the client identified by _userid_ and add his IP address to the ban
list (with a default mask of 32).

#### `addblackhole <address[/mask]> [comment ...]`
Adds specified _address_ to the blackhole list. Specify _mask_ to blackhole
entire subnetwork.  All connectionless packets from blackholed hosts will
be silently ignored.

#### `delblackhole <address[/mask]|id|all>`
Deletes exactly matching _address_/_mask_ pair from the blackhole list. You
can also specify numeric _id_ of the address/mask pair to delete, or use
special keyword _all_ to clear the entire list.

#### `listblackholes`
Displays all address/mask pairs added to the blackhole list along with
their IDs, last access times and comments.

#### `addstuffcmd <connect|begin> <command> [...]`
Adds _command_ to be automatically stuffed to every client as they initially
_connect_ or each time they _begin_ on a new map.

#### `delstuffcmd <connect|begin> <id|all>`
Deletes command identified by the numeric _id_ from the specified list.
You can also specify _all_ to clear the whole stuffcmd list.

#### `liststuffcmds <connect|begin>`
Enumerates all registered commands in the specified stuffcmd list.

#### `addfiltercmd <command> [ignore|print|stuff|kick] [comment ...]`
Prevents client _command_ otherwise unknown to the server from being
interpreted by the game mod, and takes the specified action instead.
For _print_ and _stuff_ actions _comment_ argument is mandatory, and should
contain data to print and stuff, respectively. Default action is _ignore_.
Commands are matched in a case-insensitive way.

#### `delfiltercmd <id|name|all>`
Deletes command identified by numeric _id_ or by _name_ from the list of
filtered commands. You can also specify _all_ to clear the whole filtercmd
list.

#### `listfiltercmds`
Enumerates all filtered commands along with appropriate actions and comments.

#### `listmasters`
List master server hostnames, resolved IP addresses and last acknowledge times.

#### `quit [reason ...]`
Exit the server, sending `disconnect` message to clients. Optional _reason_
string may be provided instead of the default ‘Server quit’ message.

#### `recycle [reason ...]`
This command is equivalent to `quit`, with an exception that `reconnect`
message is sent to clients instead of `disconnect`. Useful for quickly
upgrading the server binary without losing clients, assuming the server
process will be automatically restarted by an external shell script right
after it exits.


### MVD/GTV server

#### `mvdrecord [-hz] <filename>`
Start local MVD recording into `demos/_filename_.mvd2`.
* `-h` or `--help`: display help message
* `-z` or `--compress`: compress file with gzip

#### `mvdstop`
Stop local MVD recording.

#### `mvdstuff <text>`
Execute the given _text_ on behalf of dummy MVD observer.

#### `addgtvhost <address[/mask]>`
Adds specified _address_ to the white list of trusted MVD/GTV hosts allowed
to connect to this server without password. Specify _mask_ to allow entire
subnetwork.

#### `delgtvhost <address[/mask]|id|all>`
Deletes exactly matching _address_/_mask_ pair from the white list of
trusted MVD/GTV hosts. You can also specify numeric _id_ of the
address/mask pair to delete, or use special keyword _all_ to clear the
entire list.

#### `listgtvhosts`
Displays all address/mask pairs added to the white list of trusted MVD/GTV
hosts along with their IDs.

*NOTE*: White list of MVD/GTV hosts takes precedence over black list. Whitelisted
hosts are not required to know `sv_mvd_password` even if it is set.

#### `addgtvban <address[/mask]>`
Adds specified _address_ to the black list of banned MVD/GTV hosts
disallowed to connect to this server. Specify _mask_ to ban entire
subnetwork.

#### `delgtvban <address[/mask]|id|all>`
Deletes exactly matching _address_/_mask_ pair from the black list of
banned MVD/GTV hosts. You can also specify numeric _id_ of the address/mask
pair to delete, or use special keyword _all_ to clear the entire list.

#### `listgtvbans`
Displays all address/mask pairs added to the black list of banned MVD/GTV
hosts along with their IDs.


### MVD/GTV client

#### Channels and connections
MVD/GTV client command interface operates with `channel` and `connection`
objects. Typically, there is one-to-one correspondence between them, but this
is not always the case.  Connection is a persistent handle to the remote GTV
server, while MVD channel, which represents a world state seen by the
observers, has a shorter lifetime. Channels are automatically created
once there is some data available on the connection and destroyed once
connection is suspended by server and all buffered data have been read. A demo
playback channel is special in a way that is does not have a parent connection.

Both _connections_ and _channels_ can be identified by their name or by unique
ID number seen in the output of `mvdservers` and `mvdchannels` commands.
Channels inherit names and IDs from their parent connections.

#### `mvdconnect [-hn:u:p:] <address[:port]>`
Create connection to the GTV server at the given _address_. If _port_ is
omitted, default server port 27910 is used.

* `-h` or `--help`: display help message
* `-n` or `--name=<string>`: specify channel name as _string_, default is `netX`
* `-u` or `--user=<string>`: specify username as _string_, default is to use value of `mvd_username` cvar
* `-p` or `--pass=<string>`: specify password as _string_, default is to use value of `mvd_password` cvar

#### `mvdisconnect [-ah] [connection]`
Destroy the specified GTV server _connection_ or all connections. If there
is an associated MVD channel, any buffered data is replayed to spectators,
then MVD channel is destroyed. There is no need to specify _connection_ if
there is only one active connection.
* `-a` or `--all`: destroy all connections
* `-h` or `--help`: display help message


#### `mvdkill [channel]`
Destroy the specified MVD _channel_ (any parent GTV connection is also
destroyed).  There is no need to specify _channel_ if there is only one
active channel.

#### `mvdplay [-hl:n:r:] <[/]filename> [...]`
Begins MVD playback from the file identified by _filename_ by creating a
new MVD channel.  This command does not require file extension to be
specified and supports filename autocompletion on TAB. Loads file from
‘demos/’ unless slash is prepended to _filename_, otherwise loads from the
root of quake file system. Multiple _filenames_ can be specified to create
a playlist.

* `-h` or `--help`: display help message
* `-l` or `--loop=<number>`: replay _number_ of times (0 means forever, replays once by default)
* `-n` or `--name=<string>`: specify channel name as _string_, default is `demX`
* `-r` or `--replace=<channel>`: replace existing _channel_ playlist with new entries, don't create a new channel

#### `mvdseek [+-]<timespec|percent>[%] [channel]`
Seeks the given amount of time during MVD playback on the specified
_channel_.  Prepend with `+` to seek forward relative to current position,
prepend with `-` to seek backward relative to current position.  Without
prefix, seeks to an absolute frame position within the MVD file, counted
from the last map change. See below for _timespec_ syntax description.
With `%` suffix, seeks to specified file position percentage.  Initial
forward seek may be slow, so be patient. For multi-map recordings, it is
not possible to return to the previous map by seeking. Seeking during demo
recording is not yet supported.


#### MVD time specification
Absolute or relative MVD time can be specified in one of the following
formats:

* `.FF`, where `FF` are frames
* `SS`, where `SS` are seconds
* `SS.FF`, where `SS` are seconds, `FF` are frames
* `MM:SS`, where `MM` are minutes, `SS` are seconds
* `MM:SS.FF`, where `MM` are minutes, `SS` are seconds, `FF` are frames

#### `mvdrecord [-hz] <filename> [channel]`
Start MVD recording on the specified _channel_ into `demos/_filename_.mvd2`.
There is no need to specify _channel_ if there is only one active channel.

* `-h` or `--help`: display help message
* `-z` or `--compress`: compress file with gzip

*TIP*: With Q2PRO it is possible to record a demo while playing back another one.

#### `mvdstop [channel]`
Stop MVD recording on the specified channel. There is no need to specify
_channel_ if there is only one active channel.

#### Record commands
As you have probably noticed, the same `mvdrecord` and `mvdstop` commands
introduced in the previous section are used here. This is possible since Q2PRO
server may be either in the MVD/GTV server or client mode, but not both at the
same time.

#### `mvdspawn`
Put the server into MVD/GTV mode, creating the Waiting Room channel.  This
command performs a full server restart. Done automatically as soon as the
first GTV connection is established or local MVD file is replayed.

#### `mvdchannels [mode]`
List all MVD channels (there may be none, if all GTV connections are
suspended). Optional _mode_ argument may be provided to show different
kind of information.

`r(ecordings)`: show MVD recording status

#### `mvdservers`
List all GTV connections.


Incompatibilities
-----------------

Q2PRO server tries to be compatible with other Quake 2 ports, including
original Quake 2 release. Compatibility, however, is defined in terms of full
file format and network protocol compatibility. Q2PRO is not meant to be a
direct replacement of your regular Quake 2 server. Some features are
implemented differently in Q2PRO, some may be not implemented at all. You may
need to review your config and adapt it for Q2PRO. This section tries to
document most of these incompatibilities so that when something doesn't work as
it used to be you know where to look. The following list may be incomplete.

- `ip` variable has been renamed to `net_ip`.

- `port` variable has been renamed to `net_port`, and
  `ip_hostport` and `hostport` aliases are no longer supported.

- `serverrecord` and `demomap` commands has been removed in favor of
  `mvdrecord` and `mvdplay`.

- On Windows, `./release/gamex86.dll` path will not be tried by default when
  loading the game DLL.  If you need this for loading some sort of a game mod
  proxy, use `sys_forcegamelib` variable.

- Q2PRO works only with virtual paths constrained to the quake file system.
  All paths are normalized before use so that it is impossible to go past virtual
  filesystem root using `../` components.  This means commands like these are
  equivalent and all reference the same file: `exec ../global.cfg`, `exec
  /global.cfg`, `exec global.cfg`.  If you have any config files in your Quake 2
  directory root, you should consider moving them into `baseq2/` to make them
  accessible.

- Likewise, `link` command syntax has been changed to work with virtual paths
  constrained to the quake file system. All arguments to `link` are normalized.

