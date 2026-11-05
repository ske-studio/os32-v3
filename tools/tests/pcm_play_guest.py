#!/usr/bin/env python3
"""Compare a capture of NP21/W PCM frames with a test WAV's data.

Generate a WAV with make_pcm_wav.py, copy it to HostDrv and play it with
pcm_play. During playback save GET /api/sound?pcm=1 as JSON (the API keeps
approximately the most recent second). Then run:
    python3 tools/tests/pcm_play_guest.py test.wav sound.json --min-frames 22050

The longest contiguous match is measured in complete stereo frames; either
input may have a leading offset. The exit status is zero only if the match
reaches the specified threshold. Capture and guest execution are done by PM.
"""
import argparse
import base64
import json
import struct
import wave


def longest_match_frames(wav_data, capture):
    """Return the longest contiguous sequence of equal four-byte frames."""
    if len(wav_data) % 4 or len(capture) % 4:
        raise ValueError('PCM data must contain complete four-byte frames')
    # Suffix automaton over WAV frames: linear time even for periodic audio.
    transitions = [{}]
    links = [-1]
    lengths = [0]
    last = 0
    for (frame,) in struct.iter_unpack('<I', wav_data):
        current = len(links)
        transitions.append({})
        lengths.append(lengths[last] + 1)
        links.append(0)
        p = last
        while p >= 0 and frame not in transitions[p]:
            transitions[p][frame] = current
            p = links[p]
        if p >= 0:
            q = transitions[p][frame]
            if lengths[p] + 1 == lengths[q]:
                links[current] = q
            else:
                clone = len(links)
                transitions.append(transitions[q].copy())
                lengths.append(lengths[p] + 1)
                links.append(links[q])
                while p >= 0 and transitions[p].get(frame) == q:
                    transitions[p][frame] = clone
                    p = links[p]
                links[q] = links[current] = clone
        last = current
    state = matched = best = 0
    for (frame,) in struct.iter_unpack('<I', capture):
        while state and frame not in transitions[state]:
            state = links[state]
            matched = lengths[state]
        if frame in transitions[state]:
            state = transitions[state][frame]
            matched += 1
            best = max(best, matched)
        else:
            matched = 0
    return best


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('wav')
    parser.add_argument('capture_json')
    parser.add_argument('--min-frames', type=int, default=22050)
    args = parser.parse_args()
    if args.min_frames <= 0:
        parser.error('--min-frames must be positive')
    with wave.open(args.wav, 'rb') as wav:
        if (wav.getnchannels(), wav.getsampwidth(), wav.getframerate(),
                wav.getcomptype()) != (2, 2, 44100, 'NONE'):
            parser.error('WAV must be 44.1 kHz 16-bit stereo PCM')
        data = wav.readframes(wav.getnframes())
    with open(args.capture_json, encoding='utf-8') as source:
        reply = json.load(source)
    if not reply.get('ok') or reply.get('rate') != 44100:
        parser.error('capture failed or has a rate other than 44100 Hz')
    capture = base64.b64decode(reply['pcm'], validate=True)
    if len(capture) != reply['frames'] * 4:
        parser.error('capture byte count does not match frame count')
    matched = longest_match_frames(data, capture)
    print(f'longest match: {matched} frames; required: {args.min_frames}')
    return 0 if matched >= args.min_frames else 1


if __name__ == '__main__':
    raise SystemExit(main())
