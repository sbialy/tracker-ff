## Summary & Goal
<!-- Explain WHY this change is being made and what problem or musical workflow it addresses. Keep each PR focused on a single feature or bug fix. -->


## Behavior & Changes
<!-- Describe WHAT changed: user-facing behavior, UI screens, button combinations (on both Original/Plus and Tracker Mini if applicable), and key technical details. -->


## Target Devices
<!-- Check all devices for which this feature/fix is built: -->
- [ ] **Original Polyend Tracker** (`MK66`)
- [ ] **Polyend Tracker+** (`IMX`)
- [ ] **Polyend Tracker Mini** (`IMX`)

## Hardware Testing Status
<!-- Please be explicit about which physical devices you have tested yourself and which devices still need testing by community members who own them.
     Note for testers: You can download ready-to-flash .ptf files from the "Checks" tab -> "polyend-tracker-ce-firmware" artifact on this PR! -->

| Device | Tested on Hardware by Author | Needs Community Hardware Testing | Not Applicable | Notes / Tested By |
| :--- | :---: | :---: | :---: | :--- |
| **Original Tracker** (`MK66`) | [ ] | [ ] | [ ] | |
| **Tracker+** (`IMX`) | [ ] | [ ] | [ ] | |
| **Tracker Mini** (`IMX`) | [ ] | [ ] | [ ] | |

## How to Test
<!-- Provide clear, step-by-step instructions so reviewers and community testers can verify this PR on their hardware: -->
1. 
2. 
3. 

## Developer Checklist
- [ ] I have read and understood the Contribution & Pull Request Guidelines (`CONTRIBUTING.md`).
- [ ] This PR implements **one single feature or bug fix** (no unrelated refactors or formatting sweeps).
- [ ] `make original` compiles cleanly (Tracker+ and Tracker Mini are checked before a release, not on every branch). A PR meant for upstream `polyend/tracker-ce` still needs `make all` to compile for all three devices (`Original`, `Plus`, `Mini`).
- [ ] Memory usage (`SRAM_DTC`, `EXTERNAL_RAM`, `FLASH`) was checked and stays within safe hardware headroom.
- [ ] No heap allocations (`new`/`malloc`) or SD card I/O were added to real-time audio callbacks.
- [ ] Existing `.mt` projects, `.pti` instruments, `.pts` synth patches, and `mtConfig` settings remain backward-compatible.
