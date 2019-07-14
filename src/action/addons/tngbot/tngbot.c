/*----------------------------------------------------------------------------
 * TNG IRC Bot for AQ2 Servers
 * (c) 2001 by Stefan Giesen aka Igor[Rock]
 * All rights reserved
 *
 * $Id: tngbot.c,v 1.9 2003/06/15 21:43:29 igor Exp $
 *
 *----------------------------------------------------------------------------
 * Usage: tngbot <ircserver>[:port] <channelname> <nickname>
 * <ircserver> - Name or IP address of IRC server
 * [:port]     - Port for the IRC server, default is 6667
 * <channel>   - Channel to join (without leading '#')
 * <nickname>  - Bot Nickname
 *
 * Example:
 * tngbot irc.barrysworld.com:6666 clanwar-tv MyTVBot
 * 
 * would connect the bot to the irc server at barrysworld at port 6666
 * with the Nickname "MyTVBot" and would join "#clanwar-tv" after connecting
 *
 * Features:
 * - Bot will reconnect automatically after disconnect
 * - No changes (beside setting the "logfile" variable) in your server config
 *   necessary, the bot will use the standard text logfile.
 * - Unknown commands, some errors and all rcon/password related console
 *   output is filtered out.
 *
 * Bugs/Missing features:
 * - The Bot has to be started in the action dir and the logfilename used is
 *   the standard one from Q2: "qconsole.log"
 *   This will be changed to a command line option later
 * - If the Nick already exists on IRC, the Bot won't be able to connect
 * - The Bot has to be started _after_ the AQ2 server itself (because the
 *   logfile has to be opened for writing by AQ2)
 *
 *----------------------------------------------------------------------------
 * $Log: tngbot.c,v $
 * Revision 1.9  2003/06/15 21:43:29  igor
 * added some code for characters <32
 *
 * Revision 1.8  2001/12/08 15:53:05  igor_rock
 * corrected a wrong offset
 *
 * Revision 1.7  2001/12/08 15:46:39  igor_rock
 * added the private message command "cycle" so the bot disconencts and
 * restarts after a 15 sec pause.
 *
 * Revision 1.6  2001/12/03 15:01:06  igor_rock
 * added password for joining the cahnnel if protected
 *
 * Revision 1.5  2001/12/03 14:52:01  igor_rock
 * fixed some bugs, added some features / channel modes
 *
 * Revision 1.4  2001/11/30 19:27:33  igor_rock
 * - added password to get op from the bot (with "/msg botname op password")
 * - the bot sets the +m flag so other people can't talk in the channel (which
 *   would maybe cause lag on the game server itself, since the bot runs on the
 *   gameserver)
 *
 * Revision 1.3  2001/11/29 18:43:16  igor_rock
 * added new format 'V' ("nickname FIXED_TEXT variable_text")
 *
 * Revision 1.2  2001/11/29 18:30:48  igor_rock
 * corrected a smaller bug
 *
 * Revision 1.1  2001/11/29 17:58:31  igor_rock
 * TNG IRC Bot - First Version
 *
 *----------------------------------------------------------------------------
 */

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <string.h>
#include <sys/time.h>
#include <fcntl.h>


#define IRC_PORT 6667
#define BUFLEN   2048

#define GREY   "14"
#define BLUE   "12"
#define GREEN   "3"
#define RED     "4"
#define ORANGE  "7"

static int   sock;
static int   file;

static char *version="TNG IRC Bot by Stefan Giesen (c)2001";

char   logfile[]   = "qconsole.log";
char   colorfile[] = "tngbot.col";

char   ircserver[BUFLEN];
char   channel[BUFLEN];
char   nickname[BUFLEN];
char   password[BUFLEN];
char  *txtbuffer;
char **filtertxt;
int    filteranz;
int    port;

int get_colorfile ( )
{
  int     ret = 0;
  size_t  size;
  int     i;
  char   *cp;
  FILE   *fp;
  
  fp = fopen(colorfile, "r");
  if (fp) {
    fseek (fp, 0, SEEK_END);
    size = (size_t) ftell (fp);
    fseek (fp, 0, SEEK_SET);
    if ((txtbuffer = (char *) calloc(size, sizeof(char))) == NULL) {
      fprintf (stderr, "Out of memory in get_colofile!\n");
      exit (1);
    }
    if (fread (txtbuffer, sizeof(char), size, fp) != size) {
      fprintf (stderr, "Read Error in '%s'\n", colorfile);
      exit (1);
    }
    fclose (fp);
    filteranz = 1;
    for (cp = txtbuffer; cp < (txtbuffer + (int) size); cp++) {
      if (*cp == '\n') {
	filteranz++;
      }
    }
    if ((filtertxt = (char **) calloc(filteranz, sizeof(char *))) == NULL) {
      fprintf (stderr, "Out of memory in get_colorfile!\n");
      exit (1);
    }
    i = 0;
    printf ("Filter count: %d\n", filteranz);
    filtertxt[i++] = txtbuffer;
    for (cp = txtbuffer; (cp < (txtbuffer + (int) size)) && (i < filteranz); cp++) {
      if (*cp == '\n') {
	*cp = 0;
	filtertxt[i++] = cp + 1;
      }
    }

#ifdef DEBUG
    for (i=0; i < filteranz; i++) {
      printf ("Filter %2.2d: '%s'\n", i, filtertxt[i]);
    }
#endif
    ret = 1;
  } else {
    fprintf (stderr, "Colorizing file 'tngbot.col' not found, colors not used\n");
    ret = 0;
  }
  return (ret);
}

void colorize (char *inbuf, char *outbuf)
{
  int    i;
  int    from;
  int    to;
  int    found = 0;
  char  *cp;
  int    done = 0;
  
  bzero (outbuf, BUFLEN);
  
  if (! done) {
    for (i = 0; (i < strlen (inbuf)) && (inbuf[i] != ' '); i++)
      ;
    if (i && (i < strlen(inbuf))) {
      if (inbuf[i-1] == ':') {
	sprintf (outbuf, "%c%s%s%c%s\n", 0x03, GREEN, inbuf, 0x03, GREY);
	done = 1;
      }
    }
  }

  if (!done) {
    for (i = 0; i < filteranz; i++ ) {
      if ((cp = strstr (inbuf, &filtertxt[i][1])) != NULL) {
	found = (int) (cp - inbuf);

	switch (filtertxt[i][0]) {
	case 'K':			/* Kill */
	  sprintf (outbuf, "%c%s", 0x03, BLUE);
	  for (from = 0, to = strlen(outbuf); from < found; from++, to++) {
	    outbuf[to] = inbuf[from];
	  }
	  sprintf (&outbuf[to], "%c%s%s%c%s", 0x03, GREY, &filtertxt[i][1], 0x03, BLUE);
	  from += strlen (&filtertxt[i][1]);
	  to = strlen(outbuf);
	  if (inbuf[from] == ' ') {
	    outbuf[to++] = ' ';
	    outbuf[to] = 0;
	    from++;
	  }
	  while ((inbuf[from] != ' ') && inbuf[from]){
	    outbuf[to++] = inbuf[from++];
	  }
	  sprintf (&outbuf[to], "%c%s%s", 0x03, GREY, &inbuf[from]);
	  done = 1;
	  break;

	case 'D':			/* Own Death */
	  sprintf (outbuf, "%c%s", 0x03, BLUE);
	  for (from = 0, to = strlen(outbuf); from < found; from++, to++) {
	    outbuf[to] = inbuf[from];
	  }
	  sprintf (&outbuf[to], "%c%s%s\n", 0x03, GREY, &filtertxt[i][1]);
	  done = 1;
	  break;

	case 'V':			/* Vote related */
	  sprintf (outbuf, "%c%s", 0x03, BLUE);
	  for (from = 0, to = strlen(outbuf); from < found; from++, to++) {
	    outbuf[to] = inbuf[from];
	  }
	  sprintf (&outbuf[to], "%c%s%s\n", 0x03, GREY, &inbuf[from]);
	  done = 1;
	  break;

	case 'G':
	  sprintf (outbuf, "%c%s%s%c%s\n", 0x03, RED, inbuf, 0x03, GREY);
	  done = 1;
	  break;

	case 'S':
	  sprintf (outbuf, "%c%s%s%c%s\n", 0x03, ORANGE, inbuf, 0x03, GREY);
	  done = 1;
	  break;

	case '-':
	  done = 1;
	  break;

	default:
	  strncpy (outbuf, inbuf, strlen(inbuf));
	  done = 1;
	  break;
	}

	i = filteranz;
      }
    }
  }
  
  if (! done) {
    strncpy (outbuf, inbuf, strlen(inbuf));
  }
}


int do_irc_reg( )
{
  char inbuf[BUFLEN];
  char outbuf[BUFLEN];
  char *newbuf;

  sprintf (outbuf, "NICK %s\n\r", nickname);
  write (sock, outbuf, strlen(outbuf));
  fprintf(stderr, "Sent nick (%s)\n", nickname);
  
  while(1) {
    bzero(inbuf, sizeof(inbuf));
    
    if ((read(sock, inbuf, sizeof(inbuf))) == 0) {
      close (sock);
      fprintf(stderr, "Dropped.\n");
      sleep (15);
      return (0);
    }
    
    newbuf = inbuf;
    
    while(((strncmp(newbuf, "PING :", 6)) != 0) && (strlen(newbuf) > 0)) {
      newbuf++;
    }
    
    if ((strncmp(newbuf, "PING :", 6)) == 0){
      printf("Ping - Pong\n");
      sprintf(outbuf, "pong %s\n\r", &newbuf[6]);
      write(sock, outbuf, strlen(outbuf));

      sprintf(outbuf, "user tngbot 12 * :%s\n\r", version);
      printf("User sent: %s\n", outbuf);
      write(sock, outbuf, strlen(outbuf));
      
      break;
    }
  }
 
  if (password[0]) {
    sprintf(outbuf, "join #%s %s\n\r", channel, password);
    printf ("Joining #%s %s\n", channel, password);
  } else {
    sprintf(outbuf, "join #%s\n\r", channel);
    printf ("Joining #%s\n", channel);
  }
  write(sock, outbuf, strlen(outbuf));

  if (password[0]) {
    sprintf (outbuf, "mode #%s +mntk %s\n", channel, password);
  } else {
    sprintf (outbuf, "mode #%s +mnt\n", channel);
  }
  write (sock, outbuf, strlen(outbuf));

  return (1);
}


int parse_input (char *inbuf)
{
  int  ret = 0;
  int  pos1;
  int  pos2;
  char temp[BUFLEN];
  char von[BUFLEN];
  char outbuf[BUFLEN];

  if ((inbuf[0] == ':') && (strstr(inbuf, "PRIVMSG") != NULL)) {
    for (pos1 = 1, pos2 = 0; (inbuf[pos1] != ' ') && (pos1 < strlen(inbuf)); pos1++, pos2++) {
      von[pos2] = inbuf[pos1];
    }
    von[pos2] = 0;
    if (pos1 < strlen(inbuf)) {
      if (strstr (von, "!") != NULL) {
	/* user message */
	for (pos2 = 0; (pos2 < strlen(von)) && (von[pos2] != '!'); pos2++)
	  ;
	von[pos2] = 0;
	pos1 += strlen (" PRIVMSG ");

	if (strncmp (&inbuf[pos1], nickname, strlen(nickname)) == 0) {
	  /* private message */
	  pos1 += strlen(nickname);
	  pos1 += strlen (" :");
	  if (password[0]) {
	    if (strncmp (&inbuf[pos1], "op ", 3) == 0) {
	      pos1 += 3;
	      if (strncmp (&inbuf[pos1], password, strlen (password)) == 0) {
		sprintf (outbuf, "MODE #%s +o %s\n", channel, von);
		write (sock, outbuf, strlen(outbuf));
	      }
	    } else if (strncmp (&inbuf[pos1], "cycle ", 6) == 0) {
	      pos1 += 6;
	      if (strncmp (&inbuf[pos1], password, strlen (password)) == 0) {
		ret = 1;
	      }
	    }
	  }
	} else {
	  char *cp;
	  for (cp = inbuf; *cp; cp++) {
	    if (*cp < 32) {
	      printf ("\\%3.3d", (int) *cp);
	    } else {
	      printf ("%c", *cp);
	    }
	  }
	  printf ("\n");
	  /* public message, we ignore it */
	}
      } else {
	/* server message */
	/* we ignore these in the moment */
      }
    }
  } else {
    sprintf (temp, " KICK #%s %s :", channel, nickname);
    if ((inbuf[0] == ':') && (strstr(inbuf, temp) != NULL)) {
      if (password[0]) {
	sprintf(outbuf, "join #%s %s\n\r", channel, password);
	printf ("Joining #%s %s\n", channel, password);
      } else {
	sprintf(outbuf, "join #%s\n\r", channel);
	printf ("Joining #%s\n", channel);
      }
      write(sock, outbuf, strlen(outbuf));
    } else {
      printf ("%s\n", inbuf);
    }
  }

  return (ret);
}


void irc_loop ( )
{
  int  i;
  int  e;
  int  anz = 0;
  int  anz_net = 0;
  char inbuf[BUFLEN];
  char colbuf[BUFLEN];
  char outbuf[BUFLEN];
  char filebuf[BUFLEN];
  
  while((anz_net = read(sock, inbuf, sizeof(inbuf))) != 0) {
    anz = read(file, filebuf, sizeof(filebuf));
    if (anz > 0) {
      for (i = 0, e = 0; e < anz; e++) {
	if (filebuf[e] == '\n') {
	  filebuf[e] = 0;
	  if ((filebuf[i] != '(')) {
	    if (strncmp (&filebuf[i], "Unknown", 7) == 0) {
	    } else if (strncmp (&filebuf[i], "\"password\"", 10) == 0) {
	    } else if (strncmp (&filebuf[i], "\"rcon_password\"", 15) == 0) {
	    } else if (strncmp (&filebuf[i], "rcon", 4) == 0) {
	    } else if (strncmp (&filebuf[i], "[DEAD]", 6) == 0) {
	    } else if (strncmp (&filebuf[i], "Sending ", 8) == 0) {
	    } else if (strncmp (&filebuf[i], "droptofloor:", 12) == 0) {
	    } else if (strncmp (&filebuf[i], "Error opening file", 18) == 0) {
	    } else if (strstr (&filebuf[i], "doesn't have a spawn function") != NULL) {
	    } else if (strstr (&filebuf[i], "with no distance set") != NULL) {
	    } else {
	      if (strlen(&filebuf[i]) > 0) {
		if (filteranz > 0) {
		  bzero (colbuf, sizeof(colbuf));
		  colorize (&filebuf[i], colbuf);
		  sprintf (outbuf, "PRIVMSG #%s :%s\n", channel, colbuf);
		}else {
		  sprintf (outbuf, "PRIVMSG #%s :%s\n", channel, &filebuf[i]);
		}
		write (sock, outbuf, strlen(outbuf));
	      }
	    }
	  }
	  i = e + 1;
	}
      }
    }

    if (anz_net == -1) {
      if (errno != EAGAIN) {
	printf ("System error occured (errno = %d)\n", errno);
	break;
      }
    }

    if (anz_net > 0) {
      if ((strncmp(inbuf, "PING :", 6)) == 0) {
	/* answer with a pong */
	printf ("Ping - Pong\n");
	sprintf(outbuf, "pong %s\n\r", &inbuf[6]);
	write(sock, outbuf, strlen(outbuf));
      } else {	
	if (parse_input (inbuf)) {
	  break;
	}
      }
      bzero(inbuf,  sizeof(inbuf));
      bzero(outbuf, sizeof(outbuf));
      bzero(filebuf, sizeof(filebuf));
    }
  }

  close(sock);
  printf("Connection dropped.\n");
  sleep(15); /* So we don't get throttled */
}


void main(int argc, char *argv[])
{
  int                flags;
  char               *portstr;
  struct sockaddr_in hostaddress;
  struct in_addr     ipnum;
  struct hostent     *hostdata;

  fprintf (stderr, "AQ2 TNG IRC Bot v0.1 by Igor[Rock]\n");
  fprintf (stderr, "EMail: igor@rock-clan.de\n");

  if (argc < 4) {
    fprintf (stderr, "Usage: tngbot <ircserver>[:port] <channelname> <nickname> [password]\n");
    fprintf (stderr, "<ircserver> - Name or IP address of IRC server\n");
    fprintf (stderr, "[:port]     - Port for the IRC server\n");
    fprintf (stderr, "<channel>   - Channel to join (without leading '#')\n");
    fprintf (stderr, "<nickname>  - Bot Nickname\n");
    fprintf (stderr, "[password]  - OP-password for IRC\n\n");
    exit (1);
  }

  get_colorfile();

  strncpy (ircserver, argv[1], BUFLEN);
  strncpy (channel,   argv[2], BUFLEN);
  strncpy (nickname,  argv[3], BUFLEN);
  if (argc == 5) {
    strncpy (password, argv[4], BUFLEN);
  } else {
    password[0] = 0;
  }

  port = htons (IRC_PORT);

  portstr = strchr(argv[1], ':');
  if (portstr) {
    *portstr++ = '\0';
    port = htons(atoi(portstr));
  }
  
  if (! inet_aton(argv[1], &ipnum)) {
    /* Maybe it's a FQDN */
    hostdata = gethostbyname (ircserver);
    if (hostdata==NULL) {
      fprintf (stderr, "Invalid hostname or wrong address!\n");
      fprintf (stderr, "Please use an existing hostname or,\n");
      fprintf (stderr, "the xxx.xxx.xxx.xxx format.\n");
      exit (1);
    } else {
      /* Seems I'm just to stupid to find the right functio for this... */
      ((char *)&ipnum.s_addr)[0] = hostdata->h_addr_list[0][0];
      ((char *)&ipnum.s_addr)[1] = hostdata->h_addr_list[0][1];
      ((char *)&ipnum.s_addr)[2] = hostdata->h_addr_list[0][2];
      ((char *)&ipnum.s_addr)[3] = hostdata->h_addr_list[0][3];
    }
  }
  fprintf (stderr, "Using Server %s:%i\n", inet_ntoa(ipnum), ntohs(port));                                          
  bzero((char *) &hostaddress, sizeof(hostaddress));
  hostaddress.sin_family = AF_INET;
  hostaddress.sin_addr.s_addr = ipnum.s_addr;
  hostaddress.sin_port = port;

  file = open (logfile, O_NONBLOCK | O_RDONLY);
  if (file != -1) {
    while (1) {
      if ((sock = socket (AF_INET, SOCK_STREAM, 0)) == -1) {
	perror ("Couldn't open socket");
	exit (1);
      } else {
	if (connect(sock, (struct sockaddr *)&hostaddress, sizeof(hostaddress)) == -1) {
	  perror ("Couldn't connect socket");
	  exit (1);
	} else {
	  printf("Connected to %s:%s\n", ircserver, portstr ? portstr: "6667");
	}
      }
      
      flags = fcntl(sock, F_GETFL);
      flags |= O_NONBLOCK;
      if (fcntl(sock, F_SETFL, (long) flags)) {
	printf ("Couldn't switch to non-blocking\n");
	exit (1);
      }
      
      if (do_irc_reg()) {
	irc_loop();
      }
      close (sock);
    }
    close (file);
  } else {
    fprintf (stderr, "Couldn't open file '%s'\n", logfile);
  }
  
}
    
