// OpenBFME retail tests: abilities aimed at the ground, used as a player uses them (the button pressed on the control bar, the ground clicked near the unit):
// a horde's bombard (FIRE_WEAPON NEED_TARGET_POS, GondorArcherHorde's TERTIARY slot) fires from where it stands when the point is in reach, and a hero's
// leap (Gimli's SpecialAbilityGimliLeap, WeaponFireSpecialAbilityUpdate) runs when the point is close. SKIPs without ROTWK_INSTALL / BFME2_INSTALL.

#include "doctest.h"
#include "FactATestUtil.h"
#include "FactAEffects.h"

#include "GameLogic/Combat/ObjectWeapons.h"
#include "GameLogic/Object/ExperienceTracker.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/SpecialPowerModules.h"
#include "GameLogic/WeaponState.h"

#include <cmath>
#include <cstdio>

using namespace facta;

namespace
{
float dist2D(const Coord3D &a, const Coord3D &b)
{
	return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y));
}

unsigned stateOf(Object &o)
{
	const AIUpdateInterface *ai = o.getAIUpdateInterface();
	const AIStateMachine *m = ai ? ai->stateMachineOrNull() : nullptr;
	return m ? m->currentStateId() : 0u;
}
} // namespace

TEST_CASE("ground powers: an archer horde's bombard at a point in its reach fires from where it stands")
{
	if (!hudtest::haveWorld("ground powers"))
	{
		return;
	}
	SharedWorld &s = hudtest::shared();
	FactRig r(s, "FactionMen");
	const Coord3D at = r.spot(2000.0f, 1300.0f, 350.0f);
	Object *o = r.make("GondorArcherHorde", at);
	r.frames(20);
	REQUIRE(r.select(o));
	const Coord3D start = *o->getPosition();
	const Weapon *w = o->getWeapons() ? o->getWeapons()->weaponInSlot(TERTIARY_WEAPON) : nullptr;
	REQUIRE(w);
	const std::uint32_t before = w->lastFireFrame();
	REQUIRE(r.press("Command_ArcherBombard"));
	Coord3D ground{ start.x + 165.0f, start.y + 20.0f, 0.0f };
	ground.z = r.logic().getGroundHeight(ground.x, ground.y);
	r.clickWorld(ground);
	bool fired = false;
	for (int f = 0; f < 150 && !fired; ++f)
	{
		r.frames(1);
		fired = w->lastFireFrame() != before;
		if (f % 10 == 0)
		{
			std::printf("ground powers bombard f%d: state %u moved %.0f to the point %.0f slot %d in range %d\n", f, stateOf(*o), dist2D(*o->getPosition(), start),
				dist2D(*o->getPosition(), ground), o->getWeapons()->curSlot(), (int)o->getWeapons()->isWithinAttackRange(ground));
		}
	}
	CHECK(fired);
	CHECK(dist2D(*o->getPosition(), start) < 40.0f); // in reach: no walk onto the point
}

TEST_CASE("ground powers: Gimli's leap at a point close by runs")
{
	if (!hudtest::haveWorld("ground powers"))
	{
		return;
	}
	SharedWorld &s = hudtest::shared();
	FactRig r(s, "FactionDwarves");
	const Coord3D at = r.spot(2000.0f, 1300.0f, 350.0f);
	Object *o = r.make("RohanGimli", at);
	r.frames(5);
	REQUIRE(o->getExperienceTracker());
	o->getExperienceTracker()->gainLevels(20, false);
	r.frames(3);
	REQUIRE(r.select(o));
	const SpecialPowerTemplate *spt = TheSpecialPowerStore->findSpecialPowerTemplate("SpecialAbilityGimliLeap");
	REQUIRE(spt);
	SpecialPowerModuleInterface *spm = SpecialPowerModules::findModule(*o, spt);
	REQUIRE(spm);
	const Coord3D start = *o->getPosition();
	REQUIRE(r.press("Command_RohanGimliLeap"));
	Coord3D ground{ start.x + 150.0f, start.y + 60.0f, 0.0f };
	ground.z = r.logic().getGroundHeight(ground.x, ground.y);
	r.clickWorld(ground);
	bool ran = false;
	for (int f = 0; f < 200 && !ran; ++f)
	{
		r.frames(1);
		ran = spm->getPercentReady() < 1.0f;
		if (f % 10 == 0)
		{
			std::printf("ground powers leap f%d: state %u at %.0f from the start, %.0f from the point, ready %.2f\n", f, stateOf(*o), dist2D(*o->getPosition(), start),
				dist2D(*o->getPosition(), ground), spm->getPercentReady());
		}
	}
	CHECK(ran);
}
