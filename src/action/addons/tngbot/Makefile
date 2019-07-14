#-----------------------------------------------------------------------------
# Makefile for the TNG IRC Bot
#
# $Id: Makefile,v 1.1 2001/11/29 17:58:31 igor_rock Exp $
#
# (c) 2001 by Stefan Giesen aka Igor[Rock]
# All rights reserved
#-----------------------------------------------------------------------------
# $Log: Makefile,v $
# Revision 1.1  2001/11/29 17:58:31  igor_rock
# TNG IRC Bot - First Version
#
#

CC      = gcc
CFLAGS  = -g -O2 -Wall

all: tngbot

.c.o:
	$(CC) $(CFLAGS) -c $<

tngbot: tngbot.o

clean:
	rm -f *.o

realclean:
	rm -f *.o
	rm -f tngbot
