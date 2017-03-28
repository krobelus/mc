CC	= gcc
# CC	= clang -fsanitize=address
# CC	= clang -fsanitize=thread
# CC	= clang -fsanitize=undefined
LEX	= flex
YACC	= bison
LIBS	= -lfl
CFLAGS += -O3
# CFLAGS += -ggdb
# CFLAGS += -DNDEBUG
CFLAGS += -Wall
CFLAGS += -std=c11
CFLAGS += -pthread

PARSERCFLAGS := -Wno-unused-function -Wno-implicit-function-declaration -Wno-int-conversion

NAME 	= mc
.PHONY: $(NAME)
$(NAME): $(NAME).c formula_parser.o formula_scanner.o
	clang-format -i *.[ch] || (echo '    ^ dont mind me')
	$(CC) $(CFLAGS) $(LIBS) -o $@ $^

%.c:	%.l
	$(LEX) -o$@ $^

%.c:	%.y
	$(YACC) -v -d -o $@ $^

%.o:	%.c
	$(CC) $(CFLAGS) $(PARSERCFLAGS) -c $< -o $@

clean:
	rm -f *.o *.output formula_scanner.c formula_parser.c formula_parser.h $(NAME)
