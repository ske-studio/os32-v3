/* JIS109 positions from the existing HID -> PC-98 scan contract.
 * This is a platform cooked-key map, independent of the PC-98 input driver. */
#include "input_usb.h"

static const char letters[128] = {
    [0x1d]='a', [0x2d]='b', [0x2b]='c', [0x1f]='d', [0x12]='e',
    [0x20]='f', [0x21]='g', [0x22]='h', [0x17]='i', [0x23]='j',
    [0x24]='k', [0x25]='l', [0x2f]='m', [0x2e]='n', [0x18]='o',
    [0x19]='p', [0x10]='q', [0x13]='r', [0x1e]='s', [0x14]='t',
    [0x16]='u', [0x2c]='v', [0x11]='w', [0x2a]='x', [0x15]='y',
    [0x29]='z',
};
static const char plain[128] = {
    [0x01]='1', [0x02]='2', [0x03]='3', [0x04]='4', [0x05]='5',
    [0x06]='6', [0x07]='7', [0x08]='8', [0x09]='9', [0x0a]='0',
    [0x0b]='-', [0x0c]='^', [0x0d]='\\', [0x1a]='@', [0x1b]='[',
    [0x26]=';', [0x27]=':', [0x28]=']', [0x30]=',', [0x31]='.',
    [0x32]='/', [0x33]='\\',
};
static const char shifted[128] = {
    [0x01]='!', [0x02]='"', [0x03]='#', [0x04]='$', [0x05]='%',
    [0x06]='&', [0x07]='\'', [0x08]='(', [0x09]=')',
    [0x0b]='=', [0x0c]='~', [0x0d]='|', [0x1a]='`', [0x1b]='{',
    [0x26]='+', [0x27]='*', [0x28]='}', [0x30]='<', [0x31]='>',
    [0x32]='?', [0x33]='_',
};
int pm_input_keymap(u8 sc, u8 state)
{
    int c;
    if (sc >= 128) return -1;
    c = letters[sc];
    if (c) {
        if (state & PM_INPUT_CTRL) return c == 'c' ? 27 : c - 'a' + 1;
        if (!!(state & PM_INPUT_SHIFT) ^ !!(state & PM_INPUT_CAPS)) c -= 'a' - 'A';
        return c;
    }
    c = (state & PM_INPUT_SHIFT) ? shifted[sc] : plain[sc];
    if (c) return c;
    switch (sc) {
    case 0x00: return 27;
    case 0x0e: return 8;
    case 0x0f: return 9;
    case 0x1c: return 13;
    case 0x34: return 32;
    case 0x39: case 0x3a: case 0x3b: case 0x3c: case 0x3d: case 0x3e:
        return sc << 8;
    case 0x40: return '-';
    case 0x41: return '/';
    case 0x42: return '7';
    case 0x43: return '8';
    case 0x44: return '9';
    case 0x45: return '*';
    case 0x46: return '4';
    case 0x47: return '5';
    case 0x48: return '6';
    case 0x49: return '+';
    case 0x4a: return '1';
    case 0x4b: return '2';
    case 0x4c: return '3';
    case 0x4d: return '=';
    case 0x4e: return '0';
    case 0x4f: return ',';
    case 0x50: return '.';
    default: return -1;
    }
}
