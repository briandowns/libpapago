cc = cc 

NAME = libpapago

UNAME_S = $(shell uname -s)

CFLAGS  = -O3 -fPIC -Wall -Wextra
ifeq ($(UNAME_S),FreeBSD)
	CFLAGS += $(shell pkg-config --cflags --libs libwebsockets) \
              $(shell pkg-config --cflags --libs libmicrohttpd) \
			  $(shell pkg-config --cflags --libs jansson) \
			  -lssl -lcrypto -lz
endif
TEST_CFLAGS = -g -fPIC -Wall -Wextra
LDFLAGS = -lwebsockets -lmicrohttpd -ljansson -lssl -lcrypto -lz -lm

# respect traditional UNIX paths
INCDIR  = /usr/local/include
LIBDIR  = /usr/local/lib

ifeq ($(UNAME_S),Darwin)
$(NAME).dylib: clean
	$(CC) -dynamiclib -o $@ logger.c maple.c papago.c $(CFLAGS) $(LDFLAGS)
endif
ifeq ($(UNAME_S),Linux)
$(NAME).so: clean
	$(CC) -shared -o $@ logger.c maple.c papago.c $(CFLAGS) $(LDFLAGS)
endif

.PHONY: tests
tests: clean
	$(CC) -o tests/tests tests/crosscheck.c tests/papago_test.c logger.c maple.c papago.c $(TEST_CFLAGS) $(LDFLAGS)
	tests/tests
	rm -f tests/tests

.PHONY: valgrind
valgrind: tests
	valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes --tool=memcheck ./tests/tests 2>&1 | awk -F':' '/definitely lost:/ {print $2}'

.PHONY: install
install: 
	cp papago.h $(INCDIR)
ifeq ($(UNAME_S),Linux)
	cp papago.h $(INCDIR)
	cp $(NAME).so $(LIBDIR)
endif
ifeq ($(UNAME_S),Darwin)
	cp papago.h $(INCDIR)
	cp $(NAME).dylib $(LIBDIR)
endif

uninstall:
	rm -f $(INCDIR)/papago.h
ifeq ($(UNAME_S),Linux)
	rm -f $(INCDIR)/$(NAME).so
endif
ifeq ($(UNAME_S),Darwin)
	rm -f $(INCDIR)/$(NAME).dylib
endif

.PHONY: clean
clean:
	rm -f $(NAME).dylib
	rm -f $(NAME).so
	rm -f example
	rm -f tests/tests

.PHONY: example
example: clean
	$(CC) -o $@ logger.c maple.c papago.c examples/example.c $(CFLAGS) $(LDFLAGS)

.PHONY: example_ssl
example_ssl: clean
	$(CC) -o $@ logger.c maple.c papago.c examples/example_ssl.c $(CFLAGS) $(LDFLAGS)

.PHONY: example_chat
example_chat: clean
	$(CC) -o $@ logger.c maple.c papago.c examples/example_chat.c $(CFLAGS) $(LDFLAGS)

.PHONY: example_template
example_template: clean
	$(CC) -o $@ logger.c maple.c examples/template_example.c papago.c $(CFLAGS) $(LDFLAGS)

.PHONY: all_examples
all_examples: example example_ssl example_chat example_template
