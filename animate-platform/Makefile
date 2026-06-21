
default: animate_server animate_client

libanimate:
	@if [ ! -d $@ ]; then \
		echo "ERROR: libanimate not found" > /dev/stderr; \
		echo "Download and unzip libanimate.zip from P2 resources" > /dev/stderr; \
		false; \
	fi

SERVER_SRC = \
	animate_server.c \
	rpc_handlers.c \
	registry.c \
	session.c

SERVER_OBJ = $(SERVER_SRC:.c=.o)

animate_server: $(SERVER_OBJ) | libanimate
	gcc $(SERVER_OBJ) \
	-I libanimate/include \
	-L libanimate/lib \
	-lanimate \
	-lpthread \
	-o $@ \
	-Werror

animate_client: animate_client.c
	gcc $^ -o $@ -Werror


%.o: %.c shared.h
	gcc -c $< \
	-I libanimate/include \
	-Werror


