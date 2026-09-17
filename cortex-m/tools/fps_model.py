#!/usr/bin/env python3
"""Frame-rate model for h264bsd on Cortex-M33 from QEMU profiles.

    cycles/frame = CPU cycles (instruction mix x Cortex-M33 timing)
                 + external RAM stall cycles for the picture buffers

Inputs are the outputs of the two QEMU plugins in cortex-m/qemu/tools:
  <dir>/prof_<name>_insn.txt   from insn_profile.so  (instruction counts)
  <dir>/prof_<name>_mem32.txt  from mem_profile.so with line=32
  <dir>/prof_<name>_mem16.txt  from mem_profile.so with line=16
Produce them with `make -C cortex-m profile memprofile STREAM=<clip>`
(see the Makefile), then run:  fps_model.py <dir> <name> [<name> ...]

The Cortex-M33 timing and the external RAM latencies below are
assumptions (documented next to them); treat the output as an estimate
with roughly +/-15% on the CPU part.
"""
import re, sys, glob, os

S = sys.argv[1] if len(sys.argv) > 1 else '.'
FRAMES = int(os.environ.get('FRAMES', '73'))

# Cortex-M33 timing assumptions (in-order, single issue, 3-stage pipeline)
CYC_LOAD = 1.5      # LDR 2 cycles, back-to-back loads pipeline to ~1
CYC_STORE = 1.0     # write buffer
CYC_BRANCH = 2.0    # ~60% taken with 2-3 cycle refill, 40% not taken
CYC_OTHER = 1.0     # ALU, DSP, single-cycle multiplier

# External RAM scenarios for the picture buffers.
#   t_access: uncached random 4-byte read, ns   (core stalls for all of it)
#   t_store : uncached posted write, ns (bus occupancy, partly overlapped)
#   t_line32/16: cache line fill or write-back, ns
SCENARIOS = [
    ('internal SRAM (zero wait state)',          dict(t_access=0,   t_store=0,   t_line32=0,   t_line16=0)),
    ('16-bit SDRAM @100 MHz, 16 KB cache',       dict(cache=16384, t_line32=200, t_line16=130)),
    ('16-bit SDRAM @100 MHz, no cache',          dict(t_access=70,  t_store=50)),
    ('Octal-SPI PSRAM/HyperRAM 200 MHz DDR, 16 KB cache', dict(cache=16384, t_line32=200, t_line16=160)),
    ('Octal-SPI PSRAM/HyperRAM 200 MHz DDR, 32 KB cache', dict(cache=32768, t_line32=200, t_line16=160)),
    ('Octal-SPI PSRAM/HyperRAM, no cache',       dict(t_access=130, t_store=100)),
    ('Quad-SPI PSRAM 104 MHz SDR, 16 KB cache',  dict(cache=16384, t_line32=830, t_line16=520)),
    ('Quad-SPI PSRAM 104 MHz SDR, no cache',     dict(t_access=240, t_store=200)),
]
CLOCKS_MHZ = [160, 250, 300]

def parse_insn(path):
    txt = open(path).read()
    total = int(re.search(r'total (\d+) instructions', txt).group(1))
    main = 0
    m = re.search(r'^\s*(\d+)\s+[\d.]+%\s+\d+\s+main$', txt, re.M)
    if m: main = int(m.group(1))
    heap = int(re.search(r'heap used: (\d+)', txt).group(1))
    stack = int(re.search(r'stack used: (\d+)', txt).group(1))
    dpb = re.search(r'dpb pictures: (\d+) x (\d+)', txt)
    allocs = dict((m.group(1), int(m.group(2))) for m in re.finditer(r'^alloc (\w+) \S+ (\d+) bytes', txt, re.M))
    return dict(total=total, main=main, heap=heap, stack=stack, dpb_n=int(dpb.group(1)), dpb_bytes=int(dpb.group(2)), allocs=allocs)

def parse_mem(path):
    txt = open(path).read()
    mix = {}
    for cls in ('load', 'store', 'branch', 'multiply', 'other'):
        mix[cls] = int(re.search(r'^\s*%s\s+(\d+)' % cls, txt, re.M).group(1))
    mix['total'] = int(re.search(r'instructions: (\d+)', txt).group(1))
    ranges = {}
    cur = None
    for line in txt.splitlines():
        m = re.match(r'range (\w+)\s+loads: (\d+) \(b=(\d+) h=(\d+) w=(\d+) d=(\d+)\) (\d+) bytes; stores: (\d+) \(b=(\d+) h=(\d+) w=(\d+) d=(\d+)\) (\d+) bytes', line)
        if m:
            cur = m.group(1)
            ranges[cur] = dict(loads=int(m.group(2)), ld_bytes=int(m.group(7)), stores=int(m.group(8)), st_bytes=int(m.group(13)), cache={})
            continue
        m = re.match(r'\s*cache\s+(\d+) B (\d+)-way (\d+)B lines: misses (\d+) writebacks (\d+) \(of (\d+) accesses\)', line)
        if m and cur:
            ranges[cur]['cache'][int(m.group(1))] = dict(misses=int(m.group(4)), writebacks=int(m.group(5)), line=int(m.group(3)))
    return mix, ranges

def cpu_cycles(mix):
    return (mix['load'] * CYC_LOAD + mix['store'] * CYC_STORE + mix['branch'] * CYC_BRANCH +
            (mix['multiply'] + mix['other']) * CYC_OTHER)

def report(name):
    insn = parse_insn('%s/prof_%s_insn.txt' % (S, name))
    mix32, r32 = parse_mem('%s/prof_%s_mem32.txt' % (S, name))
    mix16, r16 = parse_mem('%s/prof_%s_mem16.txt' % (S, name))
    pics32, pics16 = r32['pictures'], r16['pictures']
    dec_insn = mix32['total'] / FRAMES
    cyc_cpu = cpu_cycles(mix32) / FRAMES
    print('=== %s ===' % name)
    print('decoder instructions/frame: %.2f M  (CPI model %.2f -> %.2f M cycles/frame)' % (dec_insn / 1e6, cyc_cpu / dec_insn, cyc_cpu / 1e6))
    print('mix: load %.1f%% store %.1f%% branch %.1f%% mul %.1f%% other %.1f%%' % tuple(100.0 * mix32[k] / mix32['total'] for k in ('load', 'store', 'branch', 'multiply', 'other')))
    print('picture buffer traffic/frame: %d loads (%.0f KB), %d stores (%.0f KB)' % (pics32['loads'] / FRAMES, pics32['ld_bytes'] / FRAMES / 1024, pics32['stores'] / FRAMES, pics32['st_bytes'] / FRAMES / 1024))
    for sz in (8192, 16384, 32768):
        c32, c16 = pics32['cache'][sz], pics16['cache'][sz]
        print('  %2d KB 2-way cache: 32B lines %6d misses %6d writebacks/frame (%.0f KB);  16B lines %6d misses %6d writebacks/frame (%.0f KB)' % (
            sz // 1024, c32['misses'] / FRAMES, c32['writebacks'] / FRAMES, (c32['misses'] + c32['writebacks']) * 32 / FRAMES / 1024,
            c16['misses'] / FRAMES, c16['writebacks'] / FRAMES, (c16['misses'] + c16['writebacks']) * 16 / FRAMES / 1024))
    print('memory: heap %.2f MB (%d pictures x %d KB + %s), stack %d B' % (insn['heap'] / 1e6, insn['dpb_n'], insn['dpb_bytes'] // 1024,
          ', '.join('%s %d KB' % (k, v // 1024) for k, v in insn['allocs'].items() if k not in ('pictures',)), insn['stack']))
    print('%-58s' % 'scenario' + ''.join('%9s' % ('%d MHz' % f) for f in CLOCKS_MHZ) + '   stall share')
    for label, p in SCENARIOS:
        row = []
        for f in CLOCKS_MHZ:
            if 'cache' in p:
                c = pics32['cache'][p['cache']]
                stall_ns = (c['misses'] + c['writebacks']) / FRAMES * p['t_line32']
            else:
                stall_ns = (pics32['loads'] / FRAMES * p['t_access'] + pics32['stores'] / FRAMES * p['t_store'])
            stall_cyc = stall_ns * f / 1000.0
            cyc = cyc_cpu + stall_cyc
            row.append((f * 1e6 / cyc, stall_cyc / cyc))
        print('%-58s' % label + ''.join('%9.1f' % fps for fps, _ in row) + '   %4.0f%%' % (100 * row[0][1]))
    print()

if __name__ == '__main__':
    for n in sys.argv[2:]:
        report(n)
