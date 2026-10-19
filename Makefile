VELO_TOOLCHAIN ?= ../../velo-toolchain
VELO_SH3_LLVM ?= ../llvm/install
TOOLCHAIN = $(abspath $(VELO_TOOLCHAIN))/cmake/velo-ce.cmake

.PHONY: all card clean

all:
	cmake -S . -B build/ce2-sh3 -DCMAKE_TOOLCHAIN_FILE=$(TOOLCHAIN) -DVELO_CE_VERSION=2 -DVELO_ARCH=sh3 -DVELO_LLVM_ROOT=$(abspath $(VELO_SH3_LLVM))
	cmake --build build/ce2-sh3

card: all
	tools/mkcard.sh build/hlux.img build/ce2-sh3/hlux.exe

clean:
	rm -rf build
