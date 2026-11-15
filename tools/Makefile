# PC preview of the e-paper screens (needs a checkout of Adafruit-GFX-Library)
#   make GFX=/path/to/Adafruit-GFX-Library
GFX ?= ../../Adafruit-GFX-Library
CXX ?= g++
CXXFLAGS = -std=c++17 -O1 -w -DARDUINO=10813 -Istub -I$(GFX)/src -I$(GFX) -I../lib/BaroCore/src -I../include

preview_bin: preview.cpp ../src/display.cpp ../src/*.h
	$(CXX) $(CXXFLAGS) -o $@ preview.cpp ../src/display.cpp $(wildcard $(GFX)/src/Adafruit_GFX.cpp $(GFX)/Adafruit_GFX.cpp)

run: preview_bin
	./preview_bin .
	-python3 -c "import glob;from PIL import Image;[Image.open(f).convert('L').save(f[:-3]+'png') for f in glob.glob('*.pbm')]"

clean:
	rm -f preview_bin *.pbm *.png
