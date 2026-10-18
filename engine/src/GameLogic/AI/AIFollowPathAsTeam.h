// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0 (AIStates.cpp AIFollowPathState).
//
// The formation march's per-unit state (lane BACKLOG-8 r2): RotWK's AIFollowPathAsTeamState, state 54 (the attack variant is state 61), entered by AI
// command 0x24 (RW 0x66AB94 -> AI vslot 0x7C, RW 0x66BB9D: BFME2 decomp AIUpdateInterfacePrivateCommands.cpp bfmePrivateCommand24) that
// AIGroup::moveFormationToPos (RW 0x77348C) gives every unit of a marching group.
//
// TARGET FACTS (RotWK game.dat, caveat S-001):
//   * the command: an immobile unit or an empty path does nothing; else the machine is cleared, its goal position is the path's last point, its goal path
//     the path, the command source and the ignored obstacle are kept, the angle goes to AI + 0x1A0 and the machine enters state 54.
//   * onEnter RW 0x74FD39 (decomp AIFollowPathAsTeamState::onEnter, tier B same-shape): index 0; the AI's command source (+ 0x58) and the angle (+ 0x60) are
//     kept; points already behind the unit (owner -> point . point -> next < 0) are skipped; the AI's current goal path index (+ 0x194) is the index; the
//     internal move (RW 0x74DADA) starts toward the point, adjusting its destination only at the last point; the path extra distance is the length to the
//     next point (+ 40 when a point follows it), 0 at the last.
//   * update RW 0x74FF49 (read from the RotWK disassembly: the decomp's body is not byte-matched; the move variant, + 0x5C null): the machine's goal
//     position is the current point. After the last point the unit turns to the angle: while normalizeAngle(angle - facing) (RW 0x644FD0, signed) is at
//     least 0.31415927 (RW 0xC28A34) its locomotor goal is the angle (AI vslot 0x21C), else it takes the angle (RW 0x70C31E) and the state succeeds. Else
//     the internal move runs (RW 0x748E46): a failure retries while + 0x50 (5) lasts, then counts as arrival; at an arrival (with + 0x57, a halved
//     destination, the index steps back once) the unit faces the next point (its own top speed to AI + 0x44 first when AI vslot 0x224 answers yes; RW
//     0x4B3D8D the relative angle), the index advances (AI + 0x194), and with a next point the unit waits at it (+ 0x56: MOVING cleared) until
//     canMoveToNextFormationPathPoint (RW 0x74B85F) lets it go. Then points nearer than 10 (100 squared, RW 0xBD88D8) or behind the unit are skipped; no
//     point left: the turn to the angle begins; else the move starts again (RW 0x6627AF) toward the point with the extra distance as in onEnter; at the
//     last point the destination adjusts (+ 0x48 = + 0x54 and (+ 0x55 or vslot 0x224), RW 0x75040D-0x75042F; + 0x55 is cleared at RW 0x74FCD7 and nothing in this state sets it: a ground marcher adjusts) through adjustDestination (RW 0x6FE456): a refusal spends a
//     retry (none left: FAILURE) and halves the way to the point (+ 0x57); the goal is reserved (RW 0x68B3AB); then the internal move's path (vslot 0x44).
//   * onExit (decomp tier B): the internal move's exit, the AI may no longer path through units, the locomotor's precise z off, the desired speed
//     FAST_AS_POSSIBLE, the current goal path index -1.
// INFERENCE / NOT PORTED (stop S-4463): the attack variant (state 61, command 0x25, its attack-move machine at + 0x5C); AI + 0x44 is taken as the desired
// speed and AI + 0x164 (cleared before each new point, RW 0x662D98) is not identified; canMoveToNextFormationPathPoint's wait (AIData WaitForOthers, No
// in the retail AIData) is not ported: the unit always goes on; the internal move is the port's AIMoveToState (its adjustment rule is its own flag, RW
// 0x745B3F's parachute / aircraft tests are not read); computePath's outside-the-map quick path (BFME2 0x745BCF) is not ported.

#pragma once

#include "GameLogic/AI/AIStateMachine.h"

#include <memory>

class AIMoveToState;

class AIFollowPathAsTeamState : public AIState
{
public:
	explicit AIFollowPathAsTeamState(AIStateMachine &m);
	~AIFollowPathAsTeamState() override;
	StateReturnType onEnter() override;
	StateReturnType update() override;
	void onExit(StateExitType status) override;
	void crc(class StateHasher &hasher) const override;
	void xfer(class Xfer &x) override; // merge queue q19: SAVE-4c made it pure; every field crc covers (layout as AIFollowPathState::xfer)
	void onTemporaryStateEnded() override;
	int index() const { return m_index; }
	bool turningToFinalAngle() const { return m_turning; }

	static const char *stopLine();

private:
	// the extra distance and the internal move toward m_goal (RW 0x74FD39's tail / RW 0x7502FA ..)
	StateReturnType startMove(bool adjusts);
	int m_index = 0;           ///< + 0x4C
	int m_retries = 5;         ///< + 0x50
	bool m_adjustFinal = true; ///< + 0x54
	bool m_atPoint = false;    ///< + 0x56
	bool m_steppedBack = false; ///< + 0x57
	float m_angle = 0.0f;      ///< + 0x60
	bool m_turning = false;    ///< + 0x64
	Coord3D m_goal{ 0.0f, 0.0f, 0.0f }; ///< the internal move's goal (+ 0x20)
	std::unique_ptr<AIMoveToState> m_move;
};
