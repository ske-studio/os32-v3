#ifndef OS32_IN2_H
#define OS32_IN2_H

#define IN2_MAX_PADS 4
#define IN2_BTN_DPAD_UP 0x1
#define IN2_BTN_DPAD_DOWN 0x2
#define IN2_BTN_DPAD_LEFT 0x4
#define IN2_BTN_DPAD_RIGHT 0x8
#define IN2_BTN_START 0x10
#define IN2_BTN_BACK 0x20
#define IN2_BTN_LTHUMB 0x40
#define IN2_BTN_RTHUMB 0x80
#define IN2_BTN_LSHOULDER 0x100
#define IN2_BTN_RSHOULDER 0x200
#define IN2_BTN_A 0x1000
#define IN2_BTN_B 0x2000
#define IN2_BTN_X 0x4000
#define IN2_BTN_Y 0x8000
#define IN2_DEV_KBD 1
#define IN2_DEV_MOUSE 2
#define IN2_DEV_PAD 3
#define IN2_EV_KEY 1
#define IN2_EV_MOVE 2
#define IN2_EV_BUTTON 3
#define IN2_EV_WHEEL 4
#define IN2_EV_PADBTN 5
#define IN2_EV_ABS 6
#define IN2_EV_LOST 7
#define IN2_LOST_OVERFLOW 1
#define IN2_LOST_FOCUS 2
#define IN2_LOST_EXCL 3
#define IN2_EXCL_KBD 1
#define IN2_EXCL_MOUSE 2
#define IN2_EVF_SEAMLESS 1

typedef struct { unsigned short buttons; unsigned char lt, rt; signed short lx, ly, rx, ry; } IN2_Gamepad;
typedef struct { unsigned long packet; IN2_Gamepad pad; unsigned long time_ms; } IN2_PadState;
typedef struct { unsigned short left, right; } IN2_Vibration;
typedef struct { unsigned long size; unsigned char type, subtype; unsigned short flags; IN2_Gamepad caps; IN2_Vibration vib;
                 unsigned short vid, pid; unsigned char source, map_src; unsigned short rsv; char name[32]; } IN2_PadCaps;
typedef struct { unsigned long size; unsigned char dev, kind, source, flags; unsigned short vid, pid; char name[20]; } IN2_DevInfo;
typedef struct { unsigned char sc, down, mods, rsv; } IN2_EventKey;
typedef struct { signed short dx, dy; } IN2_EventMove;
typedef struct { unsigned char down, changed; signed short rsv; } IN2_EventBtn;
typedef struct { signed short delta, rsv; } IN2_EventWheel;
typedef struct { unsigned short buttons, changed; } IN2_EventPad;
typedef struct { unsigned short x, y; } IN2_EventAbs;
typedef struct { unsigned short reason, count; } IN2_EventLost;
typedef union {
    IN2_EventKey key;
    IN2_EventMove move;
    IN2_EventBtn btn;
    IN2_EventWheel wheel;
    IN2_EventPad pad;
    IN2_EventAbs abs;
    IN2_EventLost lost;
} IN2_EventData;

typedef struct {
    unsigned long time_ms; unsigned short seq; unsigned char dev, type;
    IN2_EventData u;
    unsigned short flags, rsv;
} IN2_Event;
#define IN2_STATUS_ACQUIRE_GEN 0 /* rsv index, last raw-stream acquisition (WM only) */
typedef struct { unsigned long size, acquired, focus_owner, focus_gen, dropped, rsv[3]; } IN2_Status;
typedef struct { signed long dx, dy; unsigned long buttons; signed long wheel; } IN2_MouseState;
typedef struct { unsigned long size, gen, excl, owner; } IN2_WmState;

#endif
