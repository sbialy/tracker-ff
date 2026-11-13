# Notices for the plugins in this directory

Licence: LICENSE.md. Third party licence texts: the LICENSE-*.txt files here.

## phz

Clean-room phase distortion engine of this project, written from the public Polyend Synth manual (SYN2 p.76-80), nothing of Polyend's closed code read. Copyright (c) 2026 sbialy. All rights reserved. Proprietary freeware, free to use on the Polyend Tracker running the tracker-ff firmware, unmodified copies may be passed on free of charge with the licence and notice texts attached, no modification, sale, bundling (the tracker-ff firmware excepted) or porting, see LICENSE.md. Third party parts keep their licences: the parts that follow OpenTracker stay Apache License 2.0 (LICENSE-OpenTracker-Apache-2.0.txt) and the plugin runtime from the public tracker-ff repository stays MIT (LICENSE-tracker-ff-MIT.txt). The block rate voice skeleton (envelope, LFO and glide stepping, the destination ramp scheme, the TPT state variable stage and the filter flavours, fastExp2, blockCoef, fastTan, lfoShape) follows the VAP engine of OpenTracker 3.x (https://github.com/sandlbn/polyend-tracker-re, formerly its v3_fixes branch, Apache License 2.0, text in LICENSE-OpenTracker-Apache-2.0.txt), trimmed and adapted: modified.

## pluck

Clean-room Karplus-Strong plucked string engine of this project (textbook algorithm with the extensions of Jaffe and Smith, Computer Music Journal 7(2), 1983), no third party engine code. Copyright (c) 2026 sbialy. All rights reserved. Proprietary freeware, free to use on the Polyend Tracker running the tracker-ff firmware, unmodified copies may be passed on free of charge with the licence and notice texts attached, no modification, sale, bundling (the tracker-ff firmware excepted) or porting, see LICENSE.md. Built against the OpenTracker headers (Apache License 2.0, LICENSE-OpenTracker-Apache-2.0.txt) and the plugin runtime from the public tracker-ff repository (MIT, LICENSE-tracker-ff-MIT.txt), which keep their licences.
