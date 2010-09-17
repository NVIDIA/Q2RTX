# Copyright (C) 2008 Andrey Nazarov
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; either version 2
# of the License, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  
#
# See the GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

OBJFILES+=$(SRCFILES:%.c=%.o) $(ASMFILES:%.s=%.o) $(RESFILES:%.rc=%.o)

default: $(TARGET)

all: $(TARGET)

binary: $(TARGET)

clean:
	@rm -f *.d *.o $(TARGET)

.PHONY: clean

%.o: %.c
	@echo [CC] $@
	@$(CC) $(CFLAGS) -c -o $@ $<
	
%.o: %.s
	@echo [AS] $@
	@$(CC) $(CFLAGS) $(ASMFLAGS) -x assembler-with-cpp -c -o $@ $<

%.o: %.rc
	@echo [RC] $@
	@$(WINDRES) $(RESFLAGS) -o $@ $<

$(TARGET): $(OBJFILES)
	@echo [LD] $@
	@$(CC) -o $@ $^ $(LDFLAGS)

strip: $(TARGET)
	@echo [ST] $<
	@$(STRIP) $<

-include *.d

