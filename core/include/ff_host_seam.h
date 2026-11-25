#ifndef FF_HOST_SEAM_H_
#define FF_HOST_SEAM_H_

// A preempt point for the host simulator (synth-open/tools/ui_shot): where the simulator may run what an interrupt would run in the middle
// of main loop work, to prove that the interrupt path never re-enters it (midi_isr_race_sim.sh). Nothing on the firmware.
#ifdef UI_SHOT_HOST
extern "C" void uishotPreempt(int point);
#define FF_HOST_PREEMPT(point) uishotPreempt(point)
#else
#define FF_HOST_PREEMPT(point) ((void)0)
#endif

// the points
#define FF_PREEMPT_PATTERN_REFRESH 1   // cPatternEditor::refreshPattern (fPatternEdit.cpp), a redraw of the pattern view

#endif // FF_HOST_SEAM_H_
