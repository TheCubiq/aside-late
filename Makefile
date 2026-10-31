aside-late.exe: main.c
	gcc -O2 -Wall -Wextra -municode -static -o $@ $<

clean:
	rm -f aside-late.exe

.PHONY: clean
