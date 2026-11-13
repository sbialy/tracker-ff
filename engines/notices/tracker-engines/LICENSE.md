# Licence for the FF synth plugins PHZ and Pluck (freeware)

Copyright (c) 2026 sbialy. All rights reserved.

This licence covers the synth engines PHZ and Pluck (also called PLK), as the compiled
files published with this licence: the `.plg` and `.plgi` plugin files that the Polyend Tracker loads (the
"Modules"). The Modules are proprietary freeware. They are not open source. The source code of the Modules is not published and
this licence gives no right to it.

## What you may do

1. Use the Modules for free on the Polyend Tracker running the tracker-ff firmware. What you make with the Modules is yours.
2. Pass on unmodified copies of the Modules, free of charge, for use as described in item 1, if this LICENSE.md and the notice
   and licence texts that came with them stay attached.

## What you may not do

1. Modify, adapt or translate the Modules, or make anything derived from them.
2. Sell, rent or lease the Modules, or charge for them in any way.
3. Bundle them into another product or service. The tracker-ff firmware is the only exception.
4. Port them to, or run them on, any other platform or host than the one named in "What you may do".
5. Reverse engineer, decompile or disassemble them, to the extent permitted by applicable law. Where the law allows it despite
   this clause (for example decompilation for interoperability under the EU Software Directive 2009/24/EC), that right stays.

All rights not granted here are reserved.

## Third party parts

* The PHZ voice skeleton and the OpenTracker headers that PHZ and Pluck are built against come from OpenTracker (Apache License
  2.0). Those parts stay under Apache 2.0, see `LICENSE-OpenTracker-Apache-2.0.txt`, which ships next to this file. This licence
  does not replace it. The PHZ voice skeleton (envelope, LFO and glide stepping, the destination ramp scheme, the state variable
  filter stage and the filter flavours) follows the VAP engine of OpenTracker 3.x
  (https://github.com/sandlbn/polyend-tracker-re, formerly its v3_fixes branch) and we trimmed and changed it.
* The plugin runtime and SDK files built into both Modules come from the public tracker-ff repository and stay under the MIT
  licence, see `LICENSE-tracker-ff-MIT.txt`, which ships next to this file. This licence applies to the Modules published with
  this file.

## No warranty

The Modules are provided "as is", without warranty of any kind. To the extent permitted by law, the author is not liable for any
claim, damage or loss arising from their use.
