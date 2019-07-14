//-----------------------------------------------------------------------------
// IRC related definitions
//
// $Id: tng_irc.h,v 1.2 2003/06/19 15:53:26 igor_rock Exp $
//
//-----------------------------------------------------------------------------
// $Log: tng_irc.h,v $
// Revision 1.2  2003/06/19 15:53:26  igor_rock
// changed a lot of stuff because of windows stupid socket implementation
//
// Revision 1.1  2003/06/15 21:45:11  igor
// added IRC client
//
//-----------------------------------------------------------------------------

#define IRC_SERVER  	"irc.barrysworld.com"
#define IRC_PORT    	"6667"
#define IRC_CHANNEL 	""
#define IRC_CHMODE  	"nt"
#define IRC_USER    	"TNG-Bot"
#define IRC_PASSWD  	""
#define IRC_TOPIC	"AQ2 TNG IRC-Bot"

#define IRC_BUFLEN	2048

#define IRC_CBOLD	2		// Character for Bold on/off
#define IRC_CMCOLORS	3		// Character which starts Mirc like color change
#define IRC_CPLAIN	15		// Character for back to plain text
#define IRC_CUNDERLINE	31		// Character for underline on/off

#define IRC_C_GREY25	1		// color numbers, use: printf ("%c%dfoobar", IRC_CMCOLORS, IRC_C_GREY25);
#define IRC_C_DBLUE	2
#define IRC_C_DGREEN	3
#define IRC_C_DRED	4
#define IRC_C_DYELLOW	5
#define IRC_C_DVIOLET	6
#define IRC_C_ORANGE	7
#define IRC_C_YELLOW	8
#define IRC_C_GREEN	9
#define IRC_C_DCYAN	10
#define IRC_C_CYAN	11
#define IRC_C_BLUE	12
#define IRC_C_VIOLET	13
#define	IRC_C_GREY75	14
#define IRC_C_GREY50	15

#define IRC_T_SERVER	0
#define IRC_T_GAME	1
#define IRC_T_TOPIC	2
#define IRC_T_VOTE	3
#define IRC_T_DEATH	4
#define IRC_T_KILL	5
#define IRC_T_TALK	6


typedef struct {
  int   ircsocket;
  char  ircserver[IRC_BUFLEN];
  int   ircport;
  char  ircuser[IRC_BUFLEN];
  char  ircpasswd[IRC_BUFLEN];
  char  ircchannel[IRC_BUFLEN];
  int   ircstatus;
  char  input[IRC_BUFLEN];
} tng_irc_t;

#ifdef TNG_IRC_C
cvar_t *ircserver;
cvar_t *ircport;
cvar_t *ircchannel;
cvar_t *ircuser;
cvar_t *ircpasswd;
cvar_t *irctopic;
cvar_t *ircbot;
cvar_t *ircop;
cvar_t *ircstatus;
cvar_t *ircmlevel;
cvar_t *ircdebug;
cvar_t *ircadminpwd;
#else	
extern cvar_t *ircserver;
extern cvar_t *ircport;
extern cvar_t *ircchannel;
extern cvar_t *ircuser;
extern cvar_t *ircpasswd;
extern cvar_t *irctopic;
extern cvar_t *ircbot;
extern cvar_t *ircop;
extern cvar_t *ircstatus;
extern cvar_t *ircmlevel;
extern cvar_t *ircdebug;
extern cvar_t *ircadminpwd;
#endif

void IRC_init       (void);
void IRC_exit       (void);
void IRC_printf     (int type, char *fmt, ... );
void IRC_poll       (void);
void SVCmd_ircraw_f (void);
