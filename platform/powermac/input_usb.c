/* Producer: USB service (EE=0). Consumer: the cooperative foreground. */
#include "input_usb.h"
#include "io.h"

static u8 down[PM_INPUT_SOURCES][128];
static u8 caps;
static u16 keys[PM_INPUT_KEYS];
static unsigned head, tail, count;
static u32 dropped;

static u8 modifiers(void)
{
    u8 state = caps ? PM_INPUT_CAPS : 0;
    for (unsigned i = 0; i < PM_INPUT_SOURCES; i++) {
        if (down[i][0x70]) state |= PM_INPUT_SHIFT;
        if (down[i][0x74]) state |= PM_INPUT_CTRL;
        if (down[i][0x73]) state |= PM_INPUT_GRPH;
    }
    return state;
}
static void enqueue(u8 sc)
{
    int key = pm_input_keymap(sc, modifiers());
    if (key < 0) return;
    if (count == PM_INPUT_KEYS) { dropped++; return; }
    keys[head] = (u16)key;
    head = (head + 1) % PM_INPUT_KEYS;
    count++;
}
void pm_input_usb_key(u8 src, u8 sc, int pressed)
{
    if (src < 1 || src > PM_INPUT_SOURCES || sc >= 128) return;
    u8 *held = &down[src - 1][sc];
    if (!pressed) { *held = 0; return; }
    if (*held) return;
    *held = 1;
    if (sc == 0x71) { caps ^= 1; return; }
    enqueue(sc);
}
void pm_input_usb_repeat(u8 src, u8 sc)
{
    if (src < 1 || src > PM_INPUT_SOURCES || sc >= 128) return;
    if (!down[src - 1][sc]) return;
    enqueue(sc); /* Repeat uses the current modifiers. */
}
void pm_input_usb_release(u8 src)
{
    if (src < 1 || src > PM_INPUT_SOURCES) return;
    for (unsigned sc = 0; sc < 128; sc++) down[src - 1][sc] = 0;
}
int pm_input_usb_take(void)
{
    u32 flags = irq_save();
    int key = -1;
    if (count) {
        key = keys[tail];
        tail = (tail + 1) % PM_INPUT_KEYS;
        count--;
    }
    irq_restore(flags);
    return key;
}
int pm_input_usb_has_key(void)
{
    u32 flags = irq_save();
    int ready = count != 0;
    irq_restore(flags);
    return ready;
}
u32 pm_input_usb_dropped(void)
{
    u32 flags = irq_save();
    u32 value = dropped;
    irq_restore(flags);
    return value;
}
