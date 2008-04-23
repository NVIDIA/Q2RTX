# -----------------------------
# q2pro makefile by [SkulleR]
# -----------------------------

OBJFILES+=$(SRCFILES:%.c=%.o) $(ASMFILES:%.s=%.o) $(RESFILES:%.rc=%.o)

default: $(TARGET)

all: $(TARGET)

clean:
	@rm -f *.d
	@rm -f *.o
	@rm -f $(TARGET)
	
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

-include *.d

