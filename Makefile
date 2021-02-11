NAME=training-forwarder

$(NAME): main.c
	$(CC) $(CFLAGS) -o $@ $^

.PHONY: clean

clean:
	$(RM) $(NAME)
