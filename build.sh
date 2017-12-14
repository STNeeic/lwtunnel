#!/bin/bash
linux="/home/sekine/linux/"
clangversion=`clang --version | sed -n '1 p' | sed -e 's/clang version //' | sed -e 's/-.*//'`
clangver=`echo $clangversion | sed -e 's/\..//2'`
clang  -nostdinc -isystem /usr/include -I/usr/include/c++/5 -I/usr/lib/llvm-${clangver}/lib/clang/${clangversion}/include/ -I${linux}/arch/x86/include -I${linux}/arch/x86/include/generated  -I${linux}/include -I${linux}/arch/x86/include/uapi -I${linux}/arch/x86/include/generated/uapi -I${linux}/include/uapi -I${linux}/include/generated/uapi -include ${linux}/include/linux/kconfig.h  -I${linux}/samples/bpf \
		-I${linux}/tools/testing/selftests/bpf/ \
			-D__KERNEL__ -Wno-unused-value -Wno-pointer-sign \
				-D__TARGET_ARCH_x86 -Wno-compare-distinct-pointer-types \
					-Wno-gnu-variable-sized-type-not-at-end \
						-Wno-address-of-packed-member -Wno-tautological-compare \
							-Wno-unknown-warning-option  \
-O2 -emit-llvm -c gre.c -o -| llc -march=bpf -filetype=obj -o gre.o
