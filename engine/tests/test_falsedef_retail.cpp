// OpenBFME unit tests. GPL-3.0.
// FALSEDEF-1: the RotWK campaign mission 0 defeat — the retail numbers behind "two troll slings killed the
// 7,500-health AngmarFortressCitadel ~170 frames after 'BNWest Move In'". Retail-gated: SKIPs without the
// real archives. Asserts the parsed weapon/armor values retail ships and the damage the chain computes per
// direct hit; reports the retail time-to-kill floor so the observed ~34-second kill can be compared.

#include "doctest.h"
#include "RetailTestMount.h"

#include "Common/Thing/ThingTemplate.h"
#include "GameEngineDevice/Win32Device/Common/Win32BIGFileSystem.h"
#include "GameLogic/Armor.h"
#include "GameLogic/ArmorSet.h"
#include "GameLogic/Damage.h"
#include "GameLogic/Object/RetailObjectWorld.h"
#include "GameLogic/ObjectFilter.h"
#include "GameLogic/ObjectTemplateInfo.h"
#include "GameLogic/Weapon.h"
#include "GameLogic/WeaponNugget.h"
#include "GameLogic/WeaponStores.h"

#include <memory>
#include <string>
#include <vector>

namespace
{

struct SharedWorld
{
	retailtest::Mount *mount = nullptr;
	std::unique_ptr<RetailObjectWorld> world;
	std::string error;
};

SharedWorld &shared()
{
	static SharedWorld s;
	static bool built = false;
	if (!built)
	{
		built = true;
		s.mount = retailtest::pureMount();
		if (s.mount && s.mount->fs)
		{
			s.world = std::make_unique<RetailObjectWorld>(*s.mount->fs);
			if (!s.world->load(&s.error))
			{
				s.world.reset();
			}
		}
	}
	return s;
}

bool haveWorld(const char *name)
{
	SharedWorld &s = shared();
	if (!s.mount)
	{
		retailtest::printSkip(name);
		return false;
	}
	REQUIRE_MESSAGE(s.mount->fs != nullptr, s.mount->error);
	REQUIRE_MESSAGE(s.world != nullptr, s.error);
	return true;
}

// The sling-vs-citadel hit: an enemy source with no attribute modifiers; no DamageScalar entry of the
// warhead admits the victim (they want ALLIES or ENEMIES + INFANTRY / + CAVALRY — a structure matches none).
class SlingHost : public NuggetDamageHost
{
public:
	bool hasVictim() override { return true; }
	float victimDistanceSqr2D(const Coord3D &) override { return 0.0f; } // direct hit
	Coord3D victimPosition() override { return Coord3D{ 0.0f, 0.0f, 0.0f }; }
	Coord3D sourcePosition() override { return Coord3D{ 0.0f, 0.0f, 0.0f }; }
	bool sourceHasContain() override { return false; }
	int sourcePassengerCount() override { return 0; }
	bool filterAllowsVictim(const ObjectFilter &) override { return false; }
	bool sourceAdditive(int, float &) override { return false; }
	bool sourceMultiplicative(int, bool, float &) override { return false; }
	bool victimFlankedBySource() override { return false; }
	bool sourceFlankedByVictim() override { return false; }
	bool weaponSourceIsVictim() override { return false; }
	std::uint32_t sourcePlayerMask() override { return 0; }
	unsigned weaponSourceID() override { return 0; }
};

class CitadelHost : public AdjustDamageHost
{
public:
	bool victimFlankedByAttacker() override { return false; }
	bool invulnerableTo(int) override { return false; }
	float armorModifierSum(int) override { return 0.0f; }
	float armorMaxBonus() override { return 0.75f; } // GlobalData AttributeModifierArmorMaxBonus
};

const DamageNugget *firstDamageNugget(const WeaponTemplate *w, int damageType)
{
	if (!w)
	{
		return nullptr;
	}
	for (const std::shared_ptr<WeaponNugget> &n : w->m_nuggets)
	{
		const DamageNugget *d = dynamic_cast<const DamageNugget *>(n.get());
		if (d && d->m_damageType == damageType)
		{
			return d;
		}
	}
	return nullptr;
}

TEST_CASE("falsedef retail: AngmarStoneThrowerRock warhead vs the citadel's FortressArmor")
{
	if (!haveWorld("falsedef retail"))
	{
		return;
	}
	SharedWorld &s = shared();
	WeaponStores &stores = s.world->weaponStores();

	// The primary weapon: retail fields that bound the kill speed.
	const WeaponTemplate *rock = stores.weapons().findWeaponTemplate("AngmarStoneThrowerRock");
	REQUIRE(rock != nullptr);
	CHECK(rock->m_delayBetweenShotsMin == 40); // ANGMAR_STONETHROWER_ROCK_DELAYBETWEENSHOTS = 8000 ms
	CHECK(rock->m_delayBetweenShotsMax == 40);
	CHECK(rock->m_preAttackType == PREFIRE_PER_SHOT);
	CHECK(rock->m_preAttackDelay == 4); // 666 ms -> 3.33 frames, parsed per the retail rule
	CHECK(rock->m_attackRange == 400.0f);
	CHECK(rock->m_scatterRadius == 50.0f);

	// The warhead template the projectile's ProjectileNugget resolves.
	const WeaponTemplate *warhead = stores.weapons().findWeaponTemplate("AngmarStoneThrowerRockWarhead");
	REQUIRE(warhead != nullptr);
	const DamageNugget *siege = firstDamageNugget(warhead, DAMAGE_SIEGE);
	const DamageNugget *frost = firstDamageNugget(warhead, DAMAGE_FROST);
	REQUIRE(siege != nullptr);
	REQUIRE(frost != nullptr);
	CHECK(siege->m_damage == 500.0f);
	CHECK(siege->m_radius == 50.0f);
	CHECK(siege->m_damageScalar.size() == 3);
	CHECK(frost->m_damage == 100.0f);
	CHECK(frost->m_radius == 30.0f);

	// The citadel's armor set: Conditions=None Armor=FortressArmor on AngmarFortressCitadel.
	const ThingTemplate *citadel = s.world->things().findTemplate("AngmarFortressCitadel");
	REQUIRE(citadel != nullptr);
	bool fortressArmorFound = false;
	for (const ArmorTemplateSet &set : citadel->armorTemplateSets())
	{
		if (set.m_armorName == "FortressArmor")
		{
			fortressArmorFound = true;
		}
	}
	CHECK(fortressArmorFound);
	const ArmorTemplate *armor = stores.armors().findArmorTemplate("FortressArmor");
	REQUIRE(armor != nullptr);
	CHECK(armor->getCoefficient(DAMAGE_SIEGE) == 1.0f);
	CHECK(armor->getCoefficient(DAMAGE_FROST) == doctest::Approx(0.1f)); // tolerance: 10% parses to the nearest float, not the literal 0.1

	// Per direct hit through the real chain: no scalar admits the citadel, no source modifiers, no flank.
	SlingHost source;
	CitadelHost victim;
	DamageInfo sieg;
	REQUIRE(FillDamageInfo(*siege, *warhead, source, true, nullptr, sieg));
	CHECK(sieg.m_input.m_amount == 500.0f);
	CHECK(AdjustDamage(armor, armor, sieg.m_input, true, victim) == doctest::Approx(500.0f)); // tolerance: armor coefficient float multiply
	DamageInfo fro;
	REQUIRE(FillDamageInfo(*frost, *warhead, source, true, nullptr, fro));
	CHECK(fro.m_input.m_amount == 100.0f);
	CHECK(AdjustDamage(armor, armor, fro.m_input, true, victim) == doctest::Approx(10.0f)); // tolerance: 0.1f coefficient multiply

	// The retail floor for the UNUPGRADED rock: 7,500 health / 510 a hit = 15 direct hits. The save's slings
	// carried the IceRock upgrade (checked next).
	MESSAGE("falsedef retail: unupgraded 510 damage a direct hit");
}

// The weapon that actually did the observed kill: the trace of the owner save (engine/tests/
// test_script2_angmar.cpp "campaign owner save") shows both slings carrying AngmarStoneThrowerIceRock,
// whose warhead's SIEGE nugget is 900 with "DamageScalar = 150% ANY ENEMIES +STRUCTURE" — 1350 a hit
// against the citadel, plus 50 FROST = 1400 a volley. Two upgraded slings then legitimately kill a
// full-health 7,500 citadel in ~3 volleys: the ~170-frame kill is retail behaviour, not over-damage.
TEST_CASE("falsedef retail: AngmarStoneThrowerIceWarhead's structure scalar")
{
	if (!haveWorld("falsedef retail"))
	{
		return;
	}
	SharedWorld &s = shared();
	WeaponStores &stores = s.world->weaponStores();

	const WeaponTemplate *ice = stores.weapons().findWeaponTemplate("AngmarStoneThrowerIceWarhead");
	REQUIRE(ice != nullptr);
	const DamageNugget *siege = firstDamageNugget(ice, DAMAGE_SIEGE);
	const DamageNugget *frost = firstDamageNugget(ice, DAMAGE_FROST);
	REQUIRE(siege != nullptr);
	REQUIRE(frost != nullptr);
	CHECK(siege->m_damage == 900.0f); // ANGMAR_STONETHROWER_ROCK_DAMAGEUPGRADE
	CHECK(frost->m_damage == 500.0f); // ANGMAR_STONETHROWER_ICE_DAMAGE

	// The 150% ENEMIES +STRUCTURE scalar — the piece that makes the fast kill legitimate.
	const DamageScalarEntry *structureScalar = nullptr;
	for (const DamageScalarEntry &e : siege->m_damageScalar)
	{
		if (e.scalar == 1.5f && (e.filter.relationships & ObjectFilter::REL_ENEMIES) && e.filter.rule == ObjectFilter::RULE_ANY)
		{
			structureScalar = &e;
		}
	}
	REQUIRE(structureScalar != nullptr);
	// lane DEVPORT-8: the entry is the INI's fourth, `150% ANY ENEMIES +STRUCTURE` (data\ini\weapon.ini of _patch201ini.big): its include mask is the
	// STRUCTURE bit alone, and the three before it (0% ALL ALLIES, 25% ANY ENEMIES +INFANTRY, 25% ANY ENEMIES +CAVALRY) cannot take an enemy structure (the
	// first is ALLIES only; the others' masks do not have STRUCTURE): "the first DamageScalar whose filter allows the victim" (Damage.cpp step 3) is this one
	auto bit = [](const KindOfMaskType &m, int i) { return i >= 0 && ((m[(size_t)i / 32] >> ((unsigned)i % 32)) & 1u) != 0; };
	auto bits = [](const KindOfMaskType &m) { int n = 0; for (std::uint32_t w : m) { for (; w; w &= w - 1) { ++n; } } return n; };
	const int structure = ObjectTemplateInfoBuilder::kindOfIndex("STRUCTURE");
	REQUIRE(structure >= 0); // tolerance: kindOfIndex answers -1 for an unknown name; the index itself is the binary table's position
	REQUIRE(siege->m_damageScalar.size() == 4);
	CHECK(&siege->m_damageScalar[3] == structureScalar);
	CHECK(structureScalar->filter.relationships == (unsigned)ObjectFilter::REL_ENEMIES);
	CHECK(bit(structureScalar->filter.includeKindOf, structure));
	CHECK(bits(structureScalar->filter.includeKindOf) == 1);
	CHECK(siege->m_damageScalar[0].scalar == 0.0f);
	CHECK(siege->m_damageScalar[0].filter.rule == ObjectFilter::RULE_ALL);
	CHECK(siege->m_damageScalar[0].filter.relationships == (unsigned)ObjectFilter::REL_ALLIES);
	for (size_t i = 1; i < 3; ++i)
	{
		CHECK(siege->m_damageScalar[i].scalar == 0.25f);
		CHECK(siege->m_damageScalar[i].filter.rule == ObjectFilter::RULE_ANY);
		CHECK_FALSE(bit(siege->m_damageScalar[i].filter.includeKindOf, structure));
	}
	// the citadel is a STRUCTURE (its template's KindOf), so the fourth entry is the one that applies
	const ThingTemplate *citadel = s.world->things().findTemplate("AngmarFortressCitadel");
	REQUIRE(citadel != nullptr);
	CHECK(bit(ObjectTemplateInfoBuilder::build(*citadel).kindOf, structure));
	// the upgraded weapon fires it at the rock's rate: IceRock 8000 ms = 40 frames, PreAttackDelay 666 ms PER_SHOT (the macros of data\ini\gamedata.ini)
	const WeaponTemplate *iceRock = stores.weapons().findWeaponTemplate("AngmarStoneThrowerIceRock");
	REQUIRE(iceRock != nullptr);
	CHECK(iceRock->m_delayBetweenShotsMin == 40);
	CHECK(iceRock->m_preAttackType == PREFIRE_PER_SHOT);
	CHECK(iceRock->m_preAttackDelay == 4);

	// A source that admits only that entry's filter (an enemy STRUCTURE victim): 900 * 1.5 = 1350.
	class StructureHost : public SlingHost
	{
	public:
		explicit StructureHost(const ObjectFilter *allowed) : m_allowed(allowed) {}
		bool filterAllowsVictim(const ObjectFilter &f) override { return &f == m_allowed; }

	private:
		const ObjectFilter *m_allowed;
	};
	StructureHost source(&structureScalar->filter);
	DamageInfo info;
	REQUIRE(FillDamageInfo(*siege, *ice, source, true, nullptr, info));
	CHECK(info.m_input.m_amount == 1350.0f);

	const ArmorTemplate *armor = stores.armors().findArmorTemplate("FortressArmor");
	REQUIRE(armor != nullptr);
	CitadelHost victim;
	CHECK(AdjustDamage(armor, armor, info.m_input, true, victim) == doctest::Approx(1350.0f)); // tolerance: armor coefficient float multiply
	DamageInfo fro;
	REQUIRE(FillDamageInfo(*frost, *ice, source, true, nullptr, fro));
	CHECK(AdjustDamage(armor, armor, fro.m_input, true, victim) == doctest::Approx(50.0f)); // tolerance: 0.1f coefficient multiply
	MESSAGE("falsedef retail: IceRock sling 1350 + 50 = 1400 a volley; two slings kill 7,500 health "
		"in ~3 volleys (~130 frames) — the observed ~170-frame kill is retail-correct");
}

} // namespace
