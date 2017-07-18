Reactor.o : TypeDef.h Socket.h Reactor.cpp
	g++ -c TypeDef.h Socket.h Reactor.cpp

clean:
	@rm -f *.o
