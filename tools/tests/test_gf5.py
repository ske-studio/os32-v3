"""Layer 5 component hosts and the existing runner trace acceptance suite."""
CHECK = dict(name='gf5-host', tier=1, needs=['make:all'], budget=120.0,
             covers=[
                 'drivers/base/dev.c',
                 'fs/vfs.c',
                 'hwdev/gf_load_state.c',
                 'kdebug/gf_display_test.c',
                 'kdebug/gf_stage_state.c',
                 'kdebug/gfinit_bus.c',
                 'kdebug/gfinit_mode.c',
                 'kdebug/gfinit_plan.c',
                 'kdebug/gfinit_plan_c.c',
                 'kdebug/gfinit_resources.c',
                 'kdebug/kdebug_window.c',
                 'kdebug/recover_profile.c',
                 'kernel/module/module_image.c',
                 'kernel/module/module_runtime.c',
                 'tools/g16/gen_preload16.py',
                 'tools/gen_g16.py',
                 'tools/tests/gf_accel_c1_host.c',
                 'tools/tests/gf_accel_unbound_stubs.c',
                 'tools/tests/gf_display_host.c',
                 'tools/tests/gfinit_bus_host.c',
                 'tools/tests/gfinit_c_only_host.c',
                 'tools/tests/gfinit_init_host.c',
                 'tools/tests/gfinit_mode_host.c',
                 'tools/tests/gfinit_plan_c_host.c',
                 'tools/tests/gfinit_plan_host.c',
                 'tools/tests/gfinit_s0_host.c',
                 'tools/tests/module_image_host.c',
                 'tools/tests/module_runtime_host.c',
                 'tools/tests/test_gfinit_mode.py',
                 'tools/tests/test_gfinit_plan.py',
                 'tools/tests/test_gfinit_plan_c.py',
                 'tools/tests/test_gfinit_stages.py',
                 'tools/tests/test_module_image.py',
                 'tools/tests/test_module_runtime.py',
                 'tools/tests/gf_guard_fake.h',
                 'hwdev/gf/**',
                 'kdebug/gf5_test.c',
                 'kdebug/gfinit_pll.*',
                 'kdebug/gfinit_decode*',
                 'kdebug/gfinit_exec.c',
                 'kdebug/recover.c',
                 'kdebug/recover_gf.c',
                 'tools/tests/gf5*',
                 'tools/tests/gf_startlog_host.h',
                 'include/kprintf.h',
                 'tools/tests/test_gf5.py',
                 'tools/tests/test_gf_runner.py',
                 'tools/tests/gf_runner*',
                 'tools/tests/gf4_c2linit_host.c',
                 'tools/tests/gfinit_exec_host.c',
                 'tools/tests/gfinit_host.c',
                 'tools/tests/recover_gf_host.c',
                 'tools/tests/kernel_includes.py',
                 'tools/tests/host32.py',
                 'tools/tests/test_gf_display.py',
                 'tools/tests/gf4*',
                 'tools/tests/serial_diag_host.h',
                 'tools/tests/host_arch/*',
                 'tools/tests/os32api_host.py',
                 'tools/tests/g16_fixture.py',
                 'drivers/video/geforce/*',
                 'exec/*',
                 'gfx/*',
                 'kernel/*',
                 'drivers/input/kbd_fgpoll.inc',
                 'include/**',
                 'lib/**',
                 'platform/pc98/**',
                 'mm/*.h',
                 'fs/*.h',
                 'arch/x86/*.h',
                 'arch/x86/kernel/*.h',
                 'tools/prototypes/geforce_imports.def',
                 'userland/gshell/src/**',
                 'userland/gshell/host/**',
                 'userland/gshell/build.rs',
                 'userland/lib/**',
                 'userland/gshell/Cargo.toml',
                 'userland/gshell/Cargo.lock',
                 'sdk/**',
                 'assets/fonts/**',
                 'tools/fetch_fonts.py',
                 'tools/tests/mutpar.py',
                 'tools/tests/gf4_display_fake.h',
                 'tools/tests/gf_accel_host.h',
                 'include/types.h',
             ])

from pathlib import Path
import argparse
import re
import shutil
import subprocess
import sys
import tempfile
import host32
import mutpar
from kernel_includes import include_flags

ROOT = Path(__file__).resolve().parents[2]
# Each host defines GF5_HOST_UNIT before including gf5_fake.h.
CASES = {
    'perf': (0, ['hwdev/gf/perf.c']),
    'clock': (1, ['hwdev/gf/clock.c', 'kdebug/gfinit_pll.c']),
    'clocksave': (6, ['hwdev/gf/clock.c', 'hwdev/gf/state.c', 'kdebug/gfinit_pll.c']),
    'ramclock': (2, ['hwdev/gf/ramclock.c']),
    'therm': (3, ['hwdev/gf/therm.c']),
    'volt': (4, ['hwdev/gf/volt.c']),
    'tile': (6, ['hwdev/gf/tile.c', 'hwdev/gf/state.c', 'hwdev/gf/vram_view.c']),
    'state': (6, ['hwdev/gf/state.c']),
    'rom': (8, ['hwdev/gf/feature5_rom.c']),
    'links': (9, ['hwdev/gf/feature5_links.c']),
    **{name: (7, []) for name in
       ('foreground', 'foreground-reboot', 'foreground-gpio',
        'foreground-observe', 'foreground-gui')},
}
# Per-stage report*.md proposals, latest replacement per name wins.
# Tuple: name, product path, unique old text, new text, CASES key.
# B1/B2 binary names and B4 function observations are normalized to CASES.
# Every anchor must be unique; compile failures never count as runtime RED.
MUTATIONS = [
 ('inactive-heat-consume', 'drivers/video/geforce/geforce_module.c',
  'if (heat_bridge.active && !heat_bridge.active()) return;',
  'if (false) return;', 'foreground'),
 ('sensor-reentry-rearm', 'drivers/video/geforce/backend_geforce.c',
  'rc=gf5_therm_monitor_restart(feature5);',
  'rc=0;', 'foreground'),
 ('sensor-reentry-poll', 'hwdev/gf/therm.c',
  'g->fan.sensor_poll=true;',
  'g->fan.sensor_poll=false;', 'foreground'),
 ('sensor-reentry-deadline', 'hwdev/gf/therm.c',
  'g->fan.sensor_poll=true;\n        g->next_sensor=0;',
  'g->fan.sensor_poll=true;\n        g->next_sensor=~(u64)0;', 'foreground'),
 ('tile-pitch-fixed-point', 'hwdev/gf/tile.c',
  'return pitch && !(pitch&(a-1U));', 'return pitch!=0;', 'tile'),
 ('tile-release-borrow', 'hwdev/gf/vram_view.c',
  'if (key_matches(&b->key, v)) return GF3_EBUSY;',
  'if (0 && key_matches(&b->key, v)) return GF3_EBUSY;', 'tile'),
 ('tile-fault-new-loan', 'hwdev/gf/tile.c',
  'g->tile_slot[slot].state=GF_TILE_UNKNOWN;',
  'tile_put(g,&g->tile_slot[slot].loan[g->tile_slot[slot].current^1U]); g->tile_slot[slot].state=GF_TILE_UNKNOWN;', 'tile'),
 ('tile-init-last-program', 'hwdev/gf/tile.c',
  'i32 rc=tile_fb_program(g,i,0,0,0);',
  'i32 rc=i+1U==GF5_TILE_COUNT ? 0 : tile_fb_program(g,i,0,0,0);', 'tile'),
 ('tile-retire-early-free', 'hwdev/gf/tile.c',
  'i32 rc=tile_program(g,i,0,0,0);',
  'i32 rc=tile_program(g,i,0,0,0); if (rc) (void)tile_free(g,i);', 'tile'),
 ('tile-state-unknown-save', 'hwdev/gf/state.c',
  'if ((s->state!=GF_TILE_FREE && s->state!=GF_TILE_OWNED) ||\n'
  '            g->tile_live[i]!=(s->state==GF_TILE_OWNED))',
  'if (s->state==GF_TILE_FREE && false)', 'tile'),
 ('tile-state-save-raw', 'hwdev/gf/state.c',
  's->state,s->raw_addr,s->raw_limit,s->raw_pitch};',
  's->state,0,s->raw_limit,s->raw_pitch};', 'tile'),
 ('tile-state-restart-reset', 'hwdev/gf/state.c',
  'if (!rc) rc=g->links.restart(g->links.owner,g->deadline);',
  'if (!rc) { rc=g->links.restart(g->links.owner,g->deadline); g->tile_slot[0]=(struct gf5_tile_slot){0}; }', 'tile'),
 ('tile-state-raw-match', 'hwdev/gf/state.c',
  's->raw_limit!=saved->raw_limit || s->raw_pitch!=saved->raw_pitch ||',
  's->raw_limit!=saved->raw_limit || false ||', 'tile'),
 ('tile-recover-transfer', 'hwdev/gf/gr.c',
  'wr(f, off, tiles[i].addr)', 'wr(f, off, 0)', 'tile'),

 ('clock-zero-is-absent', 'hwdev/gf/clock.c',
  'return result(g,0); /* Zero is an absent source, nv40.c:82-115. */',
  'return result(g,c.core_khz && c.shader_khz && c.mem_khz ? 0 : GF5_ESTATE);',
  'clocksave'),
 ('therm-toggle-step-error', 'hwdev/gf/therm.c',
  'return th_toggle(g,percent,now);',
  'i32 toggle_rc=th_toggle(g,percent,now); return rc ? rc : toggle_rc;', 'therm'),
 ('therm-resume-temperature-error', 'hwdev/gf/therm.c',
  'rc=th_mode(g,mode,now);\n        if (rc) { th_fan_off(g,rc); rc=0; g->source_rc=0; }',
  'rc=th_mode(g,mode,now);\n        if (rc) { th_fan_off(g,rc); return th_exit(g,rc); }', 'therm'),
 ('therm-boost-after-write-failure', 'hwdev/gf/therm.c',
  'if (boost_rising && f->mode!=GF5_FAN_AUTO) {',
  'if (!fan_rc && boost_rising && f->mode!=GF5_FAN_AUTO) {', 'therm'),
 ('therm-fanless-mode', 'hwdev/gf/therm.c',
  '/* base.c:181-208: only temperature gates mode selection.',
  'if (rc) return rc; /* base.c:181-208: only temperature gates mode selection.', 'therm'),
 ('therm-toggle-error-rearm', 'hwdev/gf/therm.c',
  'if (percent!=(on ? TH_PERCENT : 0)) {',
  'if (!first && percent!=(on ? TH_PERCENT : 0)) {', 'therm'),
 ('therm-resume-write-error', 'hwdev/gf/therm.c',
  '/* base.c:383-386: restore mode even after a failed duty write. */\n            if (rc) { th_fan_off(g,rc); rc=0; g->source_rc=0; }',
  'if (rc) { th_fan_off(g,rc); return th_exit(g,rc); }', 'therm'),
 ('therm-fan-error-keeps-auto', 'hwdev/gf/therm.c',
  'g->fan_off=true;', 'g->fan_off=true; g->fan.mode=GF5_FAN_NONE;', 'therm'),
 ('therm-fan-error-retry-prepare', 'hwdev/gf/therm.c',
  'if (f->gpio_line!=U8_MAX) return f->method ? 0 : GF5_ENOTSUP;',
  'if (g->fan_off) return GF5_ENOTSUP;\n    if (f->gpio_line!=U8_MAX) return f->method ? 0 : GF5_ENOTSUP;', 'therm'),
 ('therm-fan-error-emergency', 'hwdev/gf/therm.c',
  'i32 heat_rc=gf5_heat_step(g,&t,now);',
  'i32 heat_rc=g->fan_off ? 0 : gf5_heat_step(g,&t,now);', 'therm'),
 ('therm-boost-mode-early-return', 'hwdev/gf/therm.c',
  'i32 mode_rc=th_mode(g,GF5_FAN_AUTO,now);',
  'i32 mode_rc=th_mode(g,GF5_FAN_AUTO,now); if (mode_rc) return mode_rc;', 'therm'),
 ('therm-fanless-resume', 'hwdev/gf/therm.c',
  'if (rc==GF5_ENOTSUP) { rc=0; }',
  'if (rc==GF5_ENOTSUP && false) { rc=0; }', 'therm'),
 ('therm-fanless-rearm', 'hwdev/gf/therm.c',
  'else if (active && t.milli_c<=cool) active=false;',
  'else if (active && !g->fan_off && t.milli_c<=cool) active=false;', 'therm'),
 ('links-restore-old-deadline', 'hwdev/gf/feature5_links.c',
  'i32 rc=gf4_stage_deadline(g->display,g->ch ? g->ch->f : 0,&g->deadline);',
  'i32 rc=0;', 'links'),
 ('links-performance-old-deadline', 'hwdev/gf/feature5_links.c',
  'if (!rc) rc=gf4_stage_deadline(g->display,g->ch ? g->ch->f : 0,&g->deadline);',
  'if (!rc) rc=0;', 'links'),('perf-mem-scale',
  'hwdev/gf/perf.c',
  'PERF_NV4_MEMORY_SCALE=2000',
  'PERF_NV4_MEMORY_SCALE=1000',
  'perf'),
 ('perf-core-sign',
  'hwdev/gf/perf.c',
  '(i32)(signed char)p[8]',
  '(i32)(unsigned char)p[8]',
  'perf'),
 ('perf-sub-stride', 'hwdev/gf/perf.c', 'p[3]+p[4]*p[5]', 'p[3]', 'perf'),
 ('perf-volt-before-core',
  'hwdev/gf/perf.c',
  'OPTIONAL(gf5_volt_id(g,e.voltage,e.voltage,+1));',
  'OPTIONAL(gf5_volt_id(g,e.voltage,e.voltage,-1));',
  'perf'),
 ('perf-abort-error',
  'hwdev/gf/perf.c',
  'if (rc) goto fault; } while (0)',
  'if (rc && rc!=GF5_EIO) goto fault; } while (0)',
  'perf'),
 ('perf-table-span', 'hwdev/gf/perf.c', 't->header+t->count*t->stride', 't->header', 'perf'),
 ('perf-reopen-restore',
  'hwdev/gf/perf.c',
  'STEP(gf5_ram_prog(g,&plan));',
  'STEP(gf5_ram_prog(g,&plan)); STEP(g->links.admission(g->links.owner,true));',
  'perf'),
 ('perf-source-preserve',
  'hwdev/gf/perf.c',
  'i32 source=g->source_rc ? g->source_rc : rc;',
  'i32 source=rc;',
  'perf'),
 ('settle',
  'hwdev/gf/clock.c',
  '#define CLK_SETTLE_US 5000U',
  '#define CLK_SETTLE_US 5001U',
  'clock'),
 ('mux', 'hwdev/gf/clock.c', '#define CLK_SHARED 0x333U', '#define CLK_SHARED 0x223U', 'clock'),
 ('core_mask',
  'hwdev/gf/clock.c',
  '#define CLK_CORE_MASK 0xc0070100U',
  '#define CLK_CORE_MASK 0U',
  'clock'),
 ('ram-refresh-second',
  'hwdev/gf/ramclock.c',
  'if (!rc) rc = wr(io, RAM_REFRESH, RAM_WORD, 1); /* refresh second */',
  'if (!rc) rc = wr(io, RAM_REFRESH, RAM_WORD, 0); /* refresh second */',
  'ramclock'),
 ('ram-extra-partition',
  'hwdev/gf/ramclock.c',
  '#define RAM_CTRL 0x004020U',
  '#define RAM_CTRL 0x004038U',
  'ramclock'),
 ('ram-sr1-restore',
  'hwdev/gf/ramclock.c',
  'first(&rc, sr_write(io, h, sr[h]));',
  'first(&rc, sr_write(io, h, sr[h] | RAM_BLANK));',
  'ramclock'),
 ('ram-self-exit',
  'hwdev/gf/ramclock.c',
  'if (!rc) rc = wr(io, RAM_SELF, RAM_WORD, 0);',
  'if (!rc) rc = wr(io, RAM_SELF, RAM_WORD, 1);',
  'ramclock'),
 ('ram-script-skip',
  'hwdev/gf/ramclock.c',
  'if (!rc && g->rom.memory_program)',
  'if (!rc && !g->rom.memory_program)',
  'ramclock'),
 ('ram-rollback-skip',
  'hwdev/gf/ramclock.c',
  'if (rc && touched) {',
  'if (rc && !touched) {',
  'ramclock'),
 ('ram-rollback-refresh-second',
  'hwdev/gf/ramclock.c',
  'first(&rc, wr(io, RAM_REFRESH, RAM_WORD, 1)); /* rollback refresh second */',
  'first(&rc, wr(io, RAM_REFRESH, RAM_WORD, 0)); /* rollback refresh second */',
  'ramclock'),
 ('ram-late-rollback-skip',
  'hwdev/gf/ramclock.c',
  'if (rc && touched && !rolled_back) {',
  'if (rc && touched && rolled_back) {',
  'ramclock'),
 ('ram-late-reblank-skip',
  'hwdev/gf/ramclock.c',
  '(void)sr_write(io, h, sr[h] | RAM_BLANK);',
  '(void)sr_write(io, h, sr[h]);',
  'ramclock'),
 ('ram-late-sr1-restore',
  'hwdev/gf/ramclock.c',
  'if (blanked & (1U << h)) (void)sr_write(io, h, sr[h]);',
  'if (blanked & (1U << h)) (void)sr_write(io, h, sr[h] | RAM_BLANK);',
  'ramclock'),
 ('therm-old-style',
  'hwdev/gf/therm.c',
  '#define TH_ADC 0x0015b0U',
  '#define TH_ADC 0x0015b8U',
  'therm'),
 ('therm-bias',
  'hwdev/gf/therm.c',
  '+f->offset_constant-TH_TEMP_BIAS;',
  '+f->offset_constant;',
  'therm'),
 ('therm-pwm-inversion',
  'hwdev/gf/therm.c',
  '    duty=divisor-duty;\n    if (f->gpio_line==2)',
  '    if (f->gpio_line==2)',
  'therm'),
 ('therm-perf-overrides-auto',
  'hwdev/gf/therm.c',
  'return th_update(g,target,false,now);',
  '(void)target; return th_update(g,f->cstate,false,now);',
  'therm'),
 ('therm-threshold-falling', 'hwdev/gf/therm.c',
  'else if (active && t.milli_c<=cool) active=false;',
  'else if (active && t.milli_c<=cool) active=true;', 'therm'),
 ('therm-step', 'hwdev/gf/therm.c', '#define TH_STEP 3U', '#define TH_STEP 100U', 'therm'),
 ('therm-auto-period',
  'hwdev/gf/therm.c',
  'g->next_auto=now+TH_POLL_US;\n        struct gf5_thermal t;',
  'g->next_auto=now+TH_POLL_US/2;\n        struct gf5_thermal t;',
  'therm'),
 ('therm-trip-hysteresis',
  'hwdev/gf/therm.c',
  'temp>last->temp-last->hyst',
  'temp>last->temp',
  'therm'),
 ('therm-threshold-min-hyst',
  'hwdev/gf/therm.c',
  'if (f->hyst[t]<2) f->hyst[t]=2;',
  'if (f->hyst[t]<1) f->hyst[t]=1;',
  'therm'),
 ('therm-p-v1-offset',
  'hwdev/gf/therm.c',
  'u32 offset=version==1 ? 12U : 16U;',
  'u32 offset=version==1 ? 16U : 16U;',
  'therm'),
 ('therm-cancel-step', 'hwdev/gf/therm.c',
  'f->mode=mode; g->next_auto=g->next_step=g->next_toggle=0; return 0;',
  'f->mode=mode; g->next_auto=g->next_toggle=0; return 0;', 'therm'),
 ('therm-first-source', 'hwdev/gf/therm.c',
  'g->source_rc=source;\n    (void)th_exit(g,first);',
  '(void)source; g->source_rc=rc;\n    (void)th_exit(g,first);', 'therm'),
 ('therm-boost-same-auto', 'hwdev/gf/therm.c',
  'if (f->active[TH_BOOST]) { target=TH_PERCENT; immediate=true; }',
  'if (f->active[TH_BOOST] && f->mode!=GF5_FAN_AUTO) { target=TH_PERCENT; immediate=true; }', 'therm'),
 ('therm-gpio-source',
  'hwdev/gf/therm.c',
  'if (!rc || !g->source_rc) g->source_rc=rc;',
  'g->source_rc=rc;',
  'therm'),
 ('therm-resume-setup', 'hwdev/gf/therm.c', 'if (!rc) rc=th_setup(g);', 'if (!rc) rc=0;', 'therm'),
 ('therm-resume-ntrip', 'hwdev/gf/therm.c', 'saved->ntrip!=g->fan.ntrip', 'false', 'therm'),
 ('therm-resume-trip-owner',
  'hwdev/gf/therm.c',
  'g->fan.trip=trip;',
  'g->fan.trip=saved->trip;',
  'therm'),
 ('therm-resume-trip-copy',
  'hwdev/gf/therm.c',
  'trip[i]=saved->trip[i];',
  'trip[i].temp=0;',
  'therm'),
 ('therm-resume-duty', 'hwdev/gf/therm.c', 'u32 duty=g->fan.duty;', 'u32 duty=0;', 'therm'),
 ('therm-resume-mode',
  'hwdev/gf/therm.c',
  'g->fan.mode=GF5_FAN_NONE;\n        rc=th_mode(g,mode,now);',
  'g->fan.mode=GF5_FAN_NONE;\n        rc=th_mode(g,GF5_FAN_NONE,now);',
  'therm'),
 ('therm-resume-sensor-deadline',
  'hwdev/gf/therm.c',
  'if (!rc && g->fan.sensor_poll) g->next_sensor=now+TH_POLL_US;',
  'if (!rc && g->fan.sensor_poll) g->next_sensor=TH_POLL_US;',
  'therm'),
 ('therm-settle-shortened',
  'hwdev/gf/therm.c',
  'if (!rc) rc=io->timing.wait_us(io->opaque,TH_SETTLE_US);',
  'if (!rc) rc=io->timing.wait_us(io->opaque,TH_SETTLE_US/2U);',
  'therm'),
 ('therm-resume-toggle-duty',
  'hwdev/gf/therm.c',
  'u32 duty=g->fan.duty;',
  'u32 duty=TH_PERCENT;',
  'therm'),
 ('volt30-shift',
  'hwdev/gf/volt.c',
  'if (t->ver==0x30) vid >>= 2;',
  'if (t->ver==0x30) vid >>= 0;',
  'volt'),
 ('volt-upper',
  'hwdev/gf/volt.c',
  'if (candidate<uv || candidate>best_uv) continue;',
  'if (candidate>uv || candidate>best_uv) continue;',
  'volt'),
 ('volt-tie',
  'hwdev/gf/volt.c',
  'if (candidate<uv || candidate>best_uv) continue;',
  'if (candidate<uv || candidate>=best_uv) continue;',
  'volt'),
 ('volt-exact', 'hwdev/gf/volt.c', 'if (candidate==uv) break;', 'if (0) break;', 'volt'),
 ('volt-mask', 'hwdev/gf/volt.c', 'return vid&t->mask;', 'return vid;', 'volt'),
 ('gpio-polarity', 'hwdev/gf/volt.c', '==!!(log[1]&1);', '==!!(log[0]&1);', 'volt'),
 ('volt-min',
  'hwdev/gf/volt.c',
  'if (!voltage_map(g,min_id,&minimum) && minimum>request) request=minimum;',
  'if (!voltage_map(g,min_id,&minimum) && minimum>request) request=0;',
  'volt'),
 ('volt-direction',
  'hwdev/gf/volt.c',
  '(condition<0 && request<prev)',
  '(condition<0 && request>prev)',
  'volt'),
 ('volt-vmap-speedo',
  'hwdev/gf/volt.c',
  'if (id<t.cnt) return GF5_ENOTSUP;',
  'if (id<t.cnt) { *uv=1000000; return 0; }',
  'volt'),
 ('volt-stop',
  'hwdev/gf/volt.c',
  'rc=gf5_gpio_set(g,pins[i].line,pins[i].log,!!(vid&(1U<<i)));\n        if (rc) return rc;',
  'rc=gf5_gpio_set(g,pins[i].line,pins[i].log,!!(vid&(1U<<i)));\n        if (rc) continue;',
  'volt'),
 ('state-cursor-copy',
  'hwdev/gf/state.c',
  'for (u32 i=0;!rc && i<count;++i)',
  'for (u32 i=0;!rc && i+1<count;++i)',
  'state'),
 ('state-complete', 'hwdev/gf/state.c', 'image->complete=true;', 'image->complete=false;', 'state'),
 ('state-old-epoch',
  'hwdev/gf/state.c',
  'g->ch->epoch==image->saved_epoch',
  'g->ch->epoch==0',
  'state'),
 ('state-manual-thermal',
  'hwdev/gf/state.c',
  'if (!rc) rc=gf5_therm_resume(g,&image->fan);',
  'if (!rc && image->fan.mode==GF5_FAN_AUTO) rc=gf5_therm_resume(g,&image->fan);',
  'state'),
 ('state-ceiling',
  'hwdev/gf/state.c',
  'if (!rc && total>U32_MAX) rc=GF5_ENOMEM;',
  'if (!rc && total>8U*1024U*1024U) rc=GF5_ENOMEM;',
  'state'),
 ('state-restore-direction',
  'hwdev/gf/state.c',
  'image->extent[i].host_copy,true);',
  'image->extent[i].host_copy,false);',
  'state'),
 ('state-free-borrow',
  'hwdev/gf/state.c',
  'if (g->image==image) g->image=0;',
  'if (g->image==image) g->image=image;',
  'state'),
 ('links-map-nospace',
  'hwdev/gf/feature5_links.c',
  'case GF1_ENOSPC: case GF2_ENOSPC: case GF4_ENOSPC: return GF5_ENOMEM;',
  'case GF1_ENOSPC: case GF2_ENOSPC: case GF4_ENOSPC: return GF5_EIO;',
  'links'),
 ('links-gr-stall',
  'hwdev/gf/feature5_links.c',
  'if (!(status&~LINKS_GR_SYNC_STALL)) return 0;',
  'if (!status) return 0;',
  'links'),
 ('links-drain-deadline',
  'hwdev/gf/feature5_links.c',
  'if (!rc) rc=gf2_fence_wait(g->ch,fence,dl);',
  'if (!rc) rc=gf2_fence_wait(g->ch,fence,dl+1U);',
  'links'),
 ('links-gr-wait-chunk',
  'hwdev/gf/feature5_links.c',
  'if (delay>GF_INIT_WAIT_CHUNK_US) delay=GF_INIT_WAIT_CHUNK_US;',
  'if (delay>GF_INIT_WAIT_CHUNK_US) delay=GF_INIT_WAIT_CHUNK_US+1U;',
  'links'),
 ('links-resume-old-rc',
  'hwdev/gf/feature5_links.c',
  'if (!rc) { ch->last_rc=old_rc; return 0; }',
  'if (!rc) { ch->last_rc=0; (void)old_rc; return 0; }',
  'links'),
 ('links-resume-new-rc', 'hwdev/gf/feature5_links.c', 'i32 rc=ch->last_rc;', 'i32 rc=0;', 'links'),
 ('links-off-negative',
  'hwdev/gf/feature5_links.c',
  'return links_result(g,gf4_service_suspend(g->display,dl));',
  'i32 rc=gf4_service_suspend(g->display,dl); return links_result(g,g->ch && g->ch->state==GF2_OFF '
  '? 0 : rc);',
  'links'),
 ('links-m-double-cleanup',
  'hwdev/gf/feature5_links.c',
  'if (!rc) rc=ctx->io.restore(ctx->io.opaque,ctx);',
  'if (!rc || rc==GF_SCRIPT_ETIME) rc=ctx->io.restore(ctx->io.opaque,ctx);',
  'links'),
 ('links-m-budget',
  'hwdev/gf/feature5_links.c',
  'ctx->steps=l->init->steps;',
  'ctx->steps=0;',
  'links'),
 ('links-boot-split',
  'hwdev/gf/feature5_links.c',
  'if (bytes>GF_MODE_VRAM_BYTES-off) bytes=GF_MODE_VRAM_BYTES-off;',
  'if (bytes>GF_MODE_VRAM_BYTES-off) bytes=left;',
  'links'),
 ('links-clock-hi', 'hwdev/gf/feature5_links.c', '*us=((u64)hi<<32)|lo;', '*us=lo;', 'links'),
 ('links-alloc-output',
  'hwdev/gf/feature5_links.c',
  'if (!phys) return links_result(g,GF5_ENOMEM);',
  'if (!phys) { *out=0; return links_result(g,GF5_ENOMEM); }',
  'links'),
 ('tile-fb-flush',
  'hwdev/gf/tile.c',
  '(rc=io->read(io->opaque,TILE_BAR,FB_ADDR+d,TILE_WIDTH,&flush)) ||',
  '(0 && io->read(io->opaque,TILE_BAR,FB_ADDR+d,TILE_WIDTH,&flush)) ||',
  'tile'),
 ('tile-gr-order',
  'hwdev/gf/tile.c',
  'GR_PITCH+d,TILE_WIDTH,pitch',
  'GR_ADDR+d,TILE_WIDTH,pitch',
  'tile'),
 ('tile-last-slot',
  'hwdev/gf/tile.c',
  '!g || !tile || slot>=GF5_TILE_COUNT || !g->ch',
  '!g || !tile || slot>=GF5_TILE_COUNT-1U || !g->ch',
  'tile'),
 ('tile-base-align', 'hwdev/gf/tile.c', '(tile->base&(TILE_ALIGN-1U))', '0', 'tile'),
 ('rom-pc-zero',
  'hwdev/gf/feature5_rom.c',
  'if (!rc) pc = (u32)rom[data] | (u32)rom[data + 1U] << 8;',
  'if (!rc) pc = 0;',
  'rom'),
 ('rom-partial-free',
  'hwdev/gf/feature5_rom.c',
  'g->links.free(g->links.owner, bytes);',
  '(void)bytes;',
  'rom'),

 # U0-U6 acceptance handoffs; product paths remain the mutation source.
 ('clock-guard-mismatch', 'hwdev/gf/clock.c',
  'if (!same_plan(p,&guard->canonical) || !same_plan(p,&recalculated))',
  'if (false && (!same_plan(p,&guard->canonical) || !same_plan(p,&recalculated)))', 'clock'),
 ('clock-guard-recalculate', 'hwdev/gf/clock.c',
  'if (!same_plan(p,&guard->canonical) || !same_plan(p,&recalculated))',
  'if (!same_plan(p,&guard->canonical))', 'clock'),
 ('clock-guard-epoch', 'hwdev/gf/clock.c',
  'guard->epoch!=g->ch->epoch',
  'false', 'clock'),
 ('clock-guard-rom-generation', 'hwdev/gf/clock.c',
  'guard->rom_generation!=g->rom_generation',
  'false', 'clock'),
 ('clock-guard-consume', 'hwdev/gf/clock.c',
  'g->plan_guard.core_used=true;',
  'g->plan_guard.core_used=false;', 'clock'),
 ('clock-guard-calc-invalidate', 'hwdev/gf/clock.c',
  'gf5_plan_invalidate(g);',
  '/* mutation: retain previous guard */', 'clock'),
 ('clock-read-error', 'hwdev/gf/clock.c',
  'if (rc) return result(g,rc);\n    *out=c;',
  'if (rc) return result(g,0);\n    *out=c;', 'clock'),
 ('ram-plan-check', 'hwdev/gf/ramclock.c',
  'rc = gf5_plan_check(g, plan, true);',
  'rc = 0;', 'ramclock'),
 ('ram-plan-consume', 'hwdev/gf/ramclock.c',
  'g->plan_guard.ram_used = true;',
  'g->plan_guard.ram_used = false;', 'ramclock'),
 ('perf-optional-only-enodev', 'hwdev/gf/perf.c',
  'if (gf5_rc_map(rc)==GF5_ENODEV)',
  'if (gf5_rc_map(rc)==GF5_ENOTSUP)', 'perf'),
 ('perf-disabled-pstate', 'hwdev/gf/perf.c',
  'return p[0]!=0xff;',
  'return true;', 'perf'),
 ('heat-level-not-edge', 'hwdev/gf/perf.c',
  'bool lowering=temp>=down;',
  'if (g->heat.down_active && temp>=down) { return finish(g,0); } bool lowering=temp>=down;', 'perf'),
 ('heat-three-cool-samples', 'hwdev/gf/perf.c',
  'if (++g->heat.cool_samples<GF5_HEAT_COOL_SAMPLES)',
  'if (++g->heat.cool_samples<GF5_HEAT_COOL_SAMPLES-1U)', 'perf'),
 ('unknown-fan-monitor', 'hwdev/gf/therm.c',
  'if (r[0]!=0x10U && unsupported) *unsupported=true;',
  'if (r[0]!=0x10U) return GF5_ENOTSUP;', 'therm'),
 ('protective-stop-handoff', 'hwdev/gf/feature5_service.c',
  'if (g->heat.stop_pending)\n        geforce_heat_stop_request',
  'if (false)\n        geforce_heat_stop_request', 'foreground'),
 ('heat-strict-cool', 'hwdev/gf/perf.c',
  'if (temp>cool || g->heat.critical_paused)',
  'if (temp>=cool || g->heat.critical_paused)', 'perf'),
 ('heat-entry-cap', 'hwdev/gf/perf.c',
  'g->heat.entry_clock=current; g->heat.ceiling=current;',
  'g->heat.entry_clock=current; g->heat.entry_clock.core_khz=324000; g->heat.entry_clock.shader_khz=324000; g->heat.ceiling=current;', 'perf'),
 ('heat-core-only', 'hwdev/gf/perf.c',
  'rc=gf5_clock_prog(g,&plan); /* No RAM, voltage or fan change. */',
  'rc=gf5_ram_prog(g,&plan); if (!rc) rc=gf5_clock_prog(g,&plan); /* No RAM, voltage or fan change. */', 'perf'),
 ('state-heat-image-ceiling', 'hwdev/gf/state.c',
  'if (state_above_ceiling(g,&image->clock)) return state_reject(g,GF5_EBUSY);',
  'if (false && state_above_ceiling(g,&image->clock)) return state_reject(g,GF5_EBUSY);', 'state'),
 ('state-heat-shader-ceiling', 'hwdev/gf/state.c',
  '(clock->shader_khz ? clock->shader_khz : clock->core_khz)>',
  '(clock->shader_khz ? clock->shader_khz : clock->core_khz)*0U>', 'state'),
 ('state-heat-memory-ceiling', 'hwdev/gf/state.c',
  'clock->mem_khz>g->heat.ceiling.mem_khz);',
  'false);', 'state'),
 ('state-heat-decoded-ceiling', 'hwdev/gf/state.c',
  'if (!rc) rc=state_plan_ceiling(g,&plan);',
  'if (!rc && false) rc=state_plan_ceiling(g,&plan);', 'state'),
 ('state-heat-decode-stage2', 'hwdev/gf/state.c',
  'khz[i]=khz[i]*n/m;',
  'khz[i]=khz[i];', 'state'),
 ('state-heat-decode-postdivider', 'hwdev/gf/state.c',
  'khz[i]>>=(c>>16)&7U;',
  'khz[i]>>=0;', 'state'),
 ('state-heat-decode-enable', 'hwdev/gf/state.c',
  'if (!(c&0x80000000U) || !m || !n) return GF5_ERANGE;',
  'if (!m || !n) return GF5_ERANGE;', 'state'),
 ('state-heat-save-closed', 'hwdev/gf/state.c',
  'if (g->heat.critical_paused || g->heat.stop_pending)\n        return state_reject(g,GF5_ESTATE);\n    for (u32 i=0;i<GF5_TILE_COUNT;++i)',
  'if (false) return state_reject(g,GF5_ESTATE);\n    for (u32 i=0;i<GF5_TILE_COUNT;++i)', 'state'),
 ('state-heat-restore-closed', 'hwdev/gf/state.c',
  'if (g->heat.critical_paused || g->heat.stop_pending)\n        return state_reject(g,GF5_ESTATE);\n    if (state_above_ceiling',
  'if (false) return state_reject(g,GF5_ESTATE);\n    if (state_above_ceiling', 'state'),
 ('state-heat-save-retain', 'hwdev/gf/state.c',
  'image->complete=true;',
  'image->complete=true; g->heat=(struct gf5_heat_state){0};', 'state'),
 ('state-heat-restore-retain', 'hwdev/gf/state.c',
  'if (g->heat.down_active) g->heat.epoch=g->ch->epoch;',
  'if (g->heat.down_active) { g->heat.epoch=g->ch->epoch; g->heat.cool_samples=0; }', 'state'),
 ('state-heat-restore-epoch', 'hwdev/gf/state.c',
  'if (g->heat.down_active) g->heat.epoch=g->ch->epoch;',
  'if (g->heat.down_active) g->heat.epoch=image->saved_epoch;', 'state'),
 ('volt-absent-class', 'hwdev/gf/volt.c',
  'u32 p=t->off;\n    if (!p) return GF5_ENODEV;',
  'u32 p=t->off;\n    if (!p) return GF5_ENOTSUP;', 'volt'),
 ('volt-zero-class', 'hwdev/gf/volt.c',
  'if (!id && !optional_zero) return GF5_ENOTSUP;',
  'if (!id && !optional_zero) return GF5_ENODEV;', 'volt'),
 ('volt-unknown-vmap-class', 'hwdev/gf/volt.c',
  'if (t.ver!=0x10 && t.ver!=0x20) return GF5_ENOTSUP;',
  'if (t.ver!=0x10 && t.ver!=0x20) return GF5_ENODEV;', 'volt'),
 ('volt-check-minimum', 'hwdev/gf/volt.c',
  'if (!rc && minimum>request) request=minimum;',
  'if (!rc && minimum>request) request=0;', 'volt'),
 ('volt-check-zero-floor', 'hwdev/gf/volt.c',
  'rc=voltage_map_request(g,min_id,&minimum,true);',
  'rc=voltage_map_request(g,min_id,&minimum,false);', 'volt'),
 ('volt-check-candidate', 'hwdev/gf/volt.c',
  'if (!rc) rc=voltage_choice(g,&t,request,&vid);',
  'if (!rc) { rc=voltage_choice(g,&t,request,&vid); rc=0; }', 'volt'),
 ('volt-check-no-mmio', 'hwdev/gf/volt.c',
  'if (!rc) rc=voltage_map(g,id,&request);',
  'if (!rc) { rc=voltage_get(g,&request); if (!rc) rc=voltage_map(g,id,&request); }', 'volt'),
 ('u4-sensor-period', 'hwdev/gf/therm.c',
  'g->next_sensor=now+GF5_SENSOR_INTERVAL_US;',
  'g->next_sensor=now+GF5_SENSOR_INTERVAL_US/2U;', 'therm'),
 ('u4-sensor-catchup', 'hwdev/gf/therm.c',
  'g->next_sensor=now+GF5_SENSOR_INTERVAL_US;',
  'g->next_sensor+=GF5_SENSOR_INTERVAL_US;', 'therm'),
 ('u4-sensor-low-word', 'hwdev/gf/therm.c',
  '(!g->next_sensor || now>=g->next_sensor)',
  '(!g->next_sensor || (u32)now>=(u32)g->next_sensor)', 'therm'),
 ('u4-heat-edge-only', 'hwdev/gf/therm.c',
  'i32 heat_rc=gf5_heat_step(g,&t,now);',
  'i32 heat_rc=boost_rising ? gf5_heat_step(g,&t,now) : 0;', 'therm'),
 ('u4-fanless-heat', 'hwdev/gf/therm.c',
  'i32 heat_rc=gf5_heat_step(g,&t,now);',
  'i32 heat_rc=g->fan_off ? 0 : gf5_heat_step(g,&t,now);', 'therm'),
 ('u4-threshold-rising', 'hwdev/gf/therm.c',
  'if (!active && t.milli_c>=limit) active=true;',
  'if (!active && t.milli_c>limit) active=true;', 'therm'),
 ('u4-threshold-falling', 'hwdev/gf/therm.c',
  'else if (active && t.milli_c<=cool) active=false;',
  'else if (active && t.milli_c<cool) active=false;', 'therm'),
 ('u4-shutdown-priority', 'hwdev/gf/therm.c',
  'if (t.milli_c>=(i64)f->threshold[TH_SHUTDOWN]*TH_MILLI) {',
  'if (false) {', 'therm'),
 ('u4-boost-hold', 'hwdev/gf/therm.c',
  'if (f->active[TH_BOOST]) { target=TH_PERCENT; immediate=true; }',
  'if (false) { target=TH_PERCENT; immediate=true; }', 'therm'),
 ('u4-boost-none', 'hwdev/gf/therm.c',
  'if (f->active[TH_BOOST]) return th_update(g,TH_PERCENT,true,now);',
  'if (false) return th_update(g,TH_PERCENT,true,now);', 'therm'),
 ('u4-sensor-loss-stop', 'hwdev/gf/therm.c',
  'if (g->heat.down_active || g->heat.critical_paused) {',
  'if (false) {', 'therm'),
 ('u4-failed-sample', 'hwdev/gf/therm.c',
  '(void)gf5_heat_step(g,0,0);',
  '(void)g;', 'therm'),
 ('foreground-poll', 'drivers/video/geforce/geforce_module.c',
  'if (ops && ops->service_poll)',
  'if (0 && ops && ops->service_poll)', 'foreground'),
 ('critical-drain', 'hwdev/gf/feature5_service.c',
  'rc=g->links.drain_pause(g->links.owner);',
  'rc=0;', 'foreground'),
 ('shutdown-route', 'drivers/video/geforce/backend_geforce.c',
  'if (pc98_route_to_98()) rc = -1;',
  'if ((!geforce_context->heat || !geforce_context->heat->latched) && pc98_route_to_98()) rc = -1;', 'foreground'),
 ('fault-monitor', 'hwdev/gf/feature5_service.c',
  'if (!g || g!=service_ctx || (service_slot && !*service_slot) ||',
  'if (!g || (g->ch && g->ch->fault.latched) || g!=service_ctx || (service_slot && !*service_slot) ||', 'foreground'),
 ('probe-latch', 'drivers/video/geforce/backend_geforce.c',
  'if (geforce_context->heat && geforce_context->heat->latched) ok=0;',
  'if (0 && geforce_context->heat && geforce_context->heat->latched) ok=0;', 'foreground'),
 ('foreground-reentry', 'drivers/video/geforce/geforce_module.c',
  'if (foreground_poll_busy) return;',
  'if (0 && foreground_poll_busy) return;', 'foreground'),
 ('syscall-exit', 'exec/exec_syscall.inc',
  'if (geforce_fg_due(tick_count)) geforce_foreground_poll();',
  'if (0 && geforce_fg_due(tick_count)) geforce_foreground_poll();', 'foreground'),
 ('gpio-log', 'kdebug/gf5_test.c',
  'rc = o->gpio_find(owner, (u8)a[0], &line, log);',
  'rc = o->gpio_find(owner, (u8)a[0], &line, log);\n        log[0] = 0; log[1] = 1;', 'foreground-gpio'),
 ('observation-read-error', 'kdebug/gf5_test.c',
  'status[i] = io->read(io->opaque, 0, regs[i], sizeof(u32), &value[i]);',
  'status[i] = io->read(io->opaque, 0, regs[i], sizeof(u32), &value[i]);\n            if (status[i]) { status[i] = 0; value[i] = 0; }', 'foreground-observe'),
 ('gui-fullscreen-recovery', 'userland/gshell/src/session.rs',
  'crate::multiapp::request_kill_all();',
  '/* mutation: omit grace expiry recovery. */', 'foreground-gui'),
]


# U6 uses the real production scanlog source set and verbatim foreground bodies.
# Only CPU halt is replaced. Generate after edits so syscall mutations exercise
# the product source, never a stale generated header or a file in the worktree.
def foreground_header(tree):
    def function(path, name):
        source = (tree / path).read_text()
        match = re.search(r'^(?:static )?(?:void|int|i32) (?:__cdecl )?' +
                          name + r'\([^;{}]*\)\n\{', source, re.M)
        if not match:
            raise RuntimeError(f'foreground body missing: {path}/{name}')
        return source[match.start():source.index('\n}', match.end()) + 2]

    pieces = [function('kernel/sys.c', name) for name in ('sys_halt', 'sys_reboot')]
    pieces += [function('exec/exec_schedule.inc', name)
               for name in ('exec_sys_yield', 'exec_park_poll')]
    pieces += [function(path, name) for path, name in
               (('kernel/gui.c', 'gui_display_failed'),
                ('gfx/gfx_core.c', 'geforce_display_failed'),
                ('exec/appslot.c', 'appslot_abort_request'))]
    source = (tree / 'exec/exec_syscall.inc').read_text()
    tail = source.split('syscall_complete:', 1)[1]
    pieces.append('static void u6_syscall_tail(u32 *frame) {\n'
                  'int prev_caller=0, prev_in_syscall=0;\nu32 *prev_frame=0;\n' + tail)
    (tree / 'tools/tests/gf5_foreground_entries.h').write_text(
        '\n'.join(pieces).replace('_halt();', 'u6_halt();'))


def foreground_gui(tree, directory):
    # Reuse only the existing host setup/build function, before its suite runs.
    # Both generated Rust roots and the appended fixture remain private.
    script = tree / 'userland/gshell/host/integration.py'
    setup = script.read_text().split('# 票 KBD_NAV', 1)[0]
    setup = setup.replace("OUT = ROOT / 'userland/gshell/target/gshell-host'",
                          'OUT = Path(' + repr(str(directory / 'gshell-host')) + ')')
    namespace = {'__file__': str(script), '__name__': 'gf5_gui_host'}
    exec(compile(setup, str(script), 'exec'), namespace)
    gshell = directory / 'gui-tree'
    for sub in ('src', 'host'):
        shutil.copytree(tree / 'userland/gshell' / sub, gshell / sub)
    host = gshell / 'host/wm_tests.rs'
    fixture = (tree / 'tools/tests/gf5_foreground_host.c').read_text()
    fixture = fixture.split('/* U6_RUST_BEGIN\n', 1)[1].split('U6_RUST_END */', 1)[0]
    host.write_text(host.read_text() + fixture)
    exe = namespace.get('build')(gshell, 'gf5-foreground')
    return subprocess.run([str(exe), 'u6_heat_shutdown_recovers_gui_fullscreen',
                           '--test-threads=1'], cwd=tree).returncode


def foreground_case(root, case, directory, edits):
    import test_gf_display
    # These include graphs cross many component boundaries. Copy them once per
    # invocation so quoted includes and edits cannot escape into the real tree.
    tree = mutpar.overlay(root, directory / 'tree', real=(
        'hwdev', 'kdebug', 'drivers', 'include', 'kernel', 'tools/tests',
        'exec', 'gfx', *edits))
    for rel, source in edits.items():
        (tree / rel).write_text(source)
    if case == 'foreground-gui':
        return foreground_gui(tree, directory)
    foreground_header(tree)
    exe = directory / case
    cmd = command(tree, case, exe)
    cmd[cmd.index('-o'):cmd.index('-o')] = [
        str(tree / rel) for rel in test_gf_display.gf4_sources(
            tree, 'scanlog', tree / 'tools/tests/gf5_foreground_host.c')
    ] if case in ('foreground', 'foreground-reboot') else [
        str(tree / 'kdebug/gf5_test.c'), str(tree / 'kdebug/gfinit_pll.c')]
    subprocess.run(cmd, cwd=tree, check=True)
    return host32.run([str(exe)], cwd=tree, timeout=120).returncode


def command(tree, case, exe):
    _, sources = CASES[case]
    foreground = case.startswith('foreground')
    flags = []
    if foreground:
        flags = ['-DGF_PRODUCTION_MODULE', '-DGF4_REAL_DISPLAY',
                 '-I' + str(tree / 'tools/tests/host_arch'),
                 '-I' + str(tree / 'build/out/geforce')]
        if case != 'foreground':
            flags.append('-DU6_' + case.split('-', 1)[1].upper())
    fixture = 'foreground' if foreground else case
    return ['gcc', '-m32', '-march=i386', '-std=gnu11', '-O2', '-Wall',
            '-Wextra', '-Werror', '-Werror=vla', '-Wno-unused-parameter',
            '-ffreestanding', '-fno-pie', '-fno-stack-protector',
            '-ffunction-sections', '-fdata-sections', '-nostdlib', '-static',
            '-no-pie', '-Wl,--gc-sections', '-D__KERNEL_BUILD__',
            *flags, *include_flags(tree), '-I' + str(tree),
            str(tree / ('tools/tests/gf5_' + fixture + '_host.c')),
            *[str(tree / source) for source in sources], '-o', str(exe)]


def run_case(tree, case, directory, edits=None):
    if case.startswith('foreground'):
        with tempfile.TemporaryDirectory(prefix=case + '-', dir=directory) as td:
            return foreground_case(tree, case, Path(td), edits or {})
    exe = directory / case
    subprocess.run(command(tree, case, exe), cwd=tree, check=True)
    return host32.run([str(exe)], cwd=tree, timeout=120).returncode


def main():
    failures = 0
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mutate', action='store_true')
    parser.add_argument('--case', action='append', choices=CASES)
    parser.add_argument('--mutation', action='append', choices=[m[0] for m in MUTATIONS])
    args = parser.parse_args()
    mutate = args.mutate
    with tempfile.TemporaryDirectory(prefix='gf5-host-') as td:
        directory = Path(td)
        for case, (_, sources) in CASES.items():
            if args.case and case not in args.case:
                continue
            fixture = 'foreground' if case.startswith('foreground') else case
            required = ['tools/tests/gf5_' + fixture + '_host.c', *sources]
            missing = [p for p in required if not (ROOT / p).is_file()]
            if missing:
                print(f'FAIL {case}: missing {", ".join(missing)}', flush=True)
                failures += 1
                continue
            try:
                rc = run_case(ROOT, case, directory)
            except subprocess.CalledProcessError:
                print(f'FAIL {case}: compile/link', flush=True)
                failures += 1
                continue
            if rc:
                print(f'FAIL {case}: runtime rc={rc}', flush=True)
                failures += 1
                continue
            print(f'PASS {case}: runtime GREEN', flush=True)
            for name, rel, old, new, target in (MUTATIONS if mutate else ()):
                if target != case or (args.mutation and name not in args.mutation):
                    continue
                original = (ROOT / rel).read_text()
                if not old or old == new or original.count(old) != 1:
                    print(f'FAIL {case}/{name}: invalid or ambiguous anchor', flush=True)
                    failures += 1
                    continue
                with tempfile.TemporaryDirectory(prefix='gf5-mut-') as md:
                    exe = Path(md) / case
                    cmd = command(ROOT, case, exe)
                    try:
                        edits = {rel: original.replace(old, new, 1)}
                        if case.startswith('foreground'):
                            red = run_case(ROOT, case, Path(md), edits)
                        else:
                            tree = mutpar.build_in_tree(ROOT, md, edits, [cmd])
                            red = host32.run([str(exe)], cwd=tree, timeout=120).returncode
                    except subprocess.CalledProcessError:
                        print(f'FAIL {case}/{name}: compile/link is not runtime RED', flush=True)
                        failures += 1
                        continue
                    if red:
                        print(f'PASS {case}/{name}: runtime RED rc={red}', flush=True)
                    else:
                        print(f'FAIL {case}/{name}: survived', flush=True)
                        failures += 1
                try:
                    restored = run_case(ROOT, case, directory)
                except subprocess.CalledProcessError:
                    restored = 1
                if not restored:
                    print(f'PASS {case}/{name}: restored runtime GREEN', flush=True)
                if restored:
                    print(f'FAIL {case}/{name}: restored control', flush=True)
                    failures += 1
    if args.case:
        return int(bool(failures))
    runner = subprocess.run([sys.executable, '-B', str(ROOT / 'tools/tests/test_gf_runner.py'),
                             *(['--mutate'] if mutate else [])], cwd=ROOT)
    if runner.returncode:
        print(f'FAIL runner: rc={runner.returncode}', flush=True)
        failures += 1
    return int(bool(failures))


def run(ctx) -> int:
    import os
    env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    args = [sys.executable, '-B', 'tools/tests/test_gf5.py']
    if ctx.mutate:
        args.append('--mutate')
    return subprocess.call(args, cwd=ctx.root, env=env, close_fds=False)


if __name__ == '__main__':
    raise SystemExit(main())
