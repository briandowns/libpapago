cc = cc 

NAME = libpapago

UNAME_S = $(shell uname -s)

# respect traditional UNIX paths
INCDIR  = /usr/local/include
LIBDIR  = /usr/local/lib

# let's just look for all the things...
CFLAGS = -O3 -fPIC -Wextra -Wall -Wformat -Wformat=2 \
	-Wconversion -Wimplicit-fallthrough \
	-Werror=format-security \
	-U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=3 \
	-D_GLIBCXX_ASSERTIONS \
	-D_LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_FAST \
	-fstrict-flex-arrays=3 \
	-fstack-clash-protection -fstack-protector-strong \

ifeq ($(UNAME_S),Darwin)
	CFLAGS += $(shell pkg-config --cflags --libs libwebsockets) \
              $(shell pkg-config --cflags --libs libmicrohttpd) \
              $(shell pkg-config --cflags --libs openssl) \
			  -lssl -lcrypto -lz
endif

TEST_CFLAGS = $(CFLAGS) -g
LDFLAGS = -lwebsockets -lmicrohttpd -lssl -lcrypto -lz -lm -lpthread -lgnutls \
	-Wl,-z,nodlopen -Wl,-z,noexecstack \
	-Wl,-z,relro -Wl,-z,now \
	-Wl,--as-needed -Wl,--no-copy-dt-needed-entries

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

EXAMPLES = example \
	example_ssl \
	example_websocket \
	example_template \
	example_rate_limit \
	example_compression \
	example_metrics \
	example_streaming \
	example_embedded \
	example_logger_middleware \
	example_token_auth_middleware \
	example_wsclient \
	example_static_dir \
	example_mtls \
	example_cors \
	example_multipart

.PHONY: example
example: clean
	$(CC) -o $@ papago.c examples/$@.c $(CFLAGS) $(LDFLAGS)

.PHONY: example_ssl
example_ssl: clean
	$(CC) -o $@ papago.c examples/$@.c $(CFLAGS) $(LDFLAGS)

.PHONY: example_websocket
example_websocket: clean
	$(CC) -o $@ papago.c examples/$@.c $(CFLAGS) $(LDFLAGS)

.PHONY: example_template
example_template: clean
	$(CC) -o $@ examples/$@.c papago.c $(CFLAGS) $(LDFLAGS)

.PHONY: example_rate_limit
example_rate_limit: clean
	$(CC) -o $@ papago.c examples/$@.c $(CFLAGS) $(LDFLAGS)

.PHONY: example_compression
example_compression: clean
	$(CC) -o $@ papago.c examples/$@.c $(CFLAGS) $(LDFLAGS)

.PHONY: example_metrics
example_metrics: clean
	$(CC) -o $@ papago.c examples/$@.c $(CFLAGS) $(LDFLAGS) -ljansson

.PHONY: example_streaming
example_streaming: clean
	$(CC) -o $@ papago.c examples/$@.c $(CFLAGS) $(LDFLAGS)

.PHONY: example_embedded
example_embedded: clean
	$(CC) -o $@ papago.c examples/$@.c $(CFLAGS) $(LDFLAGS)

.PHONY: example_form
example_form: clean
	$(CC) -o $@ papago.c examples/$@.c $(CFLAGS) $(LDFLAGS)

.PHONY: example_logger_middleware
example_logger_middleware: clean
	$(CC) -o $@ papago.c examples/$@.c $(CFLAGS) $(LDFLAGS)

.PHONY: example_token_auth_middleware
example_token_auth_middleware: clean
	$(CC) -o $@ papago.c examples/$@.c $(CFLAGS) $(LDFLAGS)

.PHONY: example_wsclient
example_wsclient: clean
	$(CC) -o $@ papago_wsc.c examples/$@.c $(CFLAGS) -lwebsockets -lssl -lcrypto -lz -lm -lpthread

.PHONY: example_static_dir
example_static_dir: clean
	$(CC) -o $@ papago.c examples/$@.c $(CFLAGS) $(LDFLAGS)

.PHONY: example_mtls
example_mtls: clean
	$(CC) -o $@ papago.c examples/$@.c $(CFLAGS) $(LDFLAGS)

.PHONY: example_cors
example_cors: clean
	$(CC) -o $@ papago.c examples/$@.c $(CFLAGS) $(LDFLAGS)

.PHONY: example_multipart
example_multipart: clean
	$(CC) -o $@ papago.c examples/$@.c $(CFLAGS) $(LDFLAGS)

.PHONY: examples_all
examples_all: $(EXAMPLES)
