// OpenBFME. GPL-3.0.
//
// LevelUpUpgrade and ExperienceScalarUpgrade (lane XP-1): the two upgrade module classes that act on the object's ExperienceTracker.
//
// TARGET FACTS (RotWK game.dat, caveat S-001), read from the disassembly:
//   * LevelUpUpgrade (field table RW 0xC6E09C): LevelsToGain parseInt (+0x138), LevelCap parseInt (+0x13C). upgradeImplementation (RW 0x8B7F90):
//     n = min(LevelsToGain, LevelCap - rank); n >= 1: tracker.gainExpForLevel(n, true, false) (RW 0x79DA0A); then, when the object's horde contain exists
//     (RW 0x694BF8) and n > 0, n calls of HordeContain::gainMemberLevel (slot 0xC4, RW 0x876C23); wouldUpgrade (RW 0x8B7FFA) rejects rank >= LevelCap,
//     otherwise delegates to UpgradeMux::wouldUpgrade;
//   * ExperienceScalarUpgrade (field table RW 0xC6F194): AddXPScalar parseReal (+0x138, constructor 0.0, RW 0x8BA332). upgradeImplementation (RW 0x8BA3AD):
//     tracker scalar += AddXPScalar (SSE); the removal (RW 0x8BA3D0): tracker scalar -= AddXPScalar.
//     INFERENCE: which of the two functions is the implementation and which the removal is read from their order in the vtable region, not from the slots.

#pragma once

#include "GameLogic/Module/UpgradeModule.h"

class ModuleFactory;

class LevelUpUpgradeModuleData : public UpgradeModuleData
{
public:
	int m_levelsToGain = 0; // +0x138
	int m_levelCap = 0;     // +0x13C
	static void buildFieldParse(MultiIniFieldParse &p);
};

class LevelUpUpgrade : public UpgradeModule
{
public:
	OBFME_MODULE_XFER(LevelUpUpgrade) // lane SAVE-4b: UpgradeModule's
	LevelUpUpgrade(Thing *thing, const LevelUpUpgradeModuleData *data) : UpgradeModule(thing, data), m_data(data) {}
	bool wouldUpgrade(const UpgradeMaskType &mask) const override; // RW 0x8B7FFA

protected:
	void upgradeImplementation() override; // RW 0x8B7F90
	void processUpgradeRemoval() override {}

private:
	const LevelUpUpgradeModuleData *m_data;
};

class ExperienceScalarUpgradeModuleData : public UpgradeModuleData
{
public:
	float m_addXPScalar = 0.0f; // +0x138
	static void buildFieldParse(MultiIniFieldParse &p);
};

class ExperienceScalarUpgrade : public UpgradeModule
{
public:
	OBFME_MODULE_XFER(ExperienceScalarUpgrade) // lane SAVE-4b: UpgradeModule's
	ExperienceScalarUpgrade(Thing *thing, const ExperienceScalarUpgradeModuleData *data) : UpgradeModule(thing, data), m_data(data) {}

protected:
	void upgradeImplementation() override;  // RW 0x8BA3AD
	void processUpgradeRemoval() override;  // RW 0x8BA3D0

private:
	const ExperienceScalarUpgradeModuleData *m_data;
};

namespace ExperienceUpgradeModules
{
void registerAll(ModuleFactory &modules);
}
