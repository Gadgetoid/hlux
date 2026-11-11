GRIMACE ?= $(error Set GRIMACE to a grimace checkout)
GRIMACE_LLVM ?= $(error Set GRIMACE_LLVM to an unpacked llvm-builds release)

.PHONY: all card clean

all:
	cmake -S . -B build/ce2-sh3 -DCMAKE_TOOLCHAIN_FILE=$(abspath $(GRIMACE))/cmake/toolchain.cmake -DGRIMACE_ARCH=sh3 -DGRIMACE_CE_VERSION=2 -DGRIMACE_LLVM_ROOT=$(abspath $(GRIMACE_LLVM))
	cmake --build build/ce2-sh3

card: all
	tools/mkcard.sh build/hlux.img build/ce2-sh3/hlux.exe

clean:
	rm -rf build
