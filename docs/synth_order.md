# Synth order in tracker-ff 1.3.0

Synth order lets you hide engines in the Model list of the Synth screen and put the rest in the order you like. It changes only what the list shows. Projects and `.patch` files are not touched.

- [What it is](#1-what-it-is)
- [The edit mode](#2-the-edit-mode)
- [The order file](#3-the-order-file)
- [How the list is built](#4-how-the-list-is-built)
- [Stepping through the list](#5-stepping-through-the-list)
- [Limits and known issues](#6-limits-and-known-issues)

## 1. What it is

The Model column of the Synth screen lists the synth engines: the five built in ones and every plugin engine in `/Plugins/Synth/` on the card. With thirteen plugin engines that is a long list. Synth order gives you two things:

- **Order.** Put the engines you use first.
- **Hide.** Take the engines you never use out of the list.

The list is a view. A project stores the engine of a part by its own number and a `.patch` file stores the engine by its id. Neither one stores a place in the list, so hiding or moving an engine changes nothing in a project, a patch or a sound, and the same files open the same way on a unit with another order. Firmware 1.2.x ignores the order file.

## 2. The edit mode

Open the Synth screen of a synth instrument and press **Shift + F1**. The Model column goes into the edit mode:

- The column label reads `Edit list` and a popup tells you the keys.
- The list shows every engine as a name, in the order of the file. The logos are not shown. An engine that is hidden has a `-` in front of its name.
- Nothing is picked in the edit mode. The part keeps its engine.

| Key | What it does in the edit mode |
|---|---|
| Up, Down | Move the cursor. At the ends it wraps round to the other end |
| Encoder | Moves the cursor. At the ends it stops |
| Shift + Up, Shift + Down | Move the engine under the cursor one place up or down, the cursor goes with it. At the ends of the list nothing happens |
| Delete | Hide the engine under the cursor |
| Insert | Show the engine under the cursor again |

The cursor starts on the engine the part plays.

Every change is written to the card at once, so there is nothing to confirm. If the card cannot take the file (for example there is no `/Plugins` folder) the popup says `Order not saved`. The change then holds only until the Synth screen is shown again.

To leave the edit mode press Shift + F1 or F1 again, or press Left, Right, another column button (F2 to F6), Load (F7) or Edit Patch (F8). Leaving the Synth screen ends it too. The edit mode never survives a new visit of the screen.

A name that is too wide for a row is shortened on the screen (the `-` always stays visible). The file keeps the full name.

## 3. The order file

The order is kept in `/Plugins/synth-order.txt` on the card. The Tracker writes it the first time you move or hide an engine. Until then there is no file and the list has the default order: the built in engines, then the plugins, as the engine list has them.

The file is plain text, one engine name per line. The top line is the top of the list. A `-` in front of a name hides the engine. A line that starts with `#` is a comment.

```
# Synth list: the order of the engines in the Model list of the Synth screen, top to bottom.
ACD
FAT
-PERC
JU6
CHIP
```

You can edit the file on a computer. The Tracker reads it again each time the Synth screen comes up, so a change needs no restart. Open the Synth screen again after the edit.

Rules the Tracker follows when it reads the file:

| Case | Result |
|---|---|
| Upper or lower case in a name | Ignored. `chip` finds CHIP |
| An engine has no line (a new `.plg` you copied to the card) | It is shown after the listed engines, in the order of the engine list |
| A line names an engine that is not on the card | The line is ignored and stays in the file, so the entry is back when the plugin is |
| Two lines for one engine | The first one counts |
| Spaces or tabs around a name or after the `-` | Ignored |
| A line with a character that is not printable text | Skipped |
| A file that is no text at all | The default order, everything shown |
| A name longer than 23 characters | Cut at 23 |

The Tracker keeps what it does not use. When it saves, it writes back every line, including the lines of engines that are not on the card now, and it adds the engines that had no line.

## 4. How the list is built

The list you see in the normal mode is made from the file like this:

1. A hidden engine that a part of the open project plays comes first, at the top, in the order of the file. Without this, a part could play an engine that its own Model list does not show.
2. Then every engine that is not hidden, in the order of the file.

So hiding the engine of a part does not take it away from that part. The engine stays in the list at the top while the project plays it, and it leaves the list when no part plays it any more. All three synth parts count.

The list is never empty. When every engine is hidden and no part plays one, the list shows all of them.

## 5. Stepping through the list

In the normal mode the keys work as they did before, only on the rows of the list:

| Key | What it does |
|---|---|
| Up, Down on the Model column | Previous or next engine of the list. They wrap from the first row to the last and back |
| Encoder | Same, but it stops at the ends |
| F1 on the Model column | The next engine, with the wrap |
| Delete | Nothing on the Model column. It hides an engine only in the edit mode |

The list moves at once, but the engine is picked only after the list has rested on a row for 1 s. Each step restarts the wait, and this holds for the built in engines too. Scrolling past engines therefore picks none of them. A plugin engine that is not loaded yet is installed when the wait is over.

## 6. Limits and known issues

| What | Limit |
|---|---|
| Engines in the list | 256 |
| Lines of the order file that count | 512 |
| Length of an engine name in the file | 23 characters |
| Hide | The Model list only. A hidden engine still plays where a project uses it |

- A part whose engine is not installed (`Missing: <NAME>`) has no row in the list. A step in the list (encoder, Up, Down, F1) puts it on a row and that engine is picked after the rest of the list, as for any part, so such a part can always be switched to an installed engine. In the edit mode the cursor starts on no row, and Shift + Up, Shift + Down and Delete do nothing until Up, Down or the encoder put the cursor on an engine.
- Hiding an engine does not remove its plugin. Delete the `.plg` and `.plgi` from the card for that.
- If you edit the file on a computer while the Tracker is on, open the Synth screen again to see the change.
