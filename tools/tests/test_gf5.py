"""Layer 5 component hosts and the existing runner trace acceptance suite."""
CHECK = dict(name='gf5-host', tier=1, needs=[], budget=90.0,
             covers=['hwdev/gf/**', 'kdebug/gf5_test.c', 'kdebug/gfinit_pll.*',
                     'kdebug/gfinit_decode*', 'kdebug/gfinit_exec.c',
                     'kdebug/recover.c', 'kdebug/recover_gf.c', 'tools/tests/gf5*', 'tools/tests/gf_startlog_host.h', 'include/kprintf.h',
                     'tools/tests/test_gf5.py', 'tools/tests/test_gf_runner.py',
                     'tools/tests/gf_runner*', 'tools/tests/gf4_c2linit_host.c',
                     'tools/tests/gfinit_exec_host.c', 'tools/tests/gfinit_host.c',
                     'tools/tests/recover_gf_host.c', 'tools/tests/kernel_includes.py',
                     'tools/tests/host32.py',
                     'tools/tests/mutpar.py', 'tools/tests/gf4_display_fake.h',
                     'tools/tests/gf_accel_host.h', 'include/types.h'])

from pathlib import Path
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
    'ramclock': (2, ['hwdev/gf/ramclock.c']),
    'therm': (3, ['hwdev/gf/therm.c']),
    'volt': (4, ['hwdev/gf/volt.c']),
    'tile': (5, ['hwdev/gf/tile.c']),
    'state': (6, ['hwdev/gf/state.c']),
    'rom': (8, ['hwdev/gf/feature5_rom.c']),
    'links': (9, ['hwdev/gf/feature5_links.c']),
}
# Per-stage report*.md proposals, latest replacement per name wins.
# Tuple: name, product path, unique old text, new text, CASES key.
# B1/B2 binary names and B4 function observations are normalized to CASES.
# Anchors checked at integration; runtime RED remains for PM verification.
MUTATIONS = [
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
 ('therm-threshold-falling',
  'hwdev/gf/therm.c',
  'else if (active && temp<=f->threshold[i]-f->hyst[i]) active=false;',
  'else if (active && temp<=f->threshold[i]-f->hyst[i]) active=true;',
  'therm'),
 ('therm-step', 'hwdev/gf/therm.c', '#define TH_STEP 3U', '#define TH_STEP 100U', 'therm'),
 ('therm-auto-period',
  'hwdev/gf/therm.c',
  'g->next_auto=now+TH_POLL_US;',
  'g->next_auto=now+TH_POLL_US/2;',
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
  'g->fan.mode=GF5_FAN_NONE;\n    g->next_auto=g->next_step=g->next_toggle=0;',
  'g->fan.mode=GF5_FAN_NONE;\n    g->next_auto=g->next_toggle=0;', 'therm'),
 ('therm-first-source', 'hwdev/gf/therm.c', 'g->source_rc=source;', '(void)source; g->source_rc=rc;', 'therm'),
 ('therm-boost-same-auto',
  'hwdev/gf/therm.c',
  'if (i==TH_BOOST && f->mode!=GF5_FAN_AUTO) {',
  'if (i==TH_BOOST) {',
  'therm'),
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
  'if (!rc) rc=th_mode(g,mode,now);\n    if (!rc && g->fan.sensor_poll)',
  'if (!rc) rc=th_mode(g,GF5_FAN_NONE,now);\n    if (!rc && g->fan.sensor_poll)',
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
  '!tile || slot>=GF5_TILE_COUNT || !g->ch',
  '!tile || slot>=GF5_TILE_COUNT-1U || !g->ch',
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
  'rom')]


def command(tree, case, exe):
    _, sources = CASES[case]
    return ['gcc', '-m32', '-march=i386', '-std=gnu11', '-O2', '-Wall',
            '-Wextra', '-Werror', '-Werror=vla', '-Wno-unused-parameter',
            '-ffreestanding', '-fno-pie', '-fno-stack-protector',
            '-ffunction-sections', '-fdata-sections', '-nostdlib', '-static',
            '-no-pie', '-Wl,--gc-sections', '-D__KERNEL_BUILD__',
            *include_flags(tree), '-I' + str(tree),
            str(tree / ('tools/tests/gf5_' + case + '_host.c')),
            *[str(tree / source) for source in sources], '-o', str(exe)]


def run_case(tree, case, directory):
    exe = directory / case
    subprocess.run(command(tree, case, exe), cwd=tree, check=True)
    return host32.run([str(exe)], cwd=tree, timeout=120).returncode


def main():
    failures = 0
    mutate = '--mutate' in sys.argv
    with tempfile.TemporaryDirectory(prefix='gf5-host-') as td:
        directory = Path(td)
        for case, (_, sources) in CASES.items():
            required = ['tools/tests/gf5_' + case + '_host.c', *sources]
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
                if target != case:
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
                        tree = mutpar.build_in_tree(ROOT, md,
                            {rel: original.replace(old, new, 1)}, [cmd])
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
                if restored:
                    print(f'FAIL {case}/{name}: restored control', flush=True)
                    failures += 1
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
