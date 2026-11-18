# Notices for the plugins in this directory

Licence: LICENSE.md. Third party licence texts: the LICENSE-*.txt files here.

## phz

Clean-room phase distortion engine of this project, written from the public Polyend Synth manual (SYN2 p.76-80), nothing of Polyend's closed code read. Copyright (c) 2026 sbialy. All rights reserved. Proprietary freeware, free to use with the Polyend Tracker, tracker-ff and the FF Synth Editor, unmodified redistribution allowed, see LICENSE.md. Third party parts keep their licences: the parts that follow OpenTracker stay Apache License 2.0 (LICENSE-OpenTracker-Apache-2.0.txt) and the plugin runtime from the public tracker-ff repository stays MIT (LICENSE-tracker-ff-MIT.txt). The block rate voice skeleton (envelope, LFO and glide stepping, the destination ramp scheme, the TPT state variable stage and the filter flavours, fastExp2, blockCoef, fastTan, lfoShape) follows the VAP engine of OpenTracker 3.x (https://github.com/sandlbn/polyend-tracker-re, formerly its v3_fixes branch, Apache License 2.0, text in LICENSE-OpenTracker-Apache-2.0.txt), trimmed and adapted: modified.

## pluck

Clean-room Karplus-Strong plucked string engine of this project (textbook algorithm with the extensions of Jaffe and Smith, Computer Music Journal 7(2), 1983), no third party engine code. Copyright (c) 2026 sbialy. All rights reserved. Proprietary freeware, free to use with the Polyend Tracker, tracker-ff and the FF Synth Editor, unmodified redistribution allowed, see LICENSE.md. Built against the OpenTracker headers (Apache License 2.0, LICENSE-OpenTracker-Apache-2.0.txt) and the plugin runtime from the public tracker-ff repository (MIT, LICENSE-tracker-ff-MIT.txt), which keep their licences.

## form

Clean-room combined synth of this project: three layers, vector phaseshaping (Kleimola, Lazzarini, Timoney, Valimaki, DAFx 2011), feedback amplitude modulation (Lazzarini, Timoney, Kleimola, EURASIP JASP 2011) and pulsar synthesis (Roads, Journal of the Audio Engineering Society 2001), written from the maths of the papers, no third party engine code. Copyright (c) 2026 sbialy. All rights reserved. Proprietary freeware, free to use with the Polyend Tracker and tracker-ff, unmodified redistribution allowed, see LICENSE.md. Built against the OpenTracker headers (Apache License 2.0, LICENSE-OpenTracker-Apache-2.0.txt) and the plugin runtime from the public tracker-ff repository (MIT, LICENSE-tracker-ff-MIT.txt), which keep their licences.

## flux

Clean-room combined synth of this project: three layers, wave terrain synthesis (Bischoff, Gold, Horton, CMJ 1978, Borgonovo and Haus, CMJ 1986), scanned synthesis (Verplank, Mathews, Shaw, ICMC 2000) and dynamic stochastic synthesis (Xenakis, Formalized Music 1992, Hoffmann, CMJ 2000), written from the maths of the papers, no third party engine code. Copyright (c) 2026 sbialy. All rights reserved. Proprietary freeware, free to use with the Polyend Tracker and tracker-ff, unmodified redistribution allowed, see LICENSE.md. Built against the OpenTracker headers (Apache License 2.0, LICENSE-OpenTracker-Apache-2.0.txt) and the plugin runtime from the public tracker-ff repository (MIT, LICENSE-tracker-ff-MIT.txt), which keep their licences.

## alco

Two oscillator algorithm combiner synth of this project (oscillators A and B and a sub joined by one of 19 algorithms: phase and frequency modulation, ring, AM, xor, folds and more), with a chord mode of one voice. Our own design from the public DSP literature, nothing of any other product's code, binary or patch data used. Copyright (c) 2026 sbialy. All rights reserved. Proprietary freeware, free to use with the Polyend Tracker and tracker-ff, unmodified redistribution allowed, see LICENSE.md. Built against the OpenTracker headers (Apache License 2.0, LICENSE-OpenTracker-Apache-2.0.txt): the exponential approximation, the ZDF ladder stage equations, the TPT state variable filter, the parabolic sine, the tangent and tanh approximations and the RC attack ADSR of va_dsp.h, modified. The plugin runtime from the public tracker-ff repository stays MIT (LICENSE-tracker-ff-MIT.txt).

