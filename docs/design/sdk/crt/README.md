# sdk/crt/ — 設計
> 状態: **現行 (2026-10-31)** — 出所: MEMMAP_V3 (D1 で移した)
ソース: `sdk/crt/`

### syscalls

シェルの 2 ヒープは API 2・供給元 1

libc の `_sbrk` (`sdk/crt/syscalls.c:162`、今は `kapi->sbrk_heap_limit` を見て断るだけ) が**ヒープ末尾に続けて `mem_map`** し、

> [要確認] sdk/crt/syscalls.c の _sbrk は現在 os32_nano_crt_sbrk へ転送する。伸長と mem_map は sdk/allocator/nano_adapter.c の grow_exact が所有し、「今は…断るだけ」は現行と異なる。
