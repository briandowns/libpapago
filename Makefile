cc = cc 

NAME = libpapago

UNAME_S = $(shell uname -s)

# respect traditional UNIX paths
INCDIR  = /usr/local/include
LIBDIR  = /usr/local/lib

CFLAGS  = -O3 -fPIC -Wall -Wextra
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

PAPAGO_USE_MAPLE ?= 0
PAPAGO_WITH_WSC ?= 0

ifeq ($(PAPAGO_USE_MAPLE),1)
	CFLAGS += -DPAPAGO_USE_MAPLE
	LDFLAGS += -lmaple
endif

EXAMPLES = example example_ssl example_websocket example_template example_rate_limit example_compression example_metrics example_streaming example_embedded example_logger_middleware example_wsclient

ifeq ($(UNAME_S),Darwin)
$(NAME).dylib: clean
	$(CC) -dynamiclib -o $@ papago.c $(CFLAGS) $(LDFLAGS)
ifeq ($(PAPAGO_WITH_WSC),1)
	$(CC) -dynamiclib -o libpapago_wsc.dylib papago_wsc.c $(CFLAGS) $(LDFLAGS)
endif
else
$(NAME).so: clean
	$(CC) -shared -o $@ papago.c $(LDFLAGS) $(CFLAGS)
ifeq ($(PAPAGO_WITH_WSC),1)
	$(CC) -shared -o libpapago_wsc.so papago_wsc.c $(CFLAGS) $(LDFLAGS)
endif
endif

.PHONY: tests
tests: clean
	$(CC) -o tests/tests tests/papago_test.c papago.c $(TEST_CFLAGS) $(LDFLAGS) -lcrosscheck
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
ifneq (,$(wildcard libpapago_wsc.dylib))
	cp libpapago_wsc.dylib $(LIBDIR)
endif
else
	cp $(NAME).so $(LIBDIR)
ifneq (,$(wildcard libpapago_wsc.so))
	cp libpapago_wsc.so $(LIBDIR)
endif
endif

uninstall:
	rm -f $(INCDIR)/papago.h
ifeq ($(UNAME_S),Darwin)
	rm -f $(INCDIR)/$(NAME).dylib
ifneq (,$(wildcard $(INCDIR)/libpapago_wsc.dylib))
	rm -f $(INCDIR)/libpapago_wsc.dylib
endif
else
	rm -f $(INCDIR)/$(NAME).so
ifneq (,$(wildcard $(INCDIR)/libpapago_wsc.so))
	rm -f $(INCDIR)/libpapago_wsc.so
endif
endif

.PHONY: clean
clean:
	rm -f $(NAME).dylib
	rm -f $(NAME).so
	rm -f $(EXAMPLES)
	rm -f tests/tests

.PHONY: example
example: clean
	$(CC) -o $@ papago.c examples/example.c $(CFLAGS) $(LDFLAGS)

.PHONY: example_ssl
example_ssl: clean
	$(CC) -o $@ papago.c examples/example_ssl.c $(CFLAGS) $(LDFLAGS)

.PHONY: example_websocket
example_websocket: clean
	$(CC) -o $@ papago.c examples/example_websocket.c $(CFLAGS) $(LDFLAGS)

.PHONY: example_template
example_template: clean
	$(CC) -o $@ examples/example_template.c papago.c $(CFLAGS) $(LDFLAGS)

.PHONY: example_rate_limit
example_rate_limit: clean
	$(CC) -o $@ papago.c examples/example_rate_limit.c $(CFLAGS) $(LDFLAGS)

.PHONY: example_compression
example_compression: clean
	$(CC) -o $@ papago.c examples/example_compression.c $(CFLAGS) $(LDFLAGS)

.PHONY: example_metrics
example_metrics: clean
	$(CC) -o $@ papago.c examples/example_metrics.c $(CFLAGS) $(LDFLAGS)

.PHONY: example_streaming
example_streaming: clean
	$(CC) -o $@ papago.c examples/example_streaming.c $(CFLAGS) $(LDFLAGS)

.PHONY: example_embedded
example_embedded: clean
	$(CC) -o $@ papago.c examples/example_embedded.c $(CFLAGS) $(LDFLAGS)

.PHONY: example_logger_middleware
example_logger_middleware: clean
	$(CC) -o $@ papago.c examples/example_logger_middleware.c $(CFLAGS) $(LDFLAGS)

.PHONY: example_wsclient
example_wsclient: clean
	$(CC) -o $@ papago_wsc.c examples/example_wsclient.c $(CFLAGS) -lwebsockets -lssl -lcrypto -lz -lm -lpthread

.PHONY: examples_all
examples_all: $(EXAMPLES)
