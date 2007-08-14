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
	$(CC) $(CFLAGS) -c -o $@ $<
	
%.o: %.s
	$(CC) $(CFLAGS) $(ASMFLAGS) -x assembler-with-cpp -c -o $@ $<

%.o: %.rc
	$(WINDRES) $(RESFLAGS) -o $@ $<

$(TARGET): $(OBJFILES)
	$(CC) -o $@ $^ $(LDFLAGS)

-include *.d

