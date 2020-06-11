GET=i386-mingw32- #for mac
CXX=$(TARGET)g++
LD=$(TARGET)g++
WINDRES=$(TARGET)windres
CXXFLAGS=-std=c++17 -O3 -DWINVER=0x500 -DWIN32_WINNT=0x500 -DUNICODE -D_UNICODE
LDFLAGS+=-mwindows
OBJECTS=main.o trayicon.o resources.o
ifdef DEBUG
	CXXFLAGS+= -g
	LDFLAGS:=$(filter-out -mwindows, $(LDFLAGS))
endif

all: neo2-llkh.exe

neo2-llkh.exe: $(OBJECTS)
	$(LD) $(LDFLAGS) -o $@ $^

%.o: %.rc
	$(WINDRES) -i $^ -o $@

clean: 
	@rm -f $(OBJECTS) neo-llkh.exe
