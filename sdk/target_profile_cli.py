#!/usr/bin/env python3
"""Make-facing query of sdk/target_profile.py.

  target_profile_cli.py --target T --require CAPABILITY
  target_profile_cli.py --target T --make [--root DIR]

--make prints one NAME=value token per SDK field (list items joined by commas,
paths prefixed with DIR). A refusal (unsupported or unknown target) prints
nothing on stdout, one line on stderr, and exits REFUSED -- before any other
work. Any other non-zero status is a broken query, not a refusal: an incomplete
profile, an unprintable value or an unknown capability exits BROKEN.
"""
import argparse
import sys

import target_profile

MAKE_NAMES = dict(call_abi='SDK_TARGET_CALL_ABI', elf_emulation='SDK_TARGET_ELF_EMULATION',
                  crt_user='SDK_TARGET_CRT_USER', crt_resident='SDK_TARGET_CRT_RESIDENT',
                  app_link='SDK_TARGET_APP_LINK', app_sys_link='SDK_TARGET_APP_SYS_LINK',
                  shlib_link='SDK_TARGET_SHLIB_LINK', rust='SDK_TARGET_RUST')
REFUSED = 3
BROKEN = 2
PATH_FIELDS = ('crt_user', 'crt_resident', 'app_link', 'app_sys_link', 'shlib_link', 'rust')


def make_tokens(target, root):
    values = target_profile.sdk_values(target)
    prefix = root.rstrip('/') + '/' if root else ''
    tokens = []
    for field in target_profile.SDK_FIELDS:
        value = values[field]
        items = list(value) if isinstance(value, (tuple, list)) else [value]
        if field in PATH_FIELDS:
            items = [prefix + item for item in items]
        text = ','.join(items)
        if any(c in text for c in ' =$#\n\t') or not text:
            raise ValueError('unprintable sdk value ' + field)
        tokens.append(MAKE_NAMES[field] + '=' + text)
    return tokens


def main(argv=None):
    parser = argparse.ArgumentParser()
    parser.add_argument('--target', required=True)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument('--require')
    mode.add_argument('--make', action='store_true')
    parser.add_argument('--root', default='')
    args = parser.parse_args(argv)
    try:
        if args.require:
            if args.require not in target_profile.MESSAGES:
                raise ValueError('unknown capability ' + args.require)
            target_profile.require(args.target, args.require)
            return 0
        tokens = make_tokens(args.target, args.root)
    except target_profile.Unsupported as error:
        print(error, file=sys.stderr)
        return REFUSED
    except ValueError as error:
        print(error, file=sys.stderr)
        return BROKEN
    print(' '.join(tokens))
    return 0


if __name__ == '__main__':
    sys.exit(main())
