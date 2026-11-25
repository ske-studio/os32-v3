/* PPC32 contract. References under /home/hight/os32-tmp/ref/:
 * openbsd/sys/arch/powerpc/include/_types.h:54-68 (alignment/integers),
 * openbsd/sys/arch/powerpc/include/endian.h:31 (big endian).
 * docs/ppc/abi.md:30-41 separates integer widths from member alignment;
 * u64 member alignment 8 is the adopted PPC_SEP4_DESIGN.md section 2 contract,
 * verified with the actual build/arch/ppc.mk compiler profile by the probe.
 * BSD's maximum alignment alone does not prove u64 member alignment.
 */
#ifndef ARCH_TYPE_CONTRACT_H
#define ARCH_TYPE_CONTRACT_H
#define ARCH_U64_MEMBER_ALIGN 8
#define ARCH_TYPE_BYTE_ORDER __ORDER_BIG_ENDIAN__
#endif
