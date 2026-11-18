# Licence for the FF modules PHZ, Pluck, Console and Tube (freeware)

Copyright (c) 2026 sbialy. All rights reserved.

This licence covers the synth engines PHZ, Pluck (also called PLK) and ALCO and the master inserts Console and Tube, as the compiled
files published with this licence: the `.wasm` modules, their `.json` description files and the `.plg` and `.plgi` plugin files
that the Polyend Tracker loads (the "Modules"). The Modules are proprietary freeware. They are not open source. The source code
of the Modules is not published and this licence gives no right to it.

## What you may do

1. Use the Modules for free in these two places only:
   * the Polyend Tracker running the tracker-ff firmware
   * the FF Synth Editor, run locally on your own computer or opened as a hosted web page

   What you make with the Modules is yours.
2. Serve the Modules unmodified, free of charge, as part of the FF Synth Editor from a web page. This includes the copies that
   web browsers, proxies and content delivery networks (CDNs) keep and pass on while they serve that page.
3. Pass on unmodified copies of the Modules, free of charge, for use as described in item 1, if this LICENSE.md and the notice
   and licence texts that came with them stay attached. This includes hosting your own copy of the FF Synth Editor web page with
   the Modules in it, unmodified and free of charge, for anyone to open.

## What you may not do

1. Modify, adapt or translate the Modules, or make anything derived from them.
2. Sell, rent or lease the Modules, or charge for them in any way.
3. Bundle them into another product or service. The FF Synth Editor and the tracker-ff firmware are the only exceptions.
4. Port them to, or run them on, any other platform or host than the two named in "What you may do". Running the Modules in a
   web browser as part of the FF Synth Editor is allowed.
5. Reverse engineer, decompile or disassemble them, to the extent permitted by applicable law. Where the law allows it despite
   this clause (for example decompilation for interoperability under the EU Software Directive 2009/24/EC), that right stays.

All rights not granted here are reserved.

## Third party parts

* The PHZ voice skeleton and the OpenTracker headers that PHZ and Pluck are built against come from OpenTracker (Apache License
  2.0). Those parts stay under Apache 2.0, see `LICENSE-OpenTracker-Apache-2.0.txt`, which ships next to this file with PHZ and
  Pluck. This licence does not replace it. The PHZ voice skeleton (envelope, LFO and glide stepping, the destination ramp
  scheme, the state variable filter stage and the filter flavours) follows the VAP engine of OpenTracker 3.x
  (https://github.com/sandlbn/polyend-tracker-re, formerly its v3_fixes branch) and we trimmed and changed it. Console and Tube
  contain no OpenTracker code.
* The plugin runtime and SDK files built into all four Modules come from the public tracker-ff repository and stay under the
  MIT licence, see `LICENSE-tracker-ff-MIT.txt`, which ships next to this file. This licence applies to the Modules published
  with this file.

## No warranty

The Modules are provided "as is", without warranty of any kind. To the extent permitted by law, the author is not liable for any
claim, damage or loss arising from their use.
