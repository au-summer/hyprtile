# CXXFLAGS=-shared -fPIC --no-gnu-unique -Wall -g -DWLR_USE_UNSTABLE -std=c++2b -O2
CXXFLAGS=-shared -fPIC --no-gnu-unique -g -std=c++2b
INCLUDES=`pkg-config --cflags pixman-1 libdrm hyprland pangocairo libinput libudev wayland-server xkbcommon`
SRC = $(wildcard src/*.cpp)
TARGET = hyprtile.so

all: plugin 

plugin:
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(SRC) -o $(TARGET)

clean:
	rm ./$(TARGET)
