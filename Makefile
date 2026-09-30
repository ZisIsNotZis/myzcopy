SRCS = $(wildcard *.c)
BINS := $(SRCS:.c=)
BASICS := cat uncat tar untar tee untee raid unraid raid0 unraid0 raid1 unraid1 raid3 unraid3 raid5 unraid5 raid6 unraid6 raid10 unraid10 md5sum unmd5sum
all: $(BINS) $(BASICS)
$(BASICS): basic
	ln -sf basic $@
%: %.c
	gcc -Ofast -march=native -s $< -luring -o $@
clean:
	rm $(BINS) $(BASICS)