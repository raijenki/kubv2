CPP = g++
OPTFLAGS= -O2 -ffast-math 

sputniPIC: sputniPIC.cpp ConfigFile.o 
	${CPP} ${OPTFLAGS} -o sputniPIC	sputniPIC.cpp ConfigFile.o 

sputniPIC.o: sputniPIC.cpp
	${CPP} ${OPTFLAGS} -c sputniPIC.cpp

ConfigFile.o: ConfigFile.cpp
	${CPP} ${OPTFLAGS} -c ConfigFile.cpp

clean:
	rm -rf *.o sputniPIC
