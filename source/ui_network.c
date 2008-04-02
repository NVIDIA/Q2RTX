/*
Copyright (C) 2003-2006 Andrey Nazarov

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/

#include "ui_local.h"

/*
=============================================================================

NETWORK SETUP MENU

=============================================================================
*/

typedef struct m_network_s {
	menuFrameWork_t	menu;
	menuSpinControl_t	connection;
	menuField_t	rate;
	menuField_t	maxpackets;
	menuField_t	maxfps;
	menuSpinControl_t	async;
} m_network_t;

static m_network_t	m_network;


#define ID_CONNECTION	103
#define ID_PARAMS		104


static const char *connectionNames[] = {
	"28.8 Modem",
	"33.6 Modem",
	"Single ISDN",
	"Dual ISDN/Cable",
	"T1/LAN",
	"User defined",
	NULL
};

static const char *yesnoNames[] = {
	"no",
	"yes",
	NULL
};

static const int connectionValues[][2] = {
// FIXME - are these values correct?
	{  2700, 30 },
	{  3200, 30 },
	{  5000, 60 },
	{ 10000, 60 },
	{ 25000, 120 }
};

static const int numConnectionValues = sizeof( connectionValues ) / sizeof( connectionValues[0] );

static void SetInitialConnectionParams( void ) {
	IF_Init( &m_network.rate.field, 6, 6, cvar.VariableString( "rate" ) );
	IF_Init( &m_network.maxpackets.field, 6, 6, cvar.VariableString( "cl_maxpackets" ) );
	IF_Init( &m_network.maxfps.field, 6, 6, cvar.VariableString( "cl_maxfps" ) );
}

static void ConnectionCallback( void  ) {
	if( m_network.connection.curvalue == numConnectionValues ) {
		SetInitialConnectionParams();
		return;
	}

	IF_Replace( &m_network.rate.field, va( "%i", connectionValues[m_network.connection.curvalue][0] ) );
	IF_Replace( &m_network.maxpackets.field, va( "%i", connectionValues[m_network.connection.curvalue][1] ) );

}

static void SetConnectionType( void ) {
	int rate, maxpackets, maxfps;
	int i;

	rate = atoi( m_network.rate.field.text );
	maxpackets = atoi( m_network.maxpackets.field.text );
	maxfps = atoi( m_network.maxfps.field.text );

	for( i=0 ; i<numConnectionValues ; i++ ) {
		if( rate == connectionValues[i][0] &&
			maxpackets == connectionValues[i][1] &&
			maxfps == connectionValues[i][2] )
		{
			break;
		}
	}

	m_network.connection.curvalue = i;
}


static void ApplyChanges( void ) {
	cvar.Set( "rate", m_network.rate.field.text );
	cvar.Set( "cl_maxpackets", m_network.maxpackets.field.text );
	cvar.Set( "cl_maxfps", m_network.maxfps.field.text );
	cvar.SetInteger( "cl_async", m_network.async.curvalue );

	client.StopAllSounds();

	ref.EndFrame();
}

static int NetworkMenu_Callback( int id, int msg, int param ) {
	switch( msg ) {
	case QM_CHANGE:
		switch( id ) {
		case ID_CONNECTION:
			ConnectionCallback();
			break;
		case ID_PARAMS:
			SetConnectionType();
			break;
		default:
			break;
		}
		break;
    case QM_DESTROY:
        ApplyChanges();
        break;
    case QM_SIZE:
        Menu_Size( &m_network.menu );
        break;
	default:
		break;
	}

	return QMS_NOTHANDLED;

}

static void Network_MenuInit( void ) {
	memset( &m_network, 0, sizeof( m_network ) );

	m_network.menu.callback = NetworkMenu_Callback;

	m_network.connection.generic.type			= MTYPE_SPINCONTROL;
	m_network.connection.generic.flags			= QMF_HASFOCUS;
	m_network.connection.generic.id				= ID_CONNECTION;
	m_network.connection.generic.name			= "connection type";
	m_network.connection.curvalue				= numConnectionValues;
	m_network.connection.itemnames				= connectionNames;

	m_network.rate.generic.type = MTYPE_FIELD;
	m_network.rate.generic.flags = QMF_NUMBERSONLY;
	m_network.rate.generic.name = "rate";
	m_network.rate.generic.id	= ID_PARAMS;

	m_network.maxpackets.generic.type = MTYPE_FIELD;
	m_network.maxpackets.generic.flags = QMF_NUMBERSONLY;
	m_network.maxpackets.generic.name = "maxpackets";
	m_network.maxpackets.generic.id	= ID_PARAMS;

	m_network.maxfps.generic.type = MTYPE_FIELD;
	m_network.maxfps.generic.flags = QMF_NUMBERSONLY;
	m_network.maxfps.generic.name = "maxfps";
	m_network.maxfps.generic.id	= ID_PARAMS;

	m_network.async.generic.type			= MTYPE_SPINCONTROL;
	m_network.async.generic.name			= "async physics";
	m_network.async.curvalue				= cvar.VariableInteger( "cl_async" ) ? 1 : 0;
	m_network.async.itemnames				= yesnoNames;

	m_network.menu.banner = "Network";

	SetInitialConnectionParams();
	SetConnectionType();

	Menu_AddItem( &m_network.menu, &m_network.connection );
	Menu_AddItem( &m_network.menu, &m_network.rate );
	Menu_AddItem( &m_network.menu, &m_network.maxpackets );
	Menu_AddItem( &m_network.menu, &m_network.maxfps );
	Menu_AddItem( &m_network.menu, &m_network.async );
}

void M_Menu_Network_f( void ) {
	Network_MenuInit();
	UI_PushMenu( &m_network.menu );
}

