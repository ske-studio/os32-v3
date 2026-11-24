#!/usr/bin/env python3
"""Register the private sound PIC target for the SDK's per-unit stamp wrapper."""
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'sdk'))
import rustc_stamp
import target_profile

if __name__ == '__main__':
    target_profile.PROFILES['x86-pc98-snd'] = dict(
        target_profile.PROFILES['x86-pc98'], rust='rust/i386-os32-snd.json')
    sys.exit(rustc_stamp.main())
