all:
	gcc -ansi -Wextra -Wall -Wno-unused -O3 -DNDEBUG -o nenofex nenofex.c main.c stack.c atpg.c queue.c mem.c -L../picosat -lpicosat
std:
	gcc -ansi -g -Wextra -Wall -Wno-unused -o nenofex nenofex.c main.c stack.c atpg.c queue.c mem.c -L../picosat -lpicosat
gprof_ndebug:
	gcc -ansi -g -pg -ftest-coverage -fprofile-arcs -Wextra -Wall -Wno-unused -DNDEBUG -o nenofex nenofex.c main.c stack.c atpg.c queue.c mem.c -L../picosat -lpicosat
opt:
	gcc -ansi -Wextra -Wall -Wno-unused -O3 -o nenofex nenofex.c main.c stack.c atpg.c queue.c mem.c -L../picosat -lpicosat

clean:
	rm -f *.out *.gcda *.gcno *.gcov



