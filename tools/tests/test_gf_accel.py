#!/usr/bin/env python3
"""Layer 3 component hosts/mutations and existing transfer/GUI acceptance tests."""
CHECK = dict(name='gf-accel-host', tier=1, needs=[], budget=90.0,
    covers=['tools/tests/gf_ownerless_fixture.py', 'include/**', 'kdebug/*.h', 'kernel/irq/irq.h', 'hwdev/*.h', 'mm/*.h', 'drivers/pci/pci.h', 'lib/serial_diag.h', 'hwdev/gf/**', 'include/gfx_hal.h', 'gfx/gfx_internal.h', 'hwdev/gf/accel*', 'hwdev/gf/vram_view.*',
            'hwdev/gf/command.h', 'hwdev/gf/foundation.h',
            'drivers/video/geforce/*', 'tools/tests/gf_accel*',
            'tools/tests/gf_command_runtime_host.h',
            'tools/tests/test_gf_accel.py', 'tools/accept/gf_accel.py',
            'drivers/video/geforce/gf_accept.c', 'drivers/video/geforce/gf_present.c',
            'drivers/video/geforce/geforce_module.h', 'hwdev/gf/production.h',
            'userland/gshell/src/**', 'userland/gshell/host/**', 'userland/gshell/build.rs',
            'sdk/rust/os32api/**', 'tools/tests/os32api_host.py', 'userland/lib/font/**',
            'exec/shell_service.h', 'sdk/include/os32/os32_gui_shared.h',
            'sdk/include/os32/os32_kapi_shared.h',
            'tools/tests/host32.py', 'tools/tests/kernel_includes.py'])
from pathlib import Path
import argparse
import importlib.util
import os
import re
import shutil
import subprocess
import sys
import tempfile
import host32
import os32api_host
from kernel_includes import include_flags
from gf_ownerless_fixture import ownerless_body
ROOT = Path(__file__).resolve().parents[2]
FLAGS = ['gcc', '-m32', '-march=i386', '-std=gnu11', '-O2', '-Wall', '-Wextra', '-Werror',
         '-ffreestanding', '-fno-pie', '-fno-stack-protector', '-nostdlib', '-static', '-no-pie']


# GF_FEAT3_DESIGN §13/G1: one real product TU per host, UNIT selected in C.
# Every registered entry is required, including in this wiring worktree before
# the product branches are integrated.
COMPONENTS = {
    'b0': 'hwdev/gf/accel_objects.c',
    'b1': 'hwdev/gf/accel_surface.c',
    'b2': 'hwdev/gf/accel_rop.c',
    'b3': 'hwdev/gf/accel_fill.c',
    'b4': 'hwdev/gf/accel_copy.c',
    'b5': 'hwdev/gf/accel_mono.c',
    'b6': 'hwdev/gf/accel_ifc.c',
    'b7': 'hwdev/gf/accel_m2mf.c',
    'b8': 'hwdev/gf/accel_transfer.c',
    'b9': 'hwdev/gf/accel_scale.c',
    'b10': 'drivers/video/geforce/gf_present_m2mf.c',
    'v0': 'hwdev/gf/vram_view.c',
    'c0': 'drivers/video/geforce/gf_hook.c',
    'c1': 'hwdev/gf/service.c',
    'c2': 'hwdev/gf/display/quiesce.c',
    'wire': 'drivers/video/geforce/geforce_body.c',
}

# Reports gf3-b*/report*.md, gf3-v0/report.md (B0 fix1 supersedes old anchors).
# (name, product, unique original, replacement, runtime RED host).
# Expectations and fixtures are never mutated. No report/reference files are
# read at runtime; these registrations are frozen repository inputs.
MUTATIONS = {
    'b0': [
        ('surface-class', 'hwdev/gf/accel_objects.c',
         'GF3_CLASS_NULL, GF3_CLASS_SURFACE2D,',
         'GF3_CLASS_NULL, GF2_GR_SURFACE2D_NV30,', 'tools/tests/gf_accel_b0_host.c'),
        ('ifc-class', 'hwdev/gf/accel_objects.c',
         'GF3_CLASS_M2MF, GF3_CLASS_IFC\n',
         'GF3_CLASS_M2MF, GF3_CLASS_SURFACE2D\n', 'tools/tests/gf_accel_b0_host.c'),
        ('m2mf-subc', 'hwdev/gf/accel_objects.c',
         '{GF3_SUBC_M2MF, SETUP_BIND, 1, {GF3_HANDLE_M2MF}}',
         '{GF3_SUBC_REFERENCE, SETUP_BIND, 1, {GF3_HANDLE_M2MF}}', 'tools/tests/gf_accel_b0_host.c'),
        ('notify-null', 'hwdev/gf/accel_objects.c',
         '{GF3_SUBC_M2MF, SETUP_DMA_NOTIFY, 1, {GF3_HANDLE_NOTIFY}}',
         '{GF3_SUBC_M2MF, SETUP_DMA_NOTIFY, 1, {GF3_HANDLE_NULL}}', 'tools/tests/gf_accel_b0_host.c'),
        ('partial-success', 'hwdev/gf/accel_objects.c',
         'if (rc) return failed_init(c, rc);\n    }\n    rc = drain',
         'if (rc) return c->last_rc = GF3_OK;\n    }\n    rc = drain', 'tools/tests/gf_accel_b0_host.c'),
        ('notify-relative', 'hwdev/gf/accel_objects.c',
         'c->notify_storage.off;\n        rc = gf2_dma_new',
         '0U;\n        rc = gf2_dma_new', 'tools/tests/gf_accel_b0_host.c'),
        ('gdi-cga6', 'hwdev/gf/accel_objects.c',
         '{GF3_SUBC_GDI, SETUP_GDI_MONO_FORMAT, 1, {SETUP_GDI_CGA6}}',
         '{GF3_SUBC_GDI, SETUP_GDI_MONO_FORMAT, 1, {2U}}', 'tools/tests/gf_accel_b0_host.c'),
        ('stop-retention', 'hwdev/gf/accel_objects.c',
         'if (stopped == GF2_ESTOP) return c->last_rc = stopped;',
         'if (stopped == GF2_ESTOP) return c->last_rc = rc;', 'tools/tests/gf_accel_b0_host.c'),
    ],
    'b1': [
        ('draw-pitch', 'hwdev/gf/accel_surface.c',
         '(s->pitch & (GF3_DRAW_PITCH_ALIGN - 1U))',
         'false', 'tools/tests/gf_accel_b1_host.c'),
        ('draw-base', 'hwdev/gf/accel_surface.c',
         '(s->off & (GF3_DRAW_BASE_ALIGN - 1U))',
         'false', 'tools/tests/gf_accel_b1_host.c'),
        ('b1_argb_gdi', 'hwdev/gf/accel_surface.c',
         '{4, 10, 11, 3, 3, 4, 3}',
         '{4, 10, 10, 3, 3, 4, 3}', 'tools/tests/gf_accel_b1_host.c'),
        ('b1_span_extent', 'hwdev/gf/accel_surface.c',
         'bytes = (rows-1)*pitch+rowbytes;',
         'bytes = rowbytes;', 'tools/tests/gf_accel_b1_host.c'),
        ('b1_identity', 'hwdev/gf/accel_surface.c',
         's->off, s->bytes, &key);',
         's->off, 1, &key);', 'tools/tests/gf_accel_b1_host.c'),
        ('b1_sf_gdi', 'hwdev/gf/accel_surface.c',
         'gdi ? sf.sf_gdi : sf.sf,',
         'sf.sf,', 'tools/tests/gf_accel_b1_host.c'),
        ('b1_sf_pitch_swap', 'hwdev/gf/accel_surface.c',
         '(dst->pitch << 16) | src->pitch',
         '(src->pitch << 16) | dst->pitch', 'tools/tests/gf_accel_b1_host.c'),
        ('b1_sf_offset_swap', 'hwdev/gf/accel_surface.c',
         'src->off, dst->off};',
         'dst->off, src->off};', 'tools/tests/gf_accel_b1_host.c'),
    ],
    'b2': [
        ('b2-copy-rop', 'hwdev/gf/accel_rop.c',
         '{0x44, 0x45}, {0xcc, 0xca}',
         '{0x44, 0x45}, {0xcc, 0xcc}', 'tools/tests/gf_accel_b2_host.c'),
        ('b2-mask8', 'hwdev/gf/accel_rop.c',
         'mask |= 0xffffff00U;',
         'mask |= 0xfffffe00U;', 'tools/tests/gf_accel_b2_host.c'),
        ('b2-pat-format', 'hwdev/gf/accel_rop.c',
         'value = format.pat;',
         'value = 3U;', 'tools/tests/gf_accel_b2_host.c'),
        ('b2-operation', 'hwdev/gf/accel_rop.c',
         '#define GF3_OPERATION_COPY    3U',
         '#define GF3_OPERATION_COPY    1U', 'tools/tests/gf_accel_b2_host.c'),
        ('b2-clear', 'hwdev/gf/accel_rop.c',
         '{0x00, 0x00}, {0x88, 0x8a}',
         '{0x00, 0x0a}, {0x88, 0x8a}', 'tools/tests/gf_accel_b2_host.c'),
        ('b2-mask16', 'hwdev/gf/accel_rop.c',
         'mask |= 0xffff0000U;',
         'mask |= 0U;', 'tools/tests/gf_accel_b2_host.c'),
    ],
    'b3': [
        ('fill_gdi_constant', 'hwdev/gf/accel_fill.c',
         'v[0] = fmt.gdi;',
         'v[0] = 3;', 'tools/tests/gf_accel_b3_host.c'),
        # Keep alu used under -Werror; the drawing operation still becomes copy.
        ('fill_alu_constant', 'hwdev/gf/accel_fill.c',
         'GF3_SUBC_GDI, alu, mask, s->fmt, dl',
         'GF3_SUBC_GDI, ((void)alu, 3), mask, s->fmt, dl', 'tools/tests/gf_accel_b3_host.c'),
        ('fill_point_order', 'hwdev/gf/accel_fill.c',
         '(r.x << 16) | r.y',
         '(r.y << 16) | r.x', 'tools/tests/gf_accel_b3_host.c'),
        ('fill_size_order', 'hwdev/gf/accel_fill.c',
         '(r.w << 16) | r.h',
         '(r.h << 16) | r.w', 'tools/tests/gf_accel_b3_host.c'),
        ('fill_format', 'hwdev/gf/accel_fill.c',
         'v[0] = fmt.gdi;',
         'v[0] = fmt.sf_gdi;', 'tools/tests/gf_accel_b3_host.c'),
        ('fill_wait', 'hwdev/gf/accel_fill.c',
         'rc = gf2_fence_wait(c->ch, fence, dl);',
         'rc = GF3_OK;', 'tools/tests/gf_accel_b3_host.c'),
    ],
    # B4 report2: packed coordinate/size anchors follow IMAGE_BLIT's 16-bit
    # fields (xf86-video-nouveau/src/nv04_exa.c:255-258).
    'b4': [
        ('b4_alias_off', 'hwdev/gf/accel_copy.c',
         'src->off != dst->off',
         'src->off == dst->off', 'tools/tests/gf_accel_b4_host.c'),
        ('b4_overlap_touch', 'hwdev/gf/accel_copy.c',
         'ap < be && bp < ae',
         'ap <= be && bp <= ae', 'tools/tests/gf_accel_b4_host.c'),
        ('b4_src_point', 'hwdev/gf/accel_copy.c',
         '(sr.y << 16) | sr.x',
         '(sr.x << 16) | sr.y', 'tools/tests/gf_accel_b4_host.c'),
        ('b4_dst_point', 'hwdev/gf/accel_copy.c',
         '(dy << 16) | dx',
         '(dx << 16) | dy', 'tools/tests/gf_accel_b4_host.c'),
        ('b4_size', 'hwdev/gf/accel_copy.c',
         '(sr.h << 16) | sr.w',
         '(sr.w << 16) | sr.h', 'tools/tests/gf_accel_b4_host.c'),
    ],
    'b5': [
        ('mono-cga6', 'hwdev/gf/accel_mono.c',
         '#define MONO_CGA6 1U',
         '#define MONO_CGA6 2U', 'tools/tests/gf_accel_b5_host.c'),
        ('mono-noninc', 'hwdev/gf/accel_mono.c',
         'MONO_DATA, false, c->scratch',
         'MONO_DATA, true, c->scratch', 'tools/tests/gf_accel_b5_host.c'),
        ('mono-tail-read', 'hwdev/gf/accel_mono.c',
         '&& row < r.h',
         '&& row <= r.h', 'tools/tests/gf_accel_b5_host.c'),
        ('mono-pitch', 'hwdev/gf/accel_mono.c',
         'row * bits->pitch + byte',
         'row * rowbytes + byte', 'tools/tests/gf_accel_b5_host.c'),
        ('mono-zero-tail', 'hwdev/gf/accel_mono.c',
         'u32 word = 0;',
         'u32 word = 0xffffffffU;', 'tools/tests/gf_accel_b5_host.c'),
        ('mono-exclusive', 'hwdev/gf/accel_mono.c',
         '(bottom << 16) | right;',
         '(bottom << 16) | (right - 1);', 'tools/tests/gf_accel_b5_host.c'),
        ('mono-short-batch', 'hwdev/gf/accel_mono.c',
         'words > GF3_MONO_BATCH_WORDS ? GF3_MONO_BATCH_WORDS : words',
         'words > GF3_MONO_BATCH_WORDS ? GF3_MONO_BATCH_WORDS - 1 : words', 'tools/tests/gf_accel_b5_host.c'),
    ],
    'b6': [
        ('ifc-size-out-padding', 'hwdev/gf/accel_ifc.c',
         'v[1] = (1U << IFC_FIELD_SHIFT) | r.w;',
         'v[1] = (1U << IFC_FIELD_SHIFT) | iw;', 'tools/tests/gf_accel_b6_host.c'),
        ('ifc-row-restart', 'hwdev/gf/accel_ifc.c',
         'v[0] = ((r.y + y) << IFC_FIELD_SHIFT) | r.x;',
         'v[0] = (r.y << IFC_FIELD_SHIFT) | r.x;', 'tools/tests/gf_accel_b6_host.c'),
        ('ifc-padding-nonzero', 'hwdev/gf/accel_ifc.c',
         'c->scratch[i] = 0;',
         'c->scratch[i] = 0xa5a5a5a5U;', 'tools/tests/gf_accel_b6_host.c'),
        ('ifc-sf-format', 'hwdev/gf/accel_ifc.c',
         'v[1] = fmt.ifc;',
         'v[1] = fmt.sf;', 'tools/tests/gf_accel_b6_host.c'),
        ('ifc-limit-exclusive', 'hwdev/gf/accel_ifc.c',
         'part.rowbytes > GF3_SCRATCH_WORDS * sizeof(u32)',
         'part.rowbytes >= GF3_SCRATCH_WORDS * sizeof(u32)', 'tools/tests/gf_accel_b6_host.c'),
        ('ifc-early-completion', 'hwdev/gf/accel_ifc.c',
         'if (!rc) rc = gf2_fence_wait(c->ch, fence, dl);',
         'if (!rc) rc = GF3_OK;', 'tools/tests/gf_accel_b6_host.c'),
        ('ifc-clip-point-size-swap', 'hwdev/gf/accel_ifc.c',
         'v[0] = (r.y << IFC_FIELD_SHIFT) | r.x;\n    v[1] = (r.h << IFC_FIELD_SHIFT) | r.w;',
         'v[0] = (r.h << IFC_FIELD_SHIFT) | r.w;\n    v[1] = (r.y << IFC_FIELD_SHIFT) | r.x;', 'tools/tests/gf_accel_b6_host.c'),
    ],
    'b7': [
        ('rows-2048', 'hwdev/gf/accel_m2mf.c',
         'u32 n = rows > GF3_M2MF_MAX_ROWS ? GF3_M2MF_MAX_ROWS : rows;',
         'u32 n = rows > 2048U ? 2048U : rows;', 'tools/tests/gf_accel_b7_host.c'),
        ('post-offset', 'hwdev/gf/accel_m2mf.c',
         'M2MF_OFFSET_OUT, false, &zero, 1, dl',
         'M2MF_NOP, false, &zero, 1, dl', 'tools/tests/gf_accel_b7_host.c'),
        ('skip-wait', 'hwdev/gf/accel_m2mf.c',
         'if (!rc) rc = gf2_fence_wait(ch, fence, dl);',
         'if (!rc) rc = GF3_OK;', 'tools/tests/gf_accel_b7_host.c'),
        ('out-stride', 'hwdev/gf/accel_m2mf.c',
         'out_off += out_pitch * n;',
         'out_off += in_pitch * n;', 'tools/tests/gf_accel_b7_host.c'),
        ('overlap', 'hwdev/gf/accel_m2mf.c',
         'return m2mf_result(c, GF3_EUNSUPPORTED);',
         '(void)0;', 'tools/tests/gf_accel_b7_host.c'),
    ],
    'b8': [
        ('staging-reuse', 'hwdev/gf/accel_transfer.c',
         'if (c->ch->staging_pending) {',
         'if (false) {', 'tools/tests/gf_accel_b8_host.c'),
        ('rows-cap', 'hwdev/gf/accel_transfer.c',
         'if (rows > GF3_M2MF_MAX_ROWS) rows = GF3_M2MF_MAX_ROWS;',
         'if (rows > GF3_M2MF_MAX_ROWS) rows = GF3_M2MF_MAX_ROWS + 1U;', 'tools/tests/gf_accel_b8_host.c'),
        ('ram-pitch', 'hwdev/gf/accel_transfer.c',
         'staging[j * bytes + b] = memory[(y + j) * cpu.pitch + x + b];',
         'staging[j * bytes + b] = memory[(y + j) * vr.pitch + x + b];', 'tools/tests/gf_accel_b8_host.c'),
        ('ifc-16k', 'hwdev/gf/accel_transfer.c',
         '(TRANSFER_IFC_BYTES - 1U) / span->rowbytes',
         'TRANSFER_IFC_BYTES / span->rowbytes', 'tools/tests/gf_accel_b8_host.c'),
        ('ifc-fallback', 'hwdev/gf/accel_transfer.c',
         'if (rc != GF3_EUNSUPPORTED) return transfer_result(c, rc);',
         'if (rc != GF3_EFORMAT) return transfer_result(c, rc);', 'tools/tests/gf_accel_b8_host.c'),
        ('deadline', 'hwdev/gf/accel_transfer.c',
         '0, off, bytes, vr.pitch, bytes, rows, dl);',
         '0, off, bytes, vr.pitch, bytes, rows, dl + GF3_TIMEOUT_US);', 'tools/tests/gf_accel_b8_host.c'),
    ],
    'b9': [
        ('scale_surface_src', 'hwdev/gf/accel_scale.c',
         'gf3_surface_set(c, dst, dst, false, dl)',
         'gf3_surface_set(c, src, dst, false, dl)', 'tools/tests/gf_accel_b9_host.c'),
        ('scale_wait_rc', 'hwdev/gf/accel_scale.c',
         'rc = gf2_fence_wait(c->ch, fence, dl);',
         'gf2_fence_wait(c->ch, fence, dl); rc = 0;', 'tools/tests/gf_accel_b9_host.c'),
        ('scale_filter', 'hwdev/gf/accel_scale.c',
         'bilinear ? SIFM_FILTER_BILINEAR : 0',
         'bilinear ? 0 : SIFM_FILTER_BILINEAR', 'tools/tests/gf_accel_b9_host.c'),
        ('scale_point', 'hwdev/gf/accel_scale.c',
         '((sr.y << 4) << 16) | (sr.x << 4)',
         '((sr.x << 4) << 16) | (sr.y << 4)', 'tools/tests/gf_accel_b9_host.c'),
        ('scale_ratio', 'hwdev/gf/accel_scale.c',
         'v[4] = du; v[5] = dv;',
         'v[4] = dv; v[5] = du;', 'tools/tests/gf_accel_b9_host.c'),
    ],
    'b10': [
        ('b10-force', 'drivers/video/geforce/gf_present_m2mf.c',
         'gf3_upload(c, scan, r, ram, true, dl)',
         'gf3_upload(c, scan, r, ram, false, dl)', 'tools/tests/gf_accel_b10_host.c'),
        ('b10-rc', 'drivers/video/geforce/gf_present_m2mf.c',
         'return gf3_upload(c, scan, r, ram, true, dl);',
         'return gf3_upload(c, scan, r, ram, true, dl), 0;', 'tools/tests/gf_accel_b10_host.c'),
    ],
    'v0': [
        ('v0-containment', 'hwdev/gf/vram_view.c',
         'bytes <= r->bytes - (off - r->off)',
         'bytes < r->bytes - (off - r->off)', 'tools/tests/gf_accel_v0_host.c'),
        ('v0-generation', 'hwdev/gf/vram_view.c',
         '.generation = node->value.generation + 1U',
         '.generation = node->value.generation + 2U', 'tools/tests/gf_accel_v0_host.c'),
        ('v0-owner', 'hwdev/gf/vram_view.c',
         '(owner && v->owner != owner)',
         '(owner && v->owner == owner)', 'tools/tests/gf_accel_v0_host.c'),
        ('v0-release-key', 'hwdev/gf/vram_view.c',
         'v->generation != key->generation',
         'v->generation + 1U != key->generation', 'tools/tests/gf_accel_v0_host.c'),
        ('v0-id-order', 'hwdev/gf/vram_view.c',
         'n->value.reservation_id < first->reservation_id',
         'n->value.reservation_id > first->reservation_id', 'tools/tests/gf_accel_v0_host.c'),
        ('v0-display-map', 'hwdev/gf/vram_view.c',
         'start < display_end',
         'start > display_end', 'tools/tests/gf_accel_v0_host.c'),
        ('v0-byte-offset', 'hwdev/gf/vram_view.c',
         '.va = base + (off - start)',
         '.va = base', 'tools/tests/gf_accel_v0_host.c'),
        ('v0-unmap-extent', 'hwdev/gf/vram_view.c',
         'view->map_base, view->map_bytes',
         'view->va, view->bytes', 'tools/tests/gf_accel_v0_host.c'),
        ('v0-close-required', 'hwdev/gf/vram_view.c',
         'view->mapped || !view->close ||',
         'view->mapped ||', 'tools/tests/gf_accel_v0_host.c'),
        ('v0-close-preserved', 'hwdev/gf/vram_view.c',
         '.close = view->close',
         '.close = 0', 'tools/tests/gf_accel_v0_host.c'),
        ('v0-close-owner-preserved', 'hwdev/gf/vram_view.c',
         '.close_owner = view->close_owner',
         '.close_owner = 0', 'tools/tests/gf_accel_v0_host.c'),
        ('v0-close-traverse', 'hwdev/gf/vram_view.c',
         'struct gf_vram_view *next = view->next;',
         'struct gf_vram_view *next = 0;', 'tools/tests/gf_accel_v0_host.c'),
        ('v0-close-error', 'hwdev/gf/vram_view.c',
         'if (rc < 0) return rc;',
         'if (rc > 0) return rc;', 'tools/tests/gf_accel_v0_host.c'),
    ],
    'c0': [
        ('fill-clip', 'drivers/video/geforce/gf_hook.c',
         'hook_scan.width, hook_scan.height))',
         'hook_scan.width-1, hook_scan.height))', 'tools/tests/gf_accel_c0_host.c'),
        ('copy-source-right', 'drivers/video/geforce/gf_hook.c',
         'w = hook_scan.width - (u32)sx;',
         'w = hook_scan.width - (u32)sx - 1;', 'tools/tests/gf_accel_c0_host.c'),
        ('copy-source-bottom', 'drivers/video/geforce/gf_hook.c',
         'h = hook_scan.height - (u32)sy;',
         'h = hook_scan.height - (u32)sy - 1;', 'tools/tests/gf_accel_c0_host.c'),
        ('negative-blit', 'drivers/video/geforce/gf_hook.c',
         'return OS32_ERR_INVAL;',
         'return 0;', 'tools/tests/gf_accel_c0_host.c'),
        ('deadline', 'drivers/video/geforce/gf_hook.c',
         '*dl = now + GF3_TIMEOUT_US;',
         '*dl = now;', 'tools/tests/gf_accel_c0_host.c'),
        ('admission', 'drivers/video/geforce/gf_hook.c',
         'hook_active = active && scan && scan->fmt == GF3_INDEX8;',
         'hook_active = ((void)active, scan && scan->fmt == GF3_INDEX8);', 'tools/tests/gf_accel_c0_host.c'),
        ('mask', 'drivers/video/geforce/gf_hook.c',
         '#define GF_HOOK_FULL_MASK 0xffU',
         '#define GF_HOOK_FULL_MASK 0x0fU', 'tools/tests/gf_accel_c0_host.c'),
        ('fill-rc', 'drivers/video/geforce/gf_hook.c',
         'return rc ? OS32_ERR_IO : 0;\n}\n\nint geforce_blit',
         'return rc ? 0 : 0;\n}\n\nint geforce_blit', 'tools/tests/gf_accel_c0_host.c'),
    ],
    'c1': [
        ('close-slot', 'hwdev/gf/service.c',
         '*s->slot = 0;\n        gf_hook_scan_bind(false, 0);',
         'gf_hook_scan_bind(false, 0);', 'tools/tests/gf_accel_c1_host.c'),
        ('diagnostic-release', 'hwdev/gf/service.c',
         'else if (!first) first = GF2_ESTOP;\n    return service_result(s, first);',
         'else if (!first) first = GF2_ESTOP;\n    if (!first) first = gf3_service_release(s);\n    return service_result(s, first);', 'tools/tests/gf_accel_c1_host.c'),
        ('recover-old-keys', 'hwdev/gf/service.c',
         'rc = gf3_service_release(s);\n    if (rc) return service_result(s, rc);\n    return service_publish(s, dl);',
         'rc = 0;\n    if (rc) return service_result(s, rc);\n    return service_publish(s, dl);', 'tools/tests/gf_accel_c1_host.c'),
        ('inst-unlink', 'hwdev/gf/instmem.c',
         'if (*link) { *link = obj->next; obj->next = 0; }',
         'if (*link) { (void)obj; }', 'tools/tests/gf_accel_c1_host.c'),
        ('c1-scan-offset', 'hwdev/gf/service.c',
         'u32 off = s->scan_phys - ch->bar1.base;',
         'u32 off = s->scan_phys + ch->bar1.base;', 'tools/tests/gf_accel_c1_host.c'),
        ('c1-page-alignment', 'hwdev/gf/service.c',
         'u32 mask = MEM_PAGE_SIZE - 1U;',
         'u32 mask = 63U;', 'tools/tests/gf_accel_c1_host.c'),
        ('c1-pitch-alignment', 'hwdev/gf/service.c',
         '#define GF3_TEST_PITCH_ALIGN 64U',
         '#define GF3_TEST_PITCH_ALIGN 32U', 'tools/tests/gf_accel_c1_host.c'),
        ('c1-height-cap', 'hwdev/gf/service.c',
         'if (height > s->scan_height) height = s->scan_height;',
         'if (height < s->scan_height) height = s->scan_height;', 'tools/tests/gf_accel_c1_host.c'),
        ('c1-stop-off', 'hwdev/gf/service.c',
         'if (p->chan.state == GF2_OFF) gf3_service_invalidate(s);',
         'if (p->chan.state != GF2_OFF) gf3_service_invalidate(s);', 'tools/tests/gf_accel_c1_host.c'),
        ('c1-recover-identity', 'hwdev/gf/service.c',
         'rc = gf3_service_release(s);',
         'rc = 0;', 'tools/tests/gf_accel_c1_host.c'),
        ('c1-deadline', 'hwdev/gf/service.c',
         'gf3_init(&p->accel, &p->chan, dl)',
         'gf3_init(&p->accel, &p->chan, dl + 1U)', 'tools/tests/gf_accel_c1_host.c'),
    ],
    'wire': [
        ('body-bind', 'drivers/video/geforce/geforce_body.c',
         'int rc=gf3_service_bind(&production, context->accel_out);',
         'int rc=false ? gf3_service_bind(&production, context->accel_out) : 0;',
         'tools/tests/gf_accel_wire_host.c'),
        ('production-stop', 'hwdev/gf/command_test.c',
         'service ? gf3_service_stop(service) : gf2_stop(ch)',
         'gf2_stop(ch)', 'tools/tests/gf_accel_wire_host.c'),
    ],
    'c2': [
        ('close-failure', 'hwdev/gf/display/quiesce.c',
         'if (!revoked) goto fail;',
         'if (false) goto fail;', 'tools/tests/gf_accel_c2_host.c'),
        ('release-gate', 'hwdev/gf/display/quiesce.c',
         'if (!rc && s && closed && ch->state==GF2_OFF && !f4_owned) {',
         'if (s && closed && ch->state==GF2_OFF && !f4_owned) {', 'tools/tests/gf_accel_c2_host.c'),
        ('engaged-retry', 'hwdev/gf/display/quiesce.c',
         'if (!rc && closed && !clock_failed && (!ch || ch->state==GF2_OFF)) c->engaged=false;',
         'if (closed && !clock_failed && (!ch || ch->state==GF2_OFF)) c->engaged=false;', 'tools/tests/gf_accel_c2_host.c'),
        ('invalidate-before-off', 'hwdev/gf/display/quiesce.c',
         'if (s && ch->state==GF2_OFF) gf3_service_invalidate(s);',
         'if (s) gf3_service_invalidate(s);', 'tools/tests/gf_accel_c2_host.c'),
        ('failed-view-drop', 'hwdev/gf/vram_view.c',
         'view->map_ops->unmap(view->map_owner, view->map_base, view->map_bytes))',
         '(view->map_ops->unmap(view->map_owner, view->map_base, view->map_bytes), false))', 'tools/tests/gf_accel_c2_host.c'),
        ('fini-fault', 'hwdev/gf/service.c',
         'if (!p->accel.ready || p->chan.state != GF2_RUN || p->chan.fault.latched ||\n        p->accel.epoch != p->chan.epoch) return 0;',
         'if (!p->accel.ready) return 0;', 'tools/tests/gf_accel_c2_host.c'),
    ],
}




def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def component_build(host, source, directory, original_parent):
    component = host.stem.removeprefix('gf_accel_').removesuffix('_host')
    if component in ('c1', 'c2', 'wire'):
        # Preserve relative includes for real F1/F2/F3 integration and mutations.
        tree = directory/'tree'
        if not tree.exists():
            (tree/'tools/tests').mkdir(parents=True, exist_ok=True)
            for rel in ('hwdev/gf', 'drivers/video/geforce', 'include'):
                shutil.copytree(ROOT/rel, tree/rel)
            # Unchanged neighboring headers keep their original locations.
            for parent in ('', 'hwdev', 'drivers', 'drivers/video'):
                target_parent = tree/parent
                for entry in (ROOT/parent).iterdir():
                    target = target_parent/entry.name
                    if not entry.name.startswith('.') and not target.exists():
                        target.symlink_to(entry, target_is_directory=entry.is_dir())
            (tree/'tools/tests').mkdir(parents=True, exist_ok=True)
            for pattern in ('gf_accel*.[ch]', 'gf_command_runtime_host.h'):
                for fixture in (ROOT/'tools/tests').glob(pattern):
                    shutil.copy2(fixture, tree/'tools/tests'/fixture.name)
        if component == 'wire':
            body = (tree/'drivers/video/geforce/geforce_body.c').read_text()
            (tree/'tools/tests/gf_ownerless_body.inc').write_text(
                ownerless_body(body, bind_f3=True))
        exe = directory/'host'
        subprocess.run(FLAGS + ['-Werror=vla', '-ffunction-sections', '-fdata-sections',
                       '-Wl,--gc-sections', *include_flags(ROOT), '-I'+str(tree), '-I'+str(ROOT),
                       str(tree/'tools/tests'/host.name), str(tree/'hwdev/gf/accel_copy.c'),
                       str(tree/'tools/tests/gf_accel_unbound_stubs.c'),
                       '-o', str(exe)], cwd=ROOT, check=True)
        return exe
    exe = directory/'host'
    # check=True: compilation/link errors are suite failures, never runtime RED.
    subprocess.run(FLAGS + ['-Werror=vla', *include_flags(ROOT), '-I'+str(ROOT),
                           '-iquote', str(original_parent),
                           str(host), str(source),
                           *([str(ROOT/'tools/tests/gf_accel_unbound_stubs.c')]
                             if component == 'c0' else []), '-o', str(exe)],
                   cwd=ROOT, check=True)
    return exe


def component_execute(exe, runners, label, green):
    for runner in runners:
        result = host32.run([str(exe)], runner=runner, cwd=ROOT)
        require((result.returncode == 0) == green,
                f'{label}: runner={runner} rc={result.returncode}, '
                f'expected runtime {"GREEN" if green else "RED"}')
        print(f'{label}: runtime {"GREEN" if green else "RED"} '
              f'(runner={runner}, rc={result.returncode})', flush=True)


def component_hosts(selected, mutate):
    runners = os.environ.get('HOST32_RUNNERS', 'qemu').split()
    require(bool(runners) and all(r in ('native', 'qemu') for r in runners),
            'HOST32_RUNNERS must select native and/or qemu')
    # Validate the whole selection before invoking the compiler. Missing
    # integrated products/hosts are failures, not silently skipped cases.
    for component in selected:
        source = ROOT/COMPONENTS[component]
        host = ROOT/f'tools/tests/gf_accel_{component}_host.c'
        require(source.is_file(), f'{component}: registered product missing: {source}')
        require(host.is_file(), f'{component}: registered host missing: {host}')
        if mutate:
            original = source.read_text()
            for name, product, old, new, red_host in MUTATIONS[component]:
                require((component in ('c1', 'c2', 'wire') or product == COMPONENTS[component]) and ROOT/red_host == host,
                        f'{component}-{name}: inconsistent registration')
                require(old != new and (ROOT/product).read_text().count(old) == 1,
                        f'{component}-{name}: anchor must be unique and change source')
    with tempfile.TemporaryDirectory(prefix='gf-accel-') as temporary:
        for component in selected:
            source = ROOT/COMPONENTS[component]
            host = ROOT/f'tools/tests/gf_accel_{component}_host.c'
            directory = Path(temporary)/component
            directory.mkdir()
            exe = component_build(host, source, directory, source.parent)
            component_execute(exe, runners, component+' control', True)
            if mutate:
                original = source.read_text()
                # Preserve include spelling: -iquote supplies the original parent,
                # while root-relative includes still use the normal include paths.
                mutant = directory/source.name
                for name, product, old, new, red_host in MUTATIONS[component]:
                    label = component+'-'+name
                    target = directory/'tree'/product if component in ('c1', 'c2', 'wire') else mutant
                    if component in ('c1', 'c2', 'wire'):
                        require(target.resolve().is_relative_to((directory/'tree').resolve()),
                                label+': mutation must stay in the copied product tree')
                    product_original = (ROOT/product).read_text()
                    target.write_text(product_original.replace(old, new, 1))
                    try:
                        exe = component_build(host, mutant, directory, source.parent)
                        component_execute(exe, runners, label, False)
                    finally:
                        target.write_text(product_original)
                        exe = component_build(host, mutant, directory, source.parent)
                        component_execute(exe, runners, label+' restored', True)


def consumer(source=None):
    spec = importlib.util.spec_from_file_location('gf_accel', ROOT/'tools/accept/gf_accel.py')
    module = importlib.util.module_from_spec(spec)
    if source is None:
        spec.loader.exec_module(module)
    else:
        exec(compile(source, str(spec.origin), 'exec'), module.__dict__)
    return module


def producer(mode, scenario=0, mutation=None, expected_exit=0):
    source = (ROOT/'drivers/video/geforce/gf_accept.c').read_text()
    if mutation:
        old, new = mutation
        assert source.count(old) == 1, old
        source = source.replace(old, new, 1)
    a = source.index('/* Read-only CPU primitives begin.')
    b = source.index('/* Read-only CPU primitives end. */')
    primitives = r'''
static u32 model_flags, model_wc, ticks, timing, calls;
static u32 flags(void) { return model_flags; }
static void set_flags(u32 v) { model_flags=v; }
static void cpuid(u32 leaf,u32 sub,u32 *a,u32 *b,u32 *c,u32 *d) {
    (void)sub; *a=*b=*c=*d=0;
    if (!leaf) { *a=4; *b=0x756e6547; *d=0x49656e69; *c=0x6c65746e; }
    if (leaf==1) { *a=0x652; *d=(1<<5)|(1<<12); }
    if (leaf==2) *a=0x0a060001;
    if (leaf==0x80000000) *a=0x80000000;
}
static unsigned long long msr(u32 reg) {
    if (reg==0xfe) return 0x402;
    if (reg==0x2ff) return 0x800;
    if (reg==0x200 && model_wc) return 0x30000001;
    if (reg==0x201 && model_wc) return 0xffff80800ULL;
    return 0;
}
'''
    source = source[:a] + primitives + source[b:]
    harness = r'''
#include "accept.c"
static u8 client[GF_MODE_VRAM_BYTES], display[GF_MODE_VRAM_BYTES];
static u8 model_pattern[GF_MODE_VRAM_BYTES] __attribute__((aligned(4096)));
static u32 registers[0x10020c/4+1];
static GfxCounters counters;
static struct gf_production_resources resources;
static struct geforce_platform platform;
static GeforceModuleContext context={.counters=&counters,.platform=&platform};
const GeforceModuleContext *geforce_context=&context;
static void die(int code) { __asm__ volatile("int $0x80" : : "a"(1),"b"(code) : "memory"); for (;;) {} }
static void report(const char *s) {
    if (timing) die(51);
    u32 n=0; while (s[n]) ++n;
    __asm__ volatile("int $0x80" : : "a"(4),"b"(1),"c"(s),"d"(n) : "memory");
}
int sys_time_now(u32 *lo,u32 *hi) {
    timing=!timing; ++calls; ticks+=10; *lo=ticks; *hi=1;
    return SCENARIO==2 && calls==9 ? -1 : 0;
}
u32 pgalloc_alloc_phys(u32 owner,int pages) { (void)owner; if(pages!=75) die(56); return V2P(model_pattern); }
int pgalloc_free_n_owner(u32 owner,u32 pfn,int pages) { (void)owner; if(pfn!=V2P(model_pattern)/PAGE_SIZE || pages!=75) die(57); return SCENARIO==5 ? 0 : 1; }
u32 paging_pte_raw(u32 addr) { return addr|PTE_PRESENT|PAGE_RW|PTE_PCD; }
u32 pci_cfg_read32(u32 b,u32 d,u32 f,u32 reg) { (void)b;(void)d;(void)f;(void)reg; return 0x6000; }
u16 pci_cfg_read16(u32 b,u32 d,u32 f,u32 reg) { (void)b;(void)d;(void)f;(void)reg; return 0x102; }
static int start(void) { if (SCENARIO==4) return 0; model_wc=1; return 1; }
static int stop(void) { model_wc=SCENARIO==1; return 0; }
static int point(void *v) { (void)v; return SCENARIO==3 && measured && points>1000; }
void _start(void) {
    platform.display_phys=0x30000000; platform.resources=&resources;
    platform.wc_start=start; platform.wc_stop=stop; platform.report=report;
    for (u32 i=0; i<sizeof(client); ++i) client[i]=0xa5;
    int first=gf_accept_transfer(client,640,display,640,(volatile u8 *)registers,0,0,640,480,point,0);
    if (first) die(52);
    int rc=gf_accept_transfer(client,640,display,640,(volatile u8 *)registers,1,1,32,32,point,0);
    if (SCENARIO==3 || SCENARIO==5) die(rc == -1 ? 0 : 53);
    if (rc) die(54);
    for (u32 i=0; i<sizeof(client); ++i) if (display[i]!=client[i]) die(55);
    if (GF_ACCEPT_MODE!=2 && SCENARIO==0) {
        model_wc=1; /* Ordinary backend startup can reacquire on GUI reentry. */
        if (gf_accept_transfer(client,640,display,640,(volatile u8 *)registers,0,0,1,1,point,0) || model_wc) die(58);
    }
    die(0);
}
'''
    with tempfile.TemporaryDirectory(prefix='gf-accept-') as directory:
        tmp = Path(directory)
        (tmp/'accept.c').write_text(source)
        (tmp/'host.c').write_text(harness)
        exe = tmp/'host'
        subprocess.run(FLAGS+(['-DGF_PRESENT_BYTE'] if mode == 0 else [])+include_flags(ROOT)+['-DOS32_GEFORCE_MODULE', f'-DGF_ACCEPT_MODE={mode}',
            '-DGF_ACCEPT_ID="fixture"', '-DGF_ACCEPT_PAIR=123', f'-DSCENARIO={scenario}', '-I'+str(ROOT/'drivers/video/geforce'),
            str(tmp/'host.c'), str(ROOT/'drivers/video/geforce/gf_present.c'), '-o', str(exe)], check=True)
        result = host32.run([str(exe)], capture_output=True, text=True, timeout=60)
        assert result.returncode == expected_exit, (mode, scenario, result.returncode, result.stderr)
        return result.stdout


def reject(module, text):
    try:
        module.parse_log(text, 'fixture')
    except ValueError:
        return
    raise AssertionError('invalid observation accepted')


def gui(mutation=None):
    text = (ROOT/'userland/gshell/src/wm.rs').read_text()
    start = text.index('// Acceptance timing begin.')
    module = text[start:text.index('// Acceptance timing end.')]
    if mutation:
        old, new = mutation
        assert module.count(old) == 1
        module = module.replace(old, new, 1)
    harness = r'''
extern crate self as os32api;
use std::sync::atomic::{AtomicU32,Ordering};
static TICK: AtomicU32=AtomicU32::new(0);
static OUTPUT: std::sync::Mutex<Vec<String>>=std::sync::Mutex::new(Vec::new());
struct Api { sys_time_now: unsafe fn(*mut u32,*mut u32)->i32, serial_puts: unsafe fn(*const u8) }
fn api()->&'static Api { &Api { sys_time_now: now,serial_puts: report } }
unsafe fn now(lo:*mut u32,hi:*mut u32)->i32 { *lo=TICK.fetch_add(10,Ordering::SeqCst); *hi=0; 0 }
unsafe fn report(s:*const u8) { OUTPUT.lock().unwrap().push(std::ffi::CStr::from_ptr(s.cast()).to_str().unwrap().into()); }
fn main() {
    for _ in 0..20 { gf_accept::begin(); gf_accept::event_done(); gf_accept::finish(); }
    for _ in 0..310 {
        gf_accept::begin(); gf_accept::event_done();
        let p=gf_accept::present_begin(); gf_accept::present_end(p); gf_accept::finish();
    }
    let output=OUTPUT.lock().unwrap(); assert_eq!(output.len(),300);
    for (i,line) in output.iter().enumerate() {
        let words:Vec<_>=line.split_whitespace().collect();
        assert_eq!(words[..4],["GFW1","fixture","byte-uc","sample"]);
        assert_eq!(words[4].parse::<usize>().unwrap(),i);
        assert_eq!(words[5..],["40","10","20","10"]);
        print!("{line}");
    }
}
'''
    with tempfile.TemporaryDirectory(prefix='gf-wm-') as directory:
        tmp = Path(directory); path = tmp/'host.rs'; exe = tmp/'host'
        path.write_text(module+harness)
        subprocess.run(['rustc', '--edition=2021', '--cfg', 'feature="gf_accel_accept"', str(path), '-o', str(exe)],
            check=True, env=dict(os.environ, GF_ACCEPT_ID='fixture', GF_ACCEPT_MODE='byte-uc'))
        result = subprocess.run([str(exe)], capture_output=True, text=True)
        assert result.returncode == (101 if mutation else 0), result.stderr
    print('RED GUI present phase' if mutation else 'PASS GUI: warmup, idle exclusion, 300 updates, phase sum, deferred serial')
    return result.stdout


def gui_paths(mutation=None, kinds=('commit', 'pump')):
    """Run the actual handler and Pump -> input -> cursor -> flush code."""
    harness = r'''
#[test]
fn acceptance_external_transfer() {
    use std::sync::atomic::{AtomicU32, Ordering};
    use os32api::gui::proto::GUI_OP_COMMIT;
    static TICK: AtomicU32 = AtomicU32::new(0);
    unsafe extern "C" fn now(lo: *mut u32, hi: *mut u32) -> i32 {
        *lo = TICK.fetch_add(10, Ordering::SeqCst); *hi = 0; 0
    }
    mocks::init();
    unsafe { (*os32api::api_ptr()).sys_time_now = now; }
    let shm = mocks::Shm::new();
    let st = wm::g();
    *st = wm::GuiState::NEW;
    st.inited = true; st.screen_w = 640; st.screen_h = 480;
    st.slots[0].used = true; st.slots[0].owner = 2;
    st.shm_base = shm.base();
    st.windows[0].used = true; st.windows[0].owner = 2;
    st.windows[0].x = 40; st.windows[0].y = 40;
    st.windows[0].w = 320; st.windows[0].h = 240;
    let pump = std::env::var("GFA_PATH").unwrap() == "pump";
    // No transfer, including guarded Pump calls and empty COMMITs, yields no sample.
    if pump {
        st.inited = false; pump::gshell_gui_pump(); st.inited = true;
        st.in_pump = true; pump::gshell_gui_pump(); st.in_pump = false;
    } else {
        assert_eq!(handler::gshell_gui_handler(GUI_OP_COMMIT, 0, 2), 0);
    }
    assert!(mocks::SERIAL_OUT.lock().unwrap().is_empty());
    for i in 0..310 {
        // App rendering / time between callbacks must not enter the sample.
        TICK.fetch_add(100_000, Ordering::SeqCst);
        if pump {
            *mocks::MOUSE.lock().unwrap() = (100 + (i % 2) as i16, 400, 0);
            pump::gshell_gui_pump();
            // The same pointer position is idle, with no further transfer.
            pump::gshell_gui_pump();
        } else {
            wm::g().windows[0].issued.push(wm::Rect::new(0, 0, 32, 32));
            assert_eq!(handler::gshell_gui_handler(GUI_OP_COMMIT, 0, 2), 0);
        }
        assert_eq!(mocks::PRESENTS.load(Ordering::SeqCst), i + 1);
        if i < 309 { assert!(mocks::SERIAL_OUT.lock().unwrap().is_empty()); }
    }
    let output = String::from_utf8(mocks::SERIAL_OUT.lock().unwrap().clone()).unwrap();
    assert_eq!(output.lines().count(), 300, "external transfer samples missing");
    for (i, line) in output.lines().enumerate() {
        let words: Vec<_> = line.split_whitespace().collect();
        assert_eq!(words[..4], ["GFW1", "fixture", "byte-uc", "sample"]);
        assert_eq!(words[4].parse::<usize>().unwrap(), i);
        assert_eq!(words[5..], ["10", "0", "0", "10"]);
    }
}
'''
    with tempfile.TemporaryDirectory(prefix='gf-path-') as directory:
        tmp = Path(directory)
        gshell = ROOT/'userland/gshell'
        src = gshell/'src'
        if mutation:
            shutil.copytree(src, tmp/'src')
            (tmp/'host').symlink_to(gshell/'host', target_is_directory=True)
            src = tmp/'src'
            path = src/'wm.rs'
            old, new = mutation
            text = path.read_text()
            assert text.count(old) == 1, old
            path.write_text(text.replace(old, new, 1))
        root = (src/'lib.rs').read_text().replace('#![no_std]', '#![allow(dead_code)]')
        root = root.replace('#[no_mangle]', '').replace('pub extern "C" fn main(', 'pub extern "C" fn guest_main(')
        root = re.sub(r'^mod (\w+);', lambda m: f'#[path="{src/(m[1]+".rs")}"] mod {m[1]};', root, flags=re.M)
        path = tmp/'root.rs'
        path.write_text(root+f'\n#[path="{gshell/"host/mocks.rs"}"] mod mocks;\n'+harness)
        rlib = os32api_host.build(tmp)
        font, deps = os32api_host.build_font()
        subprocess.run(['rustc', '--edition=2021', str(gshell/'build.rs'), '-o', str(tmp/'build-script')], check=True)
        env = dict(os.environ, OUT_DIR=str(tmp), CARGO_MANIFEST_DIR=str(gshell),
                   GF_ACCEPT_ID='fixture', GF_ACCEPT_MODE='byte-uc')
        subprocess.run([str(tmp/'build-script')], env=env, check=True, stdout=subprocess.DEVNULL)
        exe = tmp/'host-tests'
        built = subprocess.run(['rustc', '--edition=2021', '--test', '--cfg', 'feature="gf_accel_accept"',
                        str(path), '--extern', f'os32api={rlib}', '--extern', f'os32_font={font}',
                        '-L', f'dependency={deps}', '-o', str(exe)], env=env,
                       stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
        assert built.returncode == 0, built.stderr.decode()
        for kind in kinds:
            result = subprocess.run([str(exe), '--exact', 'acceptance_external_transfer', '--nocapture'],
                                    env=dict(env, GFA_PATH=kind), capture_output=True, text=True)
            assert result.returncode == (101 if mutation else 0), (kind, result.stdout, result.stderr)
            if mutation:
                assert 'external transfer samples missing' in result.stderr, result.stderr
            print(('RED' if mutation else 'PASS')+f' real {kind} path outside wm_cycle')


def acceptance_main():
    module = consumer()
    for count, p50, p95 in ((100, 50, 95), (300, 150, 285)):
        result = module.stats(list(range(1, count+1)))
        assert (result['p50'], result['p95']) == (p50, p95)
    assert module.stats([1, module.FAILED, 10])['failures'] == 1
    assert module.cache_lines([[2, 0, 0x0a060001, 0, 0, 0]]) == [32]
    assert module.cache_lines([[1, 0, 0, 0xff00, 0, 0]]) == 'unknown'
    logs = [producer(mode) for mode in range(3)]
    for mode, log in enumerate(logs):
        result = module.parse_log(log, 'fixture')
        assert result['status'] == 'OK'
        assert result['rectangles']['1']['pooled']['p95'] == 10
        assert result['rectangles']['1']['points'] == [48000]*3
        assert result['cpu_cache_line_bytes'] == [32]
        reject(module, log.replace('fixture', 'old'))
        reject(module, log.replace('GFA1 fixture '+module.MODES[mode]+' done OK', 'missing'))
    for mode in (0, 1):
        skipped = module.parse_log(producer(mode, 1), 'fixture')
        assert skipped['status'] == 'SKIP' and skipped['reason'] == 'bios-wc'
    assert module.parse_log(producer(2, 4), 'fixture')['status'] == 'SKIP'
    failed = module.parse_log(producer(0, 2), 'fixture')
    assert failed['status'] == 'FAIL' and failed['rectangles']['0']['pooled']['failures'] == 1
    reject(module, producer(0, 3))
    producer(0, 5)  # pgalloc_free_n_owner failure (0) must abort.
    fake_uc = logs[0].replace('state UC OK', 'state WC OK')
    reject(module, fake_uc)
    wm_log = gui()
    gui_paths()
    assert module.parse_log(logs[0]+wm_log, 'fixture', require_wm=True)['wm']['total']['p95'] == 40
    with tempfile.TemporaryDirectory(prefix='gf-cli-') as directory:
        paths = [Path(directory)/f'{mode}.log' for mode in range(3)]
        for path, log in zip(paths, logs):
            path.write_text(log)
        command = [sys.executable, '-B', str(ROOT/'tools/accept/gf_accel.py'), '--build-id', 'fixture',
                   '--pair-crc', '123', *map(str, paths)]
        assert subprocess.run(command, capture_output=True).returncode == 0
        assert subprocess.run(command+['--require-wm'], capture_output=True).returncode == 1
        paths[0].write_text(logs[0].replace('pair 123', 'pair 124'))
        assert subprocess.run(command, capture_output=True).returncode == 1
    print('PASS acceptance: real present, 3 modes, 10+300 samples, readback, BIOS SKIP, failed clock/point, version and ranks')
    if '--mutate' in sys.argv:
        original = (ROOT/'tools/accept/gf_accel.py').read_text()
        anchor = "status != 'OK' or effective != expected"
        assert original.count(anchor) == 1
        bad = consumer(original.replace(anchor, "status != 'OK'", 1))
        assert bad.parse_log(fake_uc, 'fixture')['effective'] == 'WC'
        print('RED effective type: mutated consumer accepts WC as UC')
        mutation = ('usable = original == 1;', 'usable = 1;')
        reject(module, producer(0, 1, mutation))
        print('RED BIOS WC: mutated producer emits WC measurements in UC group')
        gui(('s.present += end.saturating_sub(start);', 's.present += 0;'))
        producer(0, mutation=('if (rc || !freed) return -1;', 'if (rc || freed) return -1;'), expected_exit=54)
        print('RED release contract: successful free must restore CLIENT and return success')
        gui_paths(('let standalone = !state().active;', 'let standalone = false;'))
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mutate', action='store_true')
    parser.add_argument('--component', choices=(*COMPONENTS, 'acceptance'),
                        help='run one component, or only the existing acceptance tests')
    args = parser.parse_args()
    if args.component != 'acceptance':
        failures = []
        for component in ((args.component,) if args.component else tuple(COMPONENTS)):
            try:
                component_hosts((component,), args.mutate)
            except (RuntimeError, subprocess.CalledProcessError) as error:
                print(f'{component}: ERROR {error}', flush=True)
                failures.append(component)
        if failures:
            print('FAILED components: ' + ', '.join(failures), flush=True)
            return 1
    if args.component is None or args.component == 'acceptance':
        return acceptance_main()
    return 0


def run(ctx):
    argv = [sys.executable, '-B', str(ctx.root/'tools/tests/test_gf_accel.py')]
    if ctx.mutate:
        argv.append('--mutate')
    return subprocess.call(argv, cwd=ctx.root, env=dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners)))


if __name__ == '__main__':
    raise SystemExit(main())
