CXX = g++
CXXFLAGS_ROOT = $(shell root-config --cflags)
ifeq ($(CXXFLAGS_ROOT),)
  $(error Cannot find root-config. Please source thisroot.sh)
endif
CXXFLAGS = -std=c++17 -Wall -pthread -g -O0 $(CXXFLAGS_ROOT)
LDFLAGS = -lROOTNTuple -lROOTNTupleUtil $(shell root-config --libs)
TARGETS = dump_pages dump_column_data

.PHONY: all clean

all: $(TARGETS)

dump_pages: dump_pages.cxx
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

dump_column_data: dump_column_data.cxx
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

clean:
	rm -f $(TARGETS)

