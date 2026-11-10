#!/usr/bin/env python3
# SwitchRecomp - Copyright 2026 MZ Labs
# Licensed under the GNU Affero General Public License v3.0, see LICENSE.txt and NOTICE.txt.
# Forks and derived works have to keep the credit to SwitchRecomp by MZ Labs and the startup screen.
"""nxs_live: shaders translated while the game runs (renderer=vulkan), with no build step before.

The runtime starts this once. The NVN layer (gfx/nxs_rt.c) writes each shader it has no translation for into the dump
folder (<key>.ctl, <key>.code) and looks for <key> in the cache twice a second. This loop picks up new dumps, translates
every one found in a batch with one run of nxnative's translator (dmsh_xlate: Ryujinx's shader translator, which reads
Switch 2 Ampere code itself), applies the same edits and reflection as nxshader.py and publishes into the cache. The game
never waits: a draw whose shader is not there yet is skipped until it arrives.

  nxs_live.py --dump DIR --cache DIR [--xlate DIR] [--dotnet dotnet] [--facts shader_facts.tsv] [--parent PID]"""
import argparse, json, os, shutil, struct, sys, tempfile, time, traceback

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import nxs_front as F, nxs_cache as C, nxshader as N

SPH_STAGE = {1: 'vs', 2: 'hs', 3: 'ds', 4: 'gs', 5: 'ps'}


def stage_of(ctl, code):
    """nxnative shader_split.py nvn2_stage / sph_stage (MIT): compute, task and mesh by the control's stage byte (+0x56),
    else the graphics SPH word 0 of the code area"""
    if F.dmsh_ampere.is_nvn2(ctl) and len(ctl) > 0x56 and ctl[0x56] in (5, 6, 7): return {5: 'cs', 6: 'ts', 7: 'ms'}[ctl[0x56]]
    if len(code) < 4: return None
    w = struct.unpack_from('<I', code, 0)[0] if F.dmsh_ampere.is_nvn2(ctl) else (struct.unpack_from('<I', code, 0x30)[0] if len(code) >= 0x34 else 0)
    if (w >> 10) & 0xf == 0 and w & 0x1f == 1: return 'vs'
    return SPH_STAGE.get((w >> 10) & 0xf) or ('cs' if not F.dmsh_ampere.is_nvn2(ctl) and code[:4] == bytes.fromhex('79563412') else None)


class DumpCorpus:
    """nxs_front.Corpus over the runtime's dumps: a record is 0x38 header bytes, the code area, the control section"""
    def __init__(self, dump, facts_path=None):
        self.dump = dump
        self.facts_raw, self.facts, self.source = {}, {}, {}
        if facts_path and os.path.isfile(facts_path):
            for l in open(facts_path, encoding='utf-8'):
                if l.strip() and not l.startswith('#'):
                    k, v = l.rstrip('\r\n').split('\t', 1)
                    self.facts_raw[k] = v; self.facts[k] = json.loads(v)
        self.stage = {}

    def record(self, key):
        ctl = open(os.path.join(self.dump, key + '.ctl'), 'rb').read()
        code = open(os.path.join(self.dump, key + '.code'), 'rb').read()
        if F.dmsh_ampere.is_nvn2(ctl): ctl = ctl[:0xe0]
        st = stage_of(ctl, code)
        if not st: raise ValueError('a shader of no known stage')
        self.stage[key] = st
        return st + '_live.bin', bytes(0x38) + code + ctl


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--dump', required=True); ap.add_argument('--cache', required=True)
    ap.add_argument('--xlate', default=os.path.join(HERE, 'xlate_bin')); ap.add_argument('--dotnet', default='dotnet')
    ap.add_argument('--facts'); ap.add_argument('--parent', type=int, default=0); ap.add_argument('--batch', type=int, default=48)
    a = ap.parse_args()
    os.makedirs(a.dump, exist_ok=True); os.makedirs(os.path.join(a.cache, 'vk'), exist_ok=True)
    log = open(os.path.join(a.cache, 'nxs_live.log'), 'a', buffering=1)
    say = lambda s: log.write(time.strftime('%H:%M:%S ') + s + '\n')
    try: xl = F.Translator(a.xlate, a.dotnet)
    except Exception as e: say('no translator: %s (.NET 10 runtime installed?)' % e); return 1
    cache = C.Cache(a.cache)
    w = tempfile.mkdtemp(prefix='nxs_live_')
    b = N.Builder(DumpCorpus(a.dump, a.facts), cache, xl, w, validate=False)
    cache.set_chain(b.chain)
    say('ready: translator %s, chain %s, dumps %s' % (xl.id, b.chain, a.dump))
    done = set(k[:-5] for k in os.listdir(os.path.join(a.cache, 'vk')) if k.endswith('.json'))
    failed = set()
    try:
        while True:
            if a.parent:
                try: os.kill(a.parent, 0)
                except OSError: break
            keys = sorted(f[:-4] for f in os.listdir(a.dump) if f.endswith('.ctl') and len(f) == 20)
            todo = [k for k in keys if k not in done and k not in failed and os.path.exists(os.path.join(a.dump, k + '.code'))]
            if not todo: time.sleep(0.05); continue
            time.sleep(0.03)   # the shaders of one pipeline arrive together: one translator run for them
            for i in range(0, len(todo), a.batch):
                part = todo[i:i + a.batch]; t0 = time.time()
                try: out = b.make(part)
                except Exception: say(traceback.format_exc()); out = {k: {'status': 'failed'} for k in part}
                ok = [k for k, r in out.items() if r.get('status') == 'ok']
                for k, r in out.items():
                    if r.get('status') == 'ok': done.add(k)
                    else: failed.add(k); say('%s: %s %s' % (k, r.get('status'), r.get('error', '')))
                say('%d translated in %.2f s (%d failed)' % (len(ok), time.time() - t0, len(part) - len(ok)))
    finally: shutil.rmtree(w, ignore_errors=True)
    return 0


if __name__ == '__main__':
    sys.exit(main())
