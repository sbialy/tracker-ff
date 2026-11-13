// Order and visibility of the engines in the model list of the Synth screen (core/interface/instrumentEditor/SynthEditor.cpp).
// Pure logic, nothing of the firmware in here, so the host test (synth-open/tools/synth_order_test.cpp) runs it on the Mac.
//
// The list the UI shows is only a view. The engine list of the synth (open_engine.h: ui index, stored number) is not touched, a
// project still stores the stored number and a .patch file the engine id, so ordering and hiding change what the picker shows
// and nothing that is saved in a project.
//
// The state is the order file /Plugins/synth-order.txt, one engine name per line, top to bottom is the order on screen, a leading
// "-" hides the engine, "#" starts a comment:
//
//     # Synth list ...
//     ACD
//     FAT
//     -PERC
//     JU6
//
// Rules (the engines are those of the synth now, built in and plugins from /Plugins/Synth):
//  - an engine is found by its name, ignoring case. A line whose engine is not there (plugin file gone) is ignored by the view and
//    kept in the file, so the entry is back when the plugin is. Two lines of one name: the first counts.
//  - an engine without a line (a new .plg on the card) is shown, after the listed ones, in the order of the engine list
//  - a hidden engine that a part of the open project uses is shown anyway, at the top, before the user ordered entries
//  - a line that is not printable text is skipped, a file that is no text at all gives the default order (everything shown)
//  - the view is never empty: when every engine is hidden and none is in use, all of them are shown
#pragma once

#include <stdint.h>
#include <string.h>

namespace synthorder
{
constexpr int kMaxEntries = 512;   // lines of the file that count (the engines are at most 256)
constexpr int kNameMax = 24;       // with the terminator, a longer name is cut
constexpr int kViewMax = 256;      // engines in the list (open_engine.h: one stored byte each)

// The room a row of the edit list has, in pixels of the list's font (font 1). The Model list is a NewList of 800 / 8 - 3 = 97 px: its
// text clip starts 13 px in and ends 7 px before the right edge, which is itself 9 px in from the control when the scrollbar is up
// (more than 7 rows, always the case in the edit mode with a plugin library) = 97 - 9 - 13 - 7 = 68 px. The text is centred, so a
// row wider than this loses its first and last glyphs: the "-" first.
constexpr int kRowPx = 68;
constexpr int kRowMarginPx = 4;   // kept free in the row (the advance of a glyph is wider than its ink, the "-" is the outermost)

struct Entry
{
    char name[kNameMax];
    bool hidden;
};

struct List
{
    Entry e[kMaxEntries];
    int n;
};

// The list the picker shows: the engine (index in the synth's engine list) and the entry of the order that stands for it
struct View
{
    int16_t ui[kViewMax];
    int16_t entry[kViewMax];
    int n;
};

inline char lower(char c)
{
    return c >= 'A' && c <= 'Z' ? char(c + 32) : c;
}

inline bool nameEq(const char* a, const char* b)
{
    for (int i = 0; i < kNameMax - 1; i++)
    {
        if (lower(a[i]) != lower(b[i]))
        {
            return false;
        }
        if (!a[i])
        {
            return true;
        }
    }
    return true;   // equal up to the cut
}

inline void clear(List& l)
{
    l.n = 0;
}

// Reads the order file text. Lenient: a line is trimmed, empty lines and comments are skipped, a line with a byte that is not
// printable ASCII is skipped, a repeated name is skipped. Returns the number of entries (l.n). len < 0 or no text: no entries.
inline int parse(List& l, const char* text, int len)
{
    clear(l);
    if (!text || len <= 0)
    {
        return 0;
    }
    int i = 0;
    while (i < len && l.n < kMaxEntries)
    {
        int end = i;
        while (end < len && text[end] != '\n' && text[end] != '\r')
        {
            end++;
        }
        int a = i, b = end;
        i = end + 1;
        while (a < b && (text[a] == ' ' || text[a] == '\t'))
        {
            a++;
        }
        while (b > a && (text[b - 1] == ' ' || text[b - 1] == '\t'))
        {
            b--;
        }
        if (a >= b || text[a] == '#')
        {
            continue;
        }
        bool ok = true;
        for (int k = a; k < b; k++)
        {
            const unsigned char c = static_cast<unsigned char>(text[k]);
            if (c < 0x20 || c > 0x7E)
            {
                ok = false;
                break;
            }
        }
        if (!ok)
        {
            continue;
        }
        bool hidden = false;
        if (text[a] == '-')
        {
            hidden = true;
            a++;
            while (a < b && (text[a] == ' ' || text[a] == '\t'))
            {
                a++;
            }
            if (a >= b)
            {
                continue;
            }
        }
        Entry& e = l.e[l.n];
        int n = 0;
        for (; a < b && n < kNameMax - 1; a++)
        {
            e.name[n++] = text[a];
        }
        e.name[n] = 0;
        e.hidden = hidden;
        bool dup = false;
        for (int k = 0; k < l.n; k++)
        {
            if (nameEq(l.e[k].name, e.name))
            {
                dup = true;
                break;
            }
        }
        if (!dup)
        {
            l.n++;
        }
    }
    return l.n;
}

// Entry of every engine: entryUi[k] is the engine (ui index) that entry k stands for, -1 when none is there. An engine takes the
// first entry of its name that no other engine has taken. Returns the number of engines that have an entry.
inline int bind(const List& l, const char* const* names, int nEng, int16_t* entryUi)
{
    for (int k = 0; k < l.n; k++)
    {
        entryUi[k] = -1;
    }
    int bound = 0;
    for (int ui = 0; ui < nEng; ui++)
    {
        for (int k = 0; k < l.n; k++)
        {
            if (entryUi[k] < 0 && nameEq(l.e[k].name, names[ui]))
            {
                entryUi[k] = int16_t(ui);
                bound++;
                break;
            }
        }
    }
    return bound;
}

// Gives every engine an entry: the engines without one are appended, visible, in the order of the engine list. When the list is
// full the last entries of engines that are not there make room. entryUi is bound again at the end (l.n entries).
inline void reconcile(List& l, const char* const* names, int nEng, int16_t* entryUi)
{
    bind(l, names, nEng, entryUi);
    bool taken[kViewMax] = {};
    for (int k = 0; k < l.n; k++)
    {
        if (entryUi[k] >= 0)
        {
            taken[entryUi[k]] = true;
        }
    }
    for (int ui = 0; ui < nEng && ui < kViewMax; ui++)
    {
        if (taken[ui])
        {
            continue;
        }
        if (l.n >= kMaxEntries)
        {   // drop the last entry of an engine that is not there (it exists: the engines that have an entry are at most 256)
            int drop = -1;
            for (int k = l.n - 1; k >= 0; k--)
            {
                if (entryUi[k] < 0)
                {
                    drop = k;
                    break;
                }
            }
            if (drop < 0)
            {
                break;
            }
            for (int k = drop; k + 1 < l.n; k++)
            {
                l.e[k] = l.e[k + 1];
                entryUi[k] = entryUi[k + 1];
            }
            l.n--;
        }
        Entry& e = l.e[l.n];
        int n = 0;
        for (; names[ui][n] && n < kNameMax - 1; n++)
        {
            e.name[n] = names[ui][n];
        }
        e.name[n] = 0;
        e.hidden = false;
        entryUi[l.n] = int16_t(ui);
        l.n++;
    }
    bind(l, names, nEng, entryUi);
}

// The list to show. used[ui]: a part of the open project plays that engine. edit: the list of the edit mode, every engine in the
// order of the file, hidden ones too. Otherwise: the hidden engines in use first (in the order of the file), then the visible
// ones in the order of the file. Never empty while there is an engine.
inline void buildView(const List& l, const int16_t* entryUi, int nEng, const bool* used, bool edit, View& v)
{
    v.n = 0;
    if (!edit)
    {
        for (int k = 0; k < l.n; k++)
        {
            const int ui = entryUi[k];
            if (ui >= 0 && l.e[k].hidden && used && used[ui] && v.n < kViewMax)
            {
                v.ui[v.n] = int16_t(ui);
                v.entry[v.n++] = int16_t(k);
            }
        }
    }
    for (int k = 0; k < l.n; k++)
    {
        const int ui = entryUi[k];
        if (ui >= 0 && (edit || !l.e[k].hidden) && v.n < kViewMax)
        {
            v.ui[v.n] = int16_t(ui);
            v.entry[v.n++] = int16_t(k);
        }
    }
    if (v.n == 0 && !edit)
    {
        buildView(l, entryUi, nEng, used, true, v);
    }
    (void)nEng;
}

// Position of an engine in the view, -1 when it is not shown
inline int viewPos(const View& v, int ui)
{
    for (int p = 0; p < v.n; p++)
    {
        if (v.ui[p] == ui)
        {
            return p;
        }
    }
    return -1;
}

// One step in a list of n rows. wrap: past the last row is the first and before the first the last, otherwise the position stops
// at the ends. A bigger step than one row keeps its direction (a wrap goes to the end it runs into).
// pos -1: no row is current (a part whose engine is not installed). A step forward counts from before the first row (+1 is the first
// row), a step back is the last row with wrap and the first one without. A step of 0 stays on no row.
inline int step(int pos, int delta, int n, bool wrap)
{
    if (n <= 0)
    {
        return 0;
    }
    if (pos < 0)
    {
        if (delta == 0)
        {
            return -1;
        }
        const int t = delta > 0 ? delta - 1 : (wrap ? n + delta : 0);
        return t < 0 ? 0 : (t >= n ? n - 1 : t);
    }
    const int t = pos + delta;
    if (t >= 0 && t < n)
    {
        return t;
    }
    if (wrap)
    {
        return delta > 0 ? 0 : n - 1;
    }
    return t < 0 ? 0 : n - 1;
}

// The entry moves one place in the order (dir -1 up, +1 down), past the entries that no engine stands for (their place in the file
// stays). The engine of an entry moves with it (entryUi). False at the ends of the list.
inline bool moveEntry(List& l, int16_t* entryUi, int entry, int dir)
{
    if (entry < 0 || entry >= l.n)
    {
        return false;
    }
    int k = entry + dir;
    while (k >= 0 && k < l.n && entryUi[k] < 0)
    {
        k += dir;
    }
    if (k < 0 || k >= l.n)
    {
        return false;
    }
    const Entry t = l.e[entry];
    l.e[entry] = l.e[k];
    l.e[k] = t;
    const int16_t u = entryUi[entry];
    entryUi[entry] = entryUi[k];
    entryUi[k] = u;
    return true;
}

// The file text. Returns the length, the text is cut before a line that does not fit in cap (the comment head is left out when it
// does not fit).
inline int format(const List& l, char* out, int cap)
{
    static const char kHead[] =
      "# Synth list: the order of the engines in the Model list of the Synth screen, top to bottom.\r\n"
      "# A leading - hides the engine. An engine that is not listed is shown after these.\r\n"
      "# A hidden engine that the open project uses is shown at the top anyway.\r\n"
      "# Edit on the unit (Synth screen, Model, Shift+F1) or here, and open the Synth screen again.\r\n";
    int n = 0;
    if (int(sizeof(kHead)) - 1 < cap)   // the comment head only when it fits whole
    {
        for (const char* p = kHead; *p; p++)
        {
            out[n++] = *p;
        }
    }
    for (int k = 0; k < l.n; k++)
    {
        int len = 0;
        while (l.e[k].name[len])
        {
            len++;
        }
        const int need = (l.e[k].hidden ? 1 : 0) + len + 2;
        if (n + need >= cap)
        {
            break;
        }
        if (l.e[k].hidden)
        {
            out[n++] = '-';
        }
        for (int i = 0; i < len; i++)
        {
            out[n++] = l.e[k].name[i];
        }
        out[n++] = '\r';
        out[n++] = '\n';
    }
    return n;
}

// The text of a row of the edit list: the "-" of a hidden engine, then the engine name, fitted so that the whole row is at most
// kRowPx - kRowMarginPx wide. width(c) is the advance of the glyph c in pixels. The mark always stays the first character: it is the
// name that gives way, and by the same width for hidden and shown engines (hiding one never changes how its name reads). A name that is
// too wide first loses a trailing "TYPE" or "-TYPE" (HANDTYPE -> HAND), then characters from the end. out has cap bytes, at most
// cap - 2 characters of the name are kept (the mark and the terminator).
template <class W>
inline void fitRow(char* out, int cap, const char* name, bool hidden, W width)
{
    auto px = [&](int len) {
        int w = 0;
        for (int i = 0; i < len; i++)
        {
            w += width(name[i]);
        }
        return w;
    };
    const int budget = kRowPx - kRowMarginPx - width('-');
    int len = 0;
    while (name[len] && len < cap - 2)
    {
        len++;
    }
    if (px(len) > budget)
    {
        if (len > 4 && lower(name[len - 4]) == 't' && lower(name[len - 3]) == 'y' && lower(name[len - 2]) == 'p' && lower(name[len - 1]) == 'e')
        {
            int cut = len - 4;
            if (cut > 1 && name[cut - 1] == '-')
            {
                cut--;
            }
            len = cut;
        }
        while (len > 1 && px(len) > budget)
        {
            len--;
        }
    }
    int n = 0;
    if (hidden)
    {
        out[n++] = '-';
    }
    for (int i = 0; i < len; i++)
    {
        out[n++] = name[i];
    }
    out[n] = 0;
}

}   // namespace synthorder
