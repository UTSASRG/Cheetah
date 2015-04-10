all:
	g++ -shared -Wall -Werror -fpic -o libmy.so libtest.cpp 
	g++ -L./ -o test test.cpp -lmy

clean:
	rm libtest.o libtest.so test
