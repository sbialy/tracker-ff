// host stand-in: the UART interrupt priority the test uses (the audio render runs below it, at 128)
#pragma once
static const int kMidiJackInUARTPriorityLevel = 32;
