/*
 * Copyright (c) 2017 rxi
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>

#include "microtar.h"

typedef struct {
  char name[100];
  char mode[8];
  char owner[8];
  char group[8];
  char size[12];
  char mtime[12];
  char checksum[8];
  char type;
  char linkname[100];
  char _padding[255];
} mtar_raw_header_t;


static unsigned round_up(unsigned n, unsigned incr) {
  return n + (incr - n % incr) % incr;
}


static unsigned checksum(const mtar_raw_header_t* rh) {
  unsigned i;
  unsigned char *p = (unsigned char*) rh;
  unsigned res = 256;
  for (i = 0; i < offsetof(mtar_raw_header_t, checksum); i++) {
    res += p[i];
  }
  for (i = offsetof(mtar_raw_header_t, type); i < sizeof(*rh); i++) {
    res += p[i];
  }
  return res;
}


/* --- OS32 追加 (README.OS32 参照) -------------------------------------- */
/* 元は sscanf(field, "%o", &v)。OS32 のユーザーランドは newlib の sscanf を
 * 一度も使っておらず (freestanding + 独自 VFS)、ustar の数値フィールドは
 * 長さいっぱいだと NUL 終端が無い。フィールド長で必ず止まる octal 読みに置く。 */
static unsigned mtar_octal(const char *p, unsigned len) {
  unsigned v = 0;
  unsigned i;
  for (i = 0; i < len; i++) {
    if (p[i] == ' ') continue;
    if (p[i] < '0' || p[i] > '7') break;
    v = (v << 3) + (unsigned)(p[i] - '0');
  }
  return v;
}

/* 元は strcpy(h->name, rh->name)。ustar の name / linkname は 100 バイト
 * ちょうどだと NUL 終端が無く、同じ 100 バイトの宛先を溢れさせる。
 * 長さで止め、終端できない (= 100 バイトちょうど) ときはエラーにする。 */
static int mtar_copy_field(char *dst, unsigned dstlen,
                           const char *src, unsigned srclen) {
  unsigned i;
  for (i = 0; i + 1 < dstlen && i < srclen; i++) {
    if (src[i] == '\0') break;
    dst[i] = src[i];
  }
  if (i + 1 == dstlen && i < srclen && src[i] != '\0') {
    return -1;
  }
  dst[i] = '\0';
  return 0;
}
/* --- OS32 追加ここまで ------------------------------------------------- */


static int tread(mtar_t *tar, void *data, unsigned size) {
  int err = tar->read(tar, data, size);
  tar->pos += size;
  return err;
}


static int twrite(mtar_t *tar, const void *data, unsigned size) {
  int err = tar->write(tar, data, size);
  tar->pos += size;
  return err;
}


/* 元は 1 バイトずつ twrite していた (`for (i=0;i<n;i++) twrite(tar,&nul,1)`)。
 * OS32 の ext2 は sys_write 1 回につき 10 セクタ前後の固定費があるので
 * (パス解決 + inode の read-modify-write + データブロックの read-modify-write)、
 * 15 バイトのファイル 1 本を束ねるだけでも padding と終端で 1521 回の
 * sys_write = 15000 セクタ以上になり、`tar c` が 15 秒を超えていた (票 S6-P、
 * 診断は docs/archive/settings/TASK_S6P.md)。ブロック単位で書けば呼び出しは
 * 数回で済む。書く中身 (ゼロ) も位置の勘定 (twrite が tar->pos を進める) も
 * 変わらない。
 * nul[] は static — 外部プログラムのスタックは細いので自動変数にしない。 */
static int write_null_bytes(mtar_t *tar, int n) {
  static const char nul[512];       /* const = .rodata、全部 0 */
  int err;
  while (n > 0) {
    int chunk = (n > (int)sizeof(nul)) ? (int)sizeof(nul) : n;
    err = twrite(tar, nul, (unsigned)chunk);
    if (err) {
      return err;
    }
    n -= chunk;
  }
  return MTAR_ESUCCESS;
}


static int raw_to_header(mtar_header_t *h, const mtar_raw_header_t *rh) {
  unsigned chksum1, chksum2;

  /* If the checksum starts with a null byte we assume the record is NULL */
  if (*rh->checksum == '\0') {
    return MTAR_ENULLRECORD;
  }

  /* Build and compare checksum */
  chksum1 = checksum(rh);
  chksum2 = mtar_octal(rh->checksum, sizeof(rh->checksum));
  if (chksum1 != chksum2) {
    return MTAR_EBADCHKSUM;
  }

  /* Load raw header into header */
  h->mode  = mtar_octal(rh->mode, sizeof(rh->mode));
  h->owner = mtar_octal(rh->owner, sizeof(rh->owner));
  h->size  = mtar_octal(rh->size, sizeof(rh->size));
  h->mtime = mtar_octal(rh->mtime, sizeof(rh->mtime));
  h->type = rh->type;
  if (mtar_copy_field(h->name, sizeof(h->name),
                      rh->name, sizeof(rh->name)) != 0) {
    return MTAR_EFAILURE;
  }
  if (mtar_copy_field(h->linkname, sizeof(h->linkname),
                      rh->linkname, sizeof(rh->linkname)) != 0) {
    return MTAR_EFAILURE;
  }

  return MTAR_ESUCCESS;
}


static int header_to_raw(mtar_raw_header_t *rh, const mtar_header_t *h) {
  unsigned chksum;

  /* Load header into raw header */
  memset(rh, 0, sizeof(*rh));
  sprintf(rh->mode, "%o", h->mode);
  sprintf(rh->owner, "%o", h->owner);
  sprintf(rh->size, "%o", h->size);
  sprintf(rh->mtime, "%o", h->mtime);
  rh->type = h->type ? h->type : MTAR_TREG;
  strcpy(rh->name, h->name);
  strcpy(rh->linkname, h->linkname);

  /* Calculate and write checksum */
  chksum = checksum(rh);
  sprintf(rh->checksum, "%06o", chksum);
  rh->checksum[7] = ' ';

  return MTAR_ESUCCESS;
}


const char* mtar_strerror(int err) {
  switch (err) {
    case MTAR_ESUCCESS     : return "success";
    case MTAR_EFAILURE     : return "failure";
    case MTAR_EOPENFAIL    : return "could not open";
    case MTAR_EREADFAIL    : return "could not read";
    case MTAR_EWRITEFAIL   : return "could not write";
    case MTAR_ESEEKFAIL    : return "could not seek";
    case MTAR_EBADCHKSUM   : return "bad checksum";
    case MTAR_ENULLRECORD  : return "null record";
    case MTAR_ENOTFOUND    : return "file not found";
  }
  return "unknown error";
}


#ifndef MTAR_NO_STDIO
/* OS32 (MTAR_NO_STDIO) はここを使わない。fopen/fread は OS32 の VFS に
 * 繋がっていないので、呼び出し側が tar->read/write/seek/close に
 * KAPI (sys_open / sys_read / sys_write / sys_lseek / sys_close) を挿す。 */
static int file_write(mtar_t *tar, const void *data, unsigned size) {
  unsigned res = fwrite(data, 1, size, tar->stream);
  return (res == size) ? MTAR_ESUCCESS : MTAR_EWRITEFAIL;
}

static int file_read(mtar_t *tar, void *data, unsigned size) {
  unsigned res = fread(data, 1, size, tar->stream);
  return (res == size) ? MTAR_ESUCCESS : MTAR_EREADFAIL;
}

static int file_seek(mtar_t *tar, unsigned offset) {
  int res = fseek(tar->stream, offset, SEEK_SET);
  return (res == 0) ? MTAR_ESUCCESS : MTAR_ESEEKFAIL;
}

static int file_close(mtar_t *tar) {
  fclose(tar->stream);
  return MTAR_ESUCCESS;
}


int mtar_open(mtar_t *tar, const char *filename, const char *mode) {
  int err;
  mtar_header_t h;

  /* Init tar struct and functions */
  memset(tar, 0, sizeof(*tar));
  tar->write = file_write;
  tar->read = file_read;
  tar->seek = file_seek;
  tar->close = file_close;

  /* Assure mode is always binary */
  if ( strchr(mode, 'r') ) mode = "rb";
  if ( strchr(mode, 'w') ) mode = "wb";
  if ( strchr(mode, 'a') ) mode = "ab";
  /* Open file */
  tar->stream = fopen(filename, mode);
  if (!tar->stream) {
    return MTAR_EOPENFAIL;
  }
  /* Read first header to check it is valid if mode is `r` */
  if (*mode == 'r') {
    err = mtar_read_header(tar, &h);
    if (err != MTAR_ESUCCESS) {
      mtar_close(tar);
      return err;
    }
  }

  /* Return ok */
  return MTAR_ESUCCESS;
}
#endif /* MTAR_NO_STDIO */


int mtar_close(mtar_t *tar) {
  return tar->close(tar);
}


int mtar_seek(mtar_t *tar, unsigned pos) {
  int err = tar->seek(tar, pos);
  tar->pos = pos;
  return err;
}


int mtar_rewind(mtar_t *tar) {
  tar->remaining_data = 0;
  tar->last_header = 0;
  return mtar_seek(tar, 0);
}


int mtar_next(mtar_t *tar) {
  int err, n;
  mtar_header_t h;
  /* Load header */
  err = mtar_read_header(tar, &h);
  if (err) {
    return err;
  }
  /* Seek to next record */
  n = round_up(h.size, 512) + sizeof(mtar_raw_header_t);
  return mtar_seek(tar, tar->pos + n);
}


int mtar_find(mtar_t *tar, const char *name, mtar_header_t *h) {
  int err;
  mtar_header_t header;
  /* Start at beginning */
  err = mtar_rewind(tar);
  if (err) {
    return err;
  }
  /* Iterate all files until we hit an error or find the file */
  while ( (err = mtar_read_header(tar, &header)) == MTAR_ESUCCESS ) {
    if ( !strcmp(header.name, name) ) {
      if (h) {
        *h = header;
      }
      return MTAR_ESUCCESS;
    }
    mtar_next(tar);
  }
  /* Return error */
  if (err == MTAR_ENULLRECORD) {
    err = MTAR_ENOTFOUND;
  }
  return err;
}


int mtar_read_header(mtar_t *tar, mtar_header_t *h) {
  int err;
  mtar_raw_header_t rh;
  /* Save header position */
  tar->last_header = tar->pos;
  /* Read raw header */
  err = tread(tar, &rh, sizeof(rh));
  if (err) {
    return err;
  }
  /* Seek back to start of header */
  err = mtar_seek(tar, tar->last_header);
  if (err) {
    return err;
  }
  /* Load raw header into header struct and return */
  return raw_to_header(h, &rh);
}


int mtar_read_data(mtar_t *tar, void *ptr, unsigned size) {
  int err;
  /* If we have no remaining data then this is the first read, we get the size,
   * set the remaining data and seek to the beginning of the data */
  if (tar->remaining_data == 0) {
    mtar_header_t h;
    /* Read header */
    err = mtar_read_header(tar, &h);
    if (err) {
      return err;
    }
    /* Seek past header and init remaining data */
    err = mtar_seek(tar, tar->pos + sizeof(mtar_raw_header_t));
    if (err) {
      return err;
    }
    tar->remaining_data = h.size;
  }
  /* Read data */
  err = tread(tar, ptr, size);
  if (err) {
    return err;
  }
  tar->remaining_data -= size;
  /* If there is no remaining data we've finished reading and seek back to the
   * header */
  if (tar->remaining_data == 0) {
    return mtar_seek(tar, tar->last_header);
  }
  return MTAR_ESUCCESS;
}


int mtar_write_header(mtar_t *tar, const mtar_header_t *h) {
  mtar_raw_header_t rh;
  /* Build raw header and write */
  header_to_raw(&rh, h);
  tar->remaining_data = h->size;
  return twrite(tar, &rh, sizeof(rh));
}


int mtar_write_file_header(mtar_t *tar, const char *name, unsigned size) {
  mtar_header_t h;
  /* Build header */
  memset(&h, 0, sizeof(h));
  /* OS32: 元は strcpy。100 バイトに収まらない名前で h.name を溢れさせる。 */
  if (mtar_copy_field(h.name, sizeof(h.name), name, (unsigned)strlen(name) + 1) != 0) {
    return MTAR_EFAILURE;
  }
  h.size = size;
  h.type = MTAR_TREG;
  h.mode = 0664;
  /* Write header */
  return mtar_write_header(tar, &h);
}


int mtar_write_dir_header(mtar_t *tar, const char *name) {
  mtar_header_t h;
  /* Build header */
  memset(&h, 0, sizeof(h));
  /* OS32: 元は strcpy (上と同じ理由)。 */
  if (mtar_copy_field(h.name, sizeof(h.name), name, (unsigned)strlen(name) + 1) != 0) {
    return MTAR_EFAILURE;
  }
  h.type = MTAR_TDIR;
  h.mode = 0775;
  /* Write header */
  return mtar_write_header(tar, &h);
}


int mtar_write_data(mtar_t *tar, const void *data, unsigned size) {
  int err;
  /* Write data */
  err = twrite(tar, data, size);
  if (err) {
    return err;
  }
  tar->remaining_data -= size;
  /* Write padding if we've written all the data for this file */
  if (tar->remaining_data == 0) {
    return write_null_bytes(tar, round_up(tar->pos, 512) - tar->pos);
  }
  return MTAR_ESUCCESS;
}


int mtar_finalize(mtar_t *tar) {
  /* Write two NULL records */
  return write_null_bytes(tar, sizeof(mtar_raw_header_t) * 2);
}
