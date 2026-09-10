CXX ?= c++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra
LDFLAGS ?=

SERVER_OBJS = server.o exchange.o order_book.o protocol.o net.o
TRADER_OBJS = trader.o client_io.o net.o
MD_OBJS = md_client.o client_io.o protocol.o net.o

.PHONY: all clean

all: exchange_server trader market_data

exchange_server: $(SERVER_OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

trader: $(TRADER_OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

market_data: $(MD_OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -MMD -MP -c -o $@ $<

clean:
	rm -f *.o *.d exchange_server trader market_data

-include $(wildcard *.d)
