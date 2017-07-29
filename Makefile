default: reposync2

misc.o: misc.c misc.h
	gcc -g -std=c99 -Wall -I/usr/include/libxml2 -c misc.c

reposync2: reposync2.c misc.o
	gcc -g -std=c99 -Wall -o reposync2 -I/usr/include/libxml2 -lcrypto -lxml2 -lz -lm -ldl -lcurl reposync2.c misc.o

clean:
	"rm" -f *.o reposync2

run: reposync2
	rm -rf DEST
	tar xvzf DEST.tgz
	./reposync2 -s 'file:///home/frank/REPOSYNC/c/SOURCE' -d '/home/frank/REPOSYNC/c/DEST' # -n

epel: reposync2
	./reposync2 -s http://mirrorservice.org/sites/dl.fedoraproject.org/pub/epel/7/x86_64/ -d /vol1/Linux/dist/epel/7/x86_64 -k -l 2 

epel_python:
	reposync --config epel.repo  --repoid epel --newest-only --download_path=/vol1/Linux/dist/epel/7/x86_64 --norepopath 
