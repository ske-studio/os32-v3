# Existing bootstrap units, compiled by the selected kernel graph.
KSTRING_C_SRC = lib/kstring_c.c
GAS_KERNEL = arch/ppc/entry.S arch/ppc/ofw_note.S

C_ARCH += arch/ppc/video_wc.c
# Compile-only GPU applicability; no PPC display backend is provided.
C_ARCH += drivers/video/geforce/gf_wc.c
