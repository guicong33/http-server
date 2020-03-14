httpd:httpd.o
	gcc -o httpd httpd.o -lpthread 
	rm -f httpd.o

httpd.o:
	gcc -lpthread -c httpd.c

clean:
	rm -f httpd.o
