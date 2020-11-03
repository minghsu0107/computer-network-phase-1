TARGETS = client server

all: $(TARGETS)

client: client.c
	$(CC) -o $@ $<

server: server.c
	$(CC) -o $@ $<

clean:
	rm -f $(TARGETS)
