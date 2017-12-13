#!/bin/bash
clang  -nostdinc -isystem /usr/include -I/usr/include/c++/6 -I/usr/lib/llvm-4.0/lib/clang/4.0.0/include/ -I/home/kinekinekine/linux/arch/x86/include -I/home/kinekinekine/linux/arch/x86/include/generated  -I/home/kinekinekine/linux/include -I/home/kinekinekine/linux/arch/x86/include/uapi -I/home/kinekinekine/linux/arch/x86/include/generated/uapi -I/home/kinekinekine/linux/include/uapi -I/home/kinekinekine/linux/include/generated/uapi -include /home/kinekinekine/linux/include/linux/kconfig.h  -I/home/kinekinekine/linux/samples/bpf \
		-I/home/kinekinekine/linux/tools/testing/selftests/bpf/ \
			-D__KERNEL__ -Wno-unused-value -Wno-pointer-sign \
				-D__TARGET_ARCH_x86 -Wno-compare-distinct-pointer-types \
					-Wno-gnu-variable-sized-type-not-at-end \
						-Wno-address-of-packed-member -Wno-tautological-compare \
							-Wno-unknown-warning-option  \
-O2 -emit-llvm -c /home/kinekinekine/lwtunnel/gre.c -o -| llc -march=bpf -filetype=obj -o /home/kinekinekine/lwtunnel/gre.o
