#!/usr/bin/env python3
"""G4 KEEP and DMA supplement against freestanding component fixtures."""
import argparse
from test_gf_accel import component_hosts

parser = argparse.ArgumentParser()
parser.add_argument('--mutate', action='store_true')
args = parser.parse_args()
component_hosts(('b3', 'b4', 'b9'), args.mutate)
