#!/usr/bin/env python3
"""End to end checks of the runner against ui_shot --serve that a script cannot express (host tool, NOT firmware):
   the runtime gate, a slow link, a reboot, the project open, and the serial code path through a pty.
   python3 -I e2e_extra.py UI_SHOT_BINARY WORK_DIR"""
import os
import shutil
import subprocess
import sys
import threading
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import usbtest  # noqa: E402
from usbtest import SerialTransport, SimTransport, Timeout, Tracker, UsbTestError  # noqa: E402

UI, WORK = sys.argv[1], sys.argv[2]
failed = []
os.makedirs(WORK, exist_ok=True)


def check(name, cond, info=''):
    print('%-5s %s %s' % ('ok' if cond else 'FAIL', name, info), flush=True)
    if not cond:
        failed.append(name)


def fresh(name):
    d = os.path.join(WORK, name)
    shutil.rmtree(d, ignore_errors=True)
    return d


# ---- runtime gate: without /.develop the console is silent
sim = SimTransport(UI, fresh('sd_gate'), ['--no-develop'])
try:
    Tracker(sim).ping(1.0)
    check('gate: silent without /.develop', False)
except Timeout:
    check('gate: silent without /.develop', True)
finally:
    sim.close()

# ---- slow link: 64 bytes per 5 ms, the screenshot still arrives whole and the crc matches
sim = SimTransport(UI, fresh('sd_slow'), ['--tx-rate', '64'])
tr = Tracker(sim)
tr.ping()
rows = tr.shot(os.path.join(WORK, 'slow.png'))
check('slow link: shot decodes, image crc matches', len(rows) == 480)
sim.close()

# ---- reboot: the process ends with 77, the runner reconnects and pings
sim = SimTransport(UI, fresh('sd_reboot'))
tr = Tracker(sim)
tr.ping()
tr.key('Master')
tr.idle()
before = tr.status()['name']
info = tr.reboot()
after = tr.status()['name']
check('reboot: reconnects and answers', info['product'] == 'original', '(%s -> %s)' % (before, after))
check('reboot: boots to the start page again', after == 'pattern')
sim.close()

# ---- project open: copy the workspace to /Projects/Demo, open it, play and stop
sd = fresh('sd_proj')
sim = SimTransport(UI, sd)
sim.close()
shutil.copytree(os.path.join(sd, 'Workspace'), os.path.join(sd, 'Projects', 'Demo'))
sim = SimTransport(UI, sd)
tr = Tracker(sim)
tr.ping()
tr.open_project('Projects', 'Demo', force=True)
tr.idle()
check('open: project loaded, pattern page', tr.status()['name'] == 'pattern')
h1 = tr.hash_project()
tr.command('play song')
sim.wait_ms(400)
check('play: sequencer running', tr.status()['play'] == '1')
tr.command('stop')
sim.wait_ms(200)
check('stop: sequencer stopped', tr.status()['play'] == '0')
term, _, _ = tr.command('open Projects Nope force', expect_err=True)
check('open: a missing project fails cleanly', term.startswith('@err'), term)
sim.close()

# ---- the serial code path: ui_shot behind a pty, SerialTransport on the slave side (termios, no pyserial)
master, slave = os.openpty()
port = os.ttyname(slave)
proc = subprocess.Popen([UI, '--serve', fresh('sd_pty')], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, bufsize=0)


def pump_out():
    while True:
        d = os.read(proc.stdout.fileno(), 65536)
        if not d:
            return
        os.write(master, d)


def pump_in():
    while True:
        try:
            d = os.read(master, 65536)
        except OSError:
            return
        if not d:
            return
        proc.stdin.write(d)


threading.Thread(target=pump_out, daemon=True).start()
threading.Thread(target=pump_in, daemon=True).start()
os.environ['USBTEST_PORT'] = port
check('serial: USBTEST_PORT is found', SerialTransport.find_port() == port)
ser = SerialTransport(port)
tr = Tracker(ser, heartbeat=0.2)
ser.drain(0.5)
for attempt in range(40):   # the sim needs a few seconds to boot
    try:
        tr.ping(2.0)
        break
    except UsbTestError:
        time.sleep(0.5)
st = tr.status()
check('serial: ping and status over the tty', st['name'] == 'pattern')
tr.key('Master')
tr.idle()
check('serial: key and idle', tr.status()['name'] == 'master')
tiles = tr.shotcrc()
check('serial: shotcrc 48 tiles', len(tiles) == 48)
rows = tr.shot(os.path.join(WORK, 'serial.png'))
check('serial: shot over the tty, image crc matches', len(rows) == 480)
tr.wait(300)   # real sleep with a heartbeat
check('serial: heartbeat wait', True)
ser.close()
proc.stdin.close()
proc.wait(timeout=10)

print('%d failed' % len(failed))
sys.exit(1 if failed else 0)
