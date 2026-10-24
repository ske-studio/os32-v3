#ifndef KDEBUG_V86_GCAP_DEFS_H
#define KDEBUG_V86_GCAP_DEFS_H

/* INT 18h graphics mode calls and the two observed AL frequency layouts. */
#define V86G_INT18_SET_MODE 0x30U
#define V86G_INT18_GET_MODE 0x31U
#define V86G_INT18_AH_DONE 0x05U
#define V86G_AL_BIT2 0x04U
#define V86G_AL_BIT3 0x08U

#endif
