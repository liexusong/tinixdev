BIN = hex
$(BIN):hex.c
	gcc $< -I /usr/include/ncurses -L /usr/lib -lcurses -o  $(BIN)
.PHONY:clean
clean:
	-rm -f $(BIN)
