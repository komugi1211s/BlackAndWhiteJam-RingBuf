
ifeq ($(OS),Windows_NT)
all:
	build.bat

else
all:
	./build.sh

endif
