// OpenBFME. GPL-3.0.
// The AI state machine's return codes as the binary uses them (RW state machine; one definition for the weapon code and the AI).
#pragma once

// lane SAVE-4c (UBSan): the fixed underlying type makes every int a value of the type: a sleep is a positive frame count (STATE_SLEEP, ZH), which the
// enumerators' range alone (-2 .. 0) does not hold
enum StateReturnType : int
{
	STATE_CONTINUE = 0,
	STATE_SUCCESS = -1,
	STATE_FAILURE = -2
};
