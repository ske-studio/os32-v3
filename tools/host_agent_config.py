"""Validation and instance separation for host-agent JSON configuration.

Endpoints use the existing host:port syntax.  This module performs no I/O
other than loading a configuration file; service lifecycle belongs to U5b.
"""
import ipaddress
import json
import os
import re


BACKENDS = ('np21w', 'bridge')
_FIELDS = {'iface', 'root', 'allow-write', 'peer', 'backend', 'frame-endpoint',
           'operator-endpoint', 'state-dir'}
_MAC = re.compile(r'^[0-9a-fA-F]{2}(?::[0-9a-fA-F]{2}){5}$')


def _text(value, field):
    if not isinstance(value, str) or not value or '\x00' in value:
        raise ValueError(f'{field} must be a nonempty string')
    return value


def _endpoint(value, field):
    value = _text(value, field)
    host, sep, port = value.rpartition(':')
    if not sep or not host or not port.isascii() or not port.isdecimal():
        raise ValueError(f'{field} must be host:port')
    if ':' in host or not 1 <= int(port) <= 65535:
        raise ValueError(f'{field} must be an IPv4 host:port')
    return host.lower(), int(port)


def validate_config(data):
    """Return a validated copy, including the default write policy."""
    if not isinstance(data, dict) or set(data) - _FIELDS:
        raise ValueError('configuration must contain only known fields')
    missing = _FIELDS - {'allow-write'} - set(data)
    if missing:
        raise ValueError('missing fields: ' + ', '.join(sorted(missing)))
    config = dict(data)
    for field in ('iface', 'root', 'peer', 'backend', 'state-dir'):
        _text(config[field], field)
    # PM 2026-11-14: np21w = NP21/W の NP2NETSOCK が frame-endpoint に接続しに来る、
    # bridge = lan_bridge.py が iface の物理 NIC と frame-endpoint を中継する (LAN_OPERATOR_DESIGN §2)。
    if config['backend'] not in BACKENDS:
        raise ValueError('backend must be one of: ' + ', '.join(BACKENDS))
    if not os.path.isabs(config['root']):
        raise ValueError('root must be an absolute path')
    config['allow-write'] = config.get('allow-write', False)
    if not isinstance(config['allow-write'], bool):
        raise ValueError('allow-write must be boolean')
    if not _MAC.fullmatch(config['peer']):
        raise ValueError('peer must be a MAC address')
    _endpoint(config['frame-endpoint'], 'frame-endpoint')
    _endpoint(config['operator-endpoint'], 'operator-endpoint')
    return config


def load_config(path):
    with open(path, encoding='utf-8') as stream:
        return validate_config(json.load(stream))


def config_summary(config):
    """Return the config portion of an operator probe response."""
    config = validate_config(config)
    return dict(iface=config['iface'], root=config['root'],
                allow_write=config['allow-write'], peer=config['peer'])


def _overlap(a, b):
    host_a, port_a = a
    host_b, port_b = b
    if port_a != port_b:
        return False
    def normalize(host):
        try:
            return str(ipaddress.IPv4Address(host))
        except ipaddress.AddressValueError:
            return host
    host_a, host_b = normalize(host_a), normalize(host_b)
    return host_a == host_b or '0.0.0.0' in (host_a, host_b)


def validate_instances(configs):
    """Reject duplicate NICs, state directories, and conflicting TCP binds."""
    configs = [validate_config(config) for config in configs]
    endpoints = []
    ifaces, directories = set(), set()
    for config in configs:
        iface = config['iface']
        directory = os.path.realpath(config['state-dir'])
        if iface in ifaces or directory in directories:
            raise ValueError('instance iface or state-dir collision')
        ifaces.add(iface)
        directories.add(directory)
        for field in ('frame-endpoint', 'operator-endpoint'):
            endpoint = _endpoint(config[field], field)
            if any(_overlap(endpoint, existing) for existing in endpoints):
                raise ValueError('instance endpoint collision')
            endpoints.append(endpoint)
    return configs
