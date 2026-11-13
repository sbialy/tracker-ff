#!/usr/bin/env python3 -I
"""fw_engine_mismatch_sim.sh helper: adds the "poke picker" action to the ui_shot host of an older tree (release-1.2.0).

Usage: fw_mismatch_probe12.py TREE. The 1.2.0 host_poke.cpp has no "picker" action (the 1.3.0 one prints the Model list the Synth
screen holds and the engine every synth part stores) and no "inserts" action (the state of both master insert slots). This inserts the
same output lines, without the 1.3.0 hide/order code, in front of the "unknown command" branch of TREE/synth-open/tools/ui_shot/host_poke.cpp. Only the host test tool changes,
never the firmware sources. Idempotent. The 1.2.0 tree is a git archive in build/, nothing in the repository is touched.
"""
import os, sys

p = os.path.join(sys.argv[1], 'synth-open/tools/ui_shot/host_poke.cpp')
t = open(p, encoding='latin-1', newline='').read()
if 'fw_mismatch_probe' in t:
    sys.exit(0)
nl = '\r\n' if '\r\n' in t else '\n'
add = '''    else if (!strcmp(cmd, "inserts"))   // fw_mismatch_probe12.py: the 1.3.0 line without the parameter values
    {
        static const char* const kState[] = { "empty", "pending", "running", "missing", "failed", "safe", "tripped", "bypassed", "noroom" };
        out("inserts T=0 1=%s:%s[] 2=%s:%s[]", kState[int(plug::slotState(0))], plug::slotName(0), kState[int(plug::slotState(1))],
            plug::slotName(1));
    }
    else if (!strcmp(cmd, "picker"))   // fw_mismatch_probe12.py: the same two kinds of line as the 1.3.0 host_poke.cpp
    {
        const strNewListData& d = instrumentEditor.modelListData;
        std::string rows;
        for (int i = 0; i < d.length; i++)
        {
            rows += i ? "," : "";
            rows += d.params && d.params[i] ? d.params[i] : "?";
        }
        out("picker rows %d cursor %d list %s", (int)d.length, (int)d.start, rows.c_str());
        for (int p = 0; p < 3; p++)
        {
            const int st = mtProject.values.synthPart[p].synthType;
            out("part %d stored %d engine %s", p, st, Synth::storedName(st));
        }
    }
'''.replace('\n', nl)
needle = '    else' + nl + '        out("unknown command'
assert needle in t, 'host_poke.cpp: the unknown command branch moved'
t = t.replace(needle, add + needle, 1)
if '#include "plug_host.h"' not in t:
    t = t.replace('#include "mtStructs.h"', '#include "mtStructs.h"' + nl + '#include "plug_host.h"', 1)
if '#include "Synth.h"' not in t:
    t = t.replace('#include "mtStructs.h"', '#include "mtStructs.h"' + nl + '#include "Synth.h"', 1)
open(p, 'w', encoding='latin-1', newline='').write(t)
