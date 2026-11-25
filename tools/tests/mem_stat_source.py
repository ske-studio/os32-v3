"""Extract the PC-98 mem_stat implementation for ILP32 host fixtures."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def mem_stat_source(source):
    header = (ROOT / 'sys/platform/pc98/platform_mem_stat.h').read_text()
    start = source.index('STATIC_ASSERT(sizeof(MemStat)')
    end = source.index('/* =====', start)
    return header[header.index('static inline void platform_mem_stat_pre'):header.rindex('#endif')] + source[start:end]
