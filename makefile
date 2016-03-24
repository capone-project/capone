include config.mk

PROTOBUF=proto/capabilities.proto \
		 proto/connect.proto \
		 proto/discovery.proto \
		 proto/encryption.proto \
		 proto/test.proto
PROTOBUF_SOURCES=$(patsubst %.proto,%.pb-c.c,${PROTOBUF})
PROTOBUF_HEADERS=$(patsubst %.proto,%.pb-c.h,${PROTOBUF})
PROTOBUF_OBJECTS=$(patsubst %.proto,%.pb-c.o,${PROTOBUF})

LIBRARY_SOURCES=lib/cfg.c \
				lib/common.c \
				lib/channel.c \
				lib/keys.c \
				lib/log.c \
				lib/proto.c \
				lib/server.c \
				lib/service.c \
				lib/service/capabilities.c \
				lib/service/exec.c \
				lib/service/invoke.c \
				lib/service/x2x.c \
				lib/service/xpra.c
LIBRARY_HEADERS=$(patsubst %.c,%.h,${LIBRARY_SOURCES})
LIBRARY_OBJECTS=$(patsubst %.c,%.o,${LIBRARY_SOURCES})

EXECUTABLES=sd-discover \
			sd-discover-responder \
			sd-genkey \
			sd-connect \
			sd-server
EXECUTABLES_OBJECTS=$(patsubst %,%.o,${EXECUTABLES})
EXECUTABLES_LIBS=libsodium libprotobuf-c
EXECUTABLES_CFLAGS=${CFLAGS} -I. $(shell pkg-config --cflags ${EXECUTABLES_LIBS})
EXECUTABLES_LDFLAGS=${LDFLAGS} $(shell pkg-config --libs ${EXECUTABLES_LIBS})

TEST_SOURCES=test/test.c \
			 test/cfg.c \
			 test/channel.c \
			 test/service.c \
			 test/server.c
TEST_OBJECTS=$(patsubst %.c,%.o,${TEST_SOURCES})
TEST_LIBS=cmocka ${EXECUTABLES_LIBS}
TEST_CFLAGS=${CFLAGS} -I. $(shell pkg-config --cflags ${TEST_LIBS})
TEST_LDFLAGS=${LDFLAGS} $(shell pkg-config --libs ${TEST_LIBS})

.SUFFIXES: .proto .pb-c.c .pb-c.h .pb-c.o
.PRECIOUS: %.pb-c.c %.pb-c.h

.PHONY: all clean test

all: ${EXECUTABLES}

clean:
	@echo "Cleaning protobufs..."
	@rm ${PROTOBUF_HEADERS} 2>/dev/null || true
	@rm ${PROTOBUF_SOURCES} 2>/dev/null || true
	@echo "Cleaning objects..."
	@rm ${EXECUTABLES_OBJECTS} 2>/dev/null || true
	@rm ${LIBRARY_OBJECTS} 2>/dev/null || true
	@rm ${PROTOBUF_OBJECTS} 2>/dev/null || true
	@rm ${TEST_OBJECTS} 2>/dev/null || true
	@echo "Cleaning executables..."
	@rm ${EXECUTABLES} 2>/dev/null || true
	@rm sd-test 2>/dev/null || true

$(EXECUTABLES): _CFLAGS=${EXECUTABLES_CFLAGS}
$(EXECUTABLES): _LDFLAGS=${EXECUTABLES_LDFLAGS}
$(EXECUTABLES): %: ${PROTOBUF_OBJECTS} ${LIBRARY_OBJECTS} %.o
	@echo "LD $@"
	@$(CC) ${_CFLAGS} ${_LDFLAGS} -o "$@" $^

test: sd-test
	./sd-test
sd-test: _CFLAGS=${TEST_CFLAGS}
sd-test: _LDFLAGS=${TEST_LDFLAGS}
sd-test: ${PROTOBUF_OBJECTS} ${TEST_OBJECTS} ${LIBRARY_OBJECTS}
	@echo "LD $@"
	@$(CC) ${_CFLAGS} ${_LDFLAGS} -o "$@" $^

%.o: %.c %.h
	@echo "CC $@"
	@$(CC) ${_CFLAGS} ${CPPFLAGS} -c -o "$@" "$<"
%.o: %.c
	@echo "CC $@"
	@$(CC) ${_CFLAGS} ${CPPFLAGS} -c -o "$@" "$<"
%.pb-c.c: %.proto
	@echo "PB $@"
	@protoc-c --c_out . $^
