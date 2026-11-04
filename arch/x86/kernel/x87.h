#ifndef OS32_X87_H
#define OS32_X87_H

#define X87_STATE_BYTES 108
typedef struct { unsigned char bytes[X87_STATE_BYTES]; } X87State;

void x87_probe(void);
void x87_save(X87State *state);
void x87_restore(const X87State *state);
void x87_init(void);

#endif
