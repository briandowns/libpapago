cc = cc 

NAME = libpapago

UNAME_S = $(shell uname -s)

# respect traditional UNIX paths
INCDIR  = /usr/local/include
LIBDIR  = /usr/local/lib

CFLAGS  = -O3 -fPIC -Wall -Wextra

SRCS = papago.c

ifdef NO_LOGGER
	CFLAGS += -DNO_LOGGER
else
	SRCS += logger.c
endif

ifdef NO_TEMPLATE
	CFLAGS += -DNO_TEMPLATE
else
	SRCS += maple.c
endif

ifeq ($(UNAME_S),Darwin)
	CFLAGS += $(shell pkg-config --cflags --libs libwebsockets) \
              $(shell pkg-config --cflags --libs libmicrohttpd) \
			  $(shell pkg-config --cflags --libs jansson) \
              $(shell pkg-config --cflags --libs openssl) \
			  -lssl -lcrypto -lz
endif

TEST_CFLAGS = -g -fPIC -Wall -Wextra
LDFLAGS = -lwebsockets -lmicrohttpd -ljansson -lssl -lcrypto -lz -lm -lpthread

ifeq ($(UNAME_S),FreeBSD)
CFLAGS += -I$(INCDIR)
TEST_CFLAGS += -I$(INCDIR)
LDFLAGS += -L$(LIBDIR)
endif

EXAMPLES = example example_ssl example_websocket example_template example_rate_limit example_compression example_metrics

ifeq ($(UNAME_S),Darwin)
$(NAME).dylib: clean
	$(CC) -dynamiclib -o $@ $(SRCS) $(CFLAGS) $(LDFLAGS)
else
$(NAME).so: clean
	$(CC) -shared -o $@ $(SRCS) $(CFLAGS) $(LDFLAGS)
endif

.PHONY: tests
tests: clean
	$(CC) -o tests/tests tests/crosscheck.c tests/papago_test.c $(SRCS) $(TEST_CFLAGS) $(LDFLAGS)
	tests/tests
	rm -f tests/tests

.PHONY: valgrind
valgrind: tests
	valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes --tool=memcheck ./tests/tests 2>&1 | awk -F':' '/definitely lost:/ {print $2}'

.PHONY: install
install: 
	cp papago.h $(INCDIR)
ifeq ($(UNAME_S),Darwin)
	cp $(NAME).dylib $(LIBDIR)
else
	cp $(NAME).so $(LIBDIR)
endif

uninstall:
	rm -f $(INCDIR)/papago.h
ifeq ($(UNAME_S),Darwin)
	rm -f $(INCDIR)/$(NAME).dylib
else
	rm -f $(INCDIR)/$(NAME).so
endif

.PHONY: clean
clean:
	rm -f $(NAME).dylib
	rm -f $(NAME).so
	rm -f $(EXAMPLES)
	rm -f tests/tests

.PHONY: example
example: clean
	$(CC) -o $@ $(SRCS) examples/example.c $(CFLAGS) $(LDFLAGS)

.PHONY: example_ssl
example_ssl: clean
	$(CC) -o $@ $(SRCS) examples/example_ssl.c $(CFLAGS) $(LDFLAGS)

.PHONY: example_websocket
example_websocket: clean
	$(CC) -o $@ $(SRCS) examples/example_websocket.c $(CFLAGS) $(LDFLAGS)

.PHONY: example_template
example_template: clean
	$(CC) -o $@ $(SRCS) examples/example_template.c $(TEST_CFLAGS) $(LDFLAGS)

.PHONY: example_rate_limit
example_rate_limit: clean
	$(CC) -o $@ $(SRCS) examples/example_rate_limit.c $(CFLAGS) $(LDFLAGS)

.PHONY: example_compression
example_compression: clean
	$(CC) -o $@ $(SRCS) examples/example_compression.c $(CFLAGS) $(LDFLAGS)

.PHONY: example_metrics
example_metrics: clean
	$(CC) -o $@ $(SRCS) examples/example_metrics.c $(CFLAGS) $(LDFLAGS)

.PHONY: examples_all
examples_all: $(EXAMPLES)
