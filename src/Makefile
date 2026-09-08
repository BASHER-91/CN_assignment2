CXX ?= c++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra
LDFLAGS ?=

.PHONY: all clean

all: exchange_server trader market_data

exchange_server: server.o exchange.o order_book.o protocol.o net.o
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

trader: trader.o client_io.o protocol.o net.o
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

market_data: md_client.o client_io.o protocol.o net.o
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

clean:
	rm -f *.o exchange_server trader market_data
