/* The flag belongs to the HID producer. A printable later key replaces
 * its target; a nonrepeating key leaves the first deadline intact. */
static void send_modifiers(unsigned i, u8 modifiers)
{
    u8 bytes[8] = {modifiers, 0};
    struct usb_xfer x = {.actual = 8, .ctx = &keyboards[i]};
    x.buf.va = bytes;
    kbd_done(&x);
}

static int metadata(void)
{
    KmEntry entry = {.group1 = {0xf500, 0xf500}, .group2 = {0xf500, 'x'}};
    u8 compat = 12, bits = 0;
    KmBackend map = {.entries = &entry, .compat_sc = &compat, .norepeat = &bits};
    if (!km_hid_repeatable(&map, 0)) return 90;
    bits = 1;
    if (km_hid_repeatable(&map, 0)) return 91;
    bits = 0;
    compat = 128;
    if (km_hid_repeatable(&map, 0)) return 92;
    compat = 127;
    entry.group2[1] = 0xf500;
    return km_hid_repeatable(&map, 0) ? 93 : 0;
}

static int check_edges(void)
{
    int rc = metadata();
    if (rc) return rc;
    unsigned i = 0;
    report(i, 4, 0);
    if (take() != 'a') return 44;
    u32 first = milliseconds + 500;
    report(i, 4, 0x53); /* HID Num Lock suppresses repeat. */
    tick(i, first - 1);
    idle_work_run();
    if (take() != -1) return 45;
    tick(i, first);
    idle_work_run();
    if (take() != 'a') return 46;
    report(i, 4, 5);
    if (take() != 'b') return 47;
    tick(i, milliseconds + 30);
    idle_work_run();
    if (take() != -1) return 48;
    tick(i, milliseconds + 470);
    idle_work_run();
    if (take() != 'b') return 49;

    tick(i, milliseconds + 30);
    send_modifiers(1, 2); /* Shift on another keyboard. */
    send_modifiers(1, 0);
    idle_work_run();
    if (take() != -1) return 50;
    send_modifiers(1, 2);
    tick(i, milliseconds + 30);
    if (!usb_input_release(USBP_INPUT_KBD, interfaces[1].src)) return 51;
    idle_work_run();
    if (take() != -1 || usb_input_alloc(USBP_INPUT_KBD) != interfaces[1].src)
        return 52;
    tick(i, milliseconds + 30);
    idle_work_run();
    if (take() != 'b') return 53;

    /* A break on the same keyboard for a different usage preserves B. */
    tick(i, milliseconds + 30);
    report(i, 5, 0);
    idle_work_run();
    if (take() != 'b') return 54;
    tick(i, milliseconds + 30);
    report(i, 0, 0);
    report(i, 5, 0);
    idle_work_run();
    if (take() != 'b' || take() != -1) return 55;

    /* A stale slot still holds a physically down key. Delivery must validate
     * its binding at consumption even if the same source ID remains held. */
    tick(i, milliseconds + 500);
    km_source_unbind(interfaces[i].src, 1);
    idle_work_run();
    if (take() != -1 || km_source_bind(interfaces[i].src, 1, KM_MAP_HID)) return 56;
    report(i, 0, 0);
    return 0;
}

#ifndef __PPC__
static void shift_a_map(const KmBackend *map, u16 code, KmPolicyKey *out)
{
    if (map->id != KM_MAP_PC98 || code != KEY_SHIFT) return;
    for (unsigned i = 0; i < 8; ++i)
        out->bank[0][i] = (KmExactCell){0xf500, KM_BSD_SPECIAL, 0x9b};
}

static int native_key(u8 src, u16 generation, u8 code, int down)
{
    KmKey key = {.src = src, .generation = generation, .keycode = code, .down = down};
    return input_capture_key(&key);
}

static int check_native(void)
{
    u16 gen;
    int src = input_src_register(INPUT_KIND_KBD, INPUT_ORIGIN_NATIVE, &gen);
    if (src < 0 || km_backend_register(&pc98_jp_backend) ||
        km_source_bind(src, gen, KM_MAP_PC98)) return 61;
    km_policy_read_install(shift_a_map);
    KmState state;
    tick(2, milliseconds + 30);
    if (native_key(src, gen, KEY_SHIFT, 1)) return 62;
    idle_work_run();
    if (native_key(src, gen, KEY_SHIFT, 0)) return 63;
    km_state_snapshot(&state);
    if (!(state.locks & KM_LOCK_ALT) || take() != -1) return 64;
    if (native_key(src, gen, KEY_SHIFT, 1)) return 65;
    tick(2, milliseconds + 30); /* Cancel the tap immediately, before break. */
    if (native_key(src, gen, KEY_SHIFT, 0)) return 66;
    idle_work_run();
    km_state_snapshot(&state);
    if (!(state.locks & KM_LOCK_ALT) || take() != -1) return 67;
    /* Switch the native lock back before testing cross-source holds. */
    native_key(src, gen, KEY_SHIFT, 1);
    native_key(src, gen, KEY_SHIFT, 0);
    km_policy_read_install(0);
    tick(2, milliseconds + 30);
    u8 sc = hid_kbd_sc(6);
    u32 old = input_key_release_gen(sc);
    if (native_key(src, gen, sc, 1) || take() != 'c') return 68;
    idle_work_run();
    if (take() != -1 || native_key(src, gen, sc, 0) ||
        !input_key_pressed(sc) || input_key_release_gen(sc) != old) return 69;
    report(2, 0, 0);
    if (input_key_pressed(sc) || input_key_release_gen(sc) != old + 1) return 70;
    report(2, 6, 0);
    if (take() != 'c') return 71;
    tick(2, milliseconds + 500);
    idle_work_run();
    if (take() != 'c') return 72;
    return 0;
}
#endif
