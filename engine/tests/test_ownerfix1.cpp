// OpenBFME unit tests: lane OWNERFIX-1, problems the owner hit playing the current build (2026-10-15). Retail runs: they SKIP when ROTWK_INSTALL /
// BFME2_INSTALL are unset. GPL-3.0.

#include "doctest.h"
#include "CampaignTestUtil.h"
#include "HudTestUtil.h"

#include "Common/PlayerScience.h"
#include "Common/PlayerTemplate.h"
#include "Common/Science.h"
#include "Common/SpecialPower.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameClient/SpellBookUI.h"
#include "GameLogic/FXEvents.h"
#include "GameLogic/Map/TerrainLogic.h"
#include "GameLogic/SimMath.h"
#include "GameLogic/Module/SpecialPowerModules.h"

using campaigntest::Mission;

namespace
{
size_t logCount(const HudInput &in, const std::string &prefix)
{
	size_t n = 0;
	for (const std::string &l : in.messageLog())
	{
		if (l.compare(0, prefix.size(), prefix) == 0)
		{
			++n;
		}
	}
	return n;
}
} // namespace

// Problem 5 (the owner: "you can select units during the in-game cutscenes"). Retail: the map scripts' DISABLE_INPUT (RW 0x7BC586) turns InGameUI's
// input off, hides the pointer, sends MSG_DESTROY_SELECTED_GROUP (true) and deselects every drawable; the CommandTranslator (RW 0x81F8F2 .. 0x81F979)
// still lets that message through while the input is off; ENABLE_INPUT (RW 0x7BC5E9) turns the input and the pointer back on. Driven by synthetic
// mouse events through the HUD input stack into the logic, as the device does.
TEST_CASE("ownerfix1 cutscene input: DISABLE_INPUT drops the selection (also the logic's), a click or a drag box selects nothing, ENABLE_INPUT restores")
{
	if (!hudtest::haveWorld("ownerfix1 cutscene input"))
	{
		return;
	}
	hudtest::SharedWorld &s = hudtest::shared();
	hudtest::Rig r(s);
	float mx = 0, my = 0;
	REQUIRE(r.logic().terrain()->getExtent(0, mx, my));
	const Coord3D base = r.freeSpot(mx * 0.5f, my * 0.5f, 200.0f);
	const std::string infantry = hudtest::templates(s).infantry.front();
	Object *a = r.make(infantry, base.x, base.y);
	Object *b = r.make(infantry, base.x + 40.0f, base.y);
	r.lookAt({ base.x + 20.0f, base.y, 0.0f }, 300.0f, 380.0f);
	r.frame(2);
	auto px = [&](Object *o) { return r.screenOf({ o->getPosition()->x, o->getPosition()->y, o->getPosition()->z + 6.0f }); };
	// the control: with the input on, a click selects and the logic's selection follows
	const ICoord2D pa = px(a), pb = px(b);
	r.leftClick(pa.x, pa.y);
	r.frame(1);
	REQUIRE(r.local->selection() == std::vector<ObjectID>{ a->getID() });
	InGameUI &ui = r.input->ui();
	CHECK_FALSE(ui.scriptMouseHidden());
	// DISABLE_INPUT
	const size_t destroys = logCount(*r.input, "MSG_DESTROY_SELECTED_GROUP");
	ui.applyScriptInput(false);
	CHECK_FALSE(ui.getInputEnabled());
	CHECK(ui.scriptMouseHidden());
	CHECK(ui.selected().empty());
	r.frame(1);
	CHECK(logCount(*r.input, "MSG_DESTROY_SELECTED_GROUP") == destroys + 1); // the message went through the translators with the input off
	CHECK(r.local->selection().empty());                                     // and reached the logic
	// clicks and a drag box during the cutscene select nothing, here or in the logic
	const size_t creates = logCount(*r.input, "MSG_CREATE_SELECTED_GROUP");
	r.leftClick(pa.x, pa.y);
	r.leftDrag(pa.x - 30, pa.y - 40, pb.x + 30, pb.y + 40);
	r.frame(1);
	CHECK(ui.selected().empty());
	CHECK(logCount(*r.input, "MSG_CREATE_SELECTED_GROUP") == creates);
	CHECK(r.local->selection().empty());
	// ENABLE_INPUT: the pointer is back and the same click selects again
	ui.applyScriptInput(true);
	CHECK(ui.getInputEnabled());
	CHECK_FALSE(ui.scriptMouseHidden());
	r.leftClick(pb.x, pb.y);
	r.frame(1);
	CHECK(ui.selected() == std::vector<ObjectID>{ b->getID() });
	CHECK(r.local->selection() == std::vector<ObjectID>{ b->getID() });
	// the parts not ported are reported (InGameHud::stops)
	CHECK(InGameUI::scriptInputStopLine().rfind("[S-4964]", 0) == 0);
	CHECK(InGameUI::scriptInputStopLine().find("RW 0x83AB5D") != std::string::npos);
}

namespace
{
int countTemplate(GameLogic &logic, const std::string &name)
{
	int n = 0;
	for (Object *o = logic.getFirstObject(); o; o = o->getNextObject())
	{
		if (!o->isEffectivelyDead() && o->getTemplate() && o->getTemplate()->getName() == name)
		{
			++n;
		}
	}
	return n;
}
} // namespace

// Problem 1 (the owner, MAP ANG Angmar: "I can't use the powers I earn"). Retail facts, each independent of the code under test:
//   * Player::newMap (RW 0x6B183D, BFME2 decomp PlayerNewMapBFME2.cpp) makes the player's spell book object on every new map, a campaign mission included
//     (PlayerList::newMap RW 0x6A850B has no game mode test): FactionAngmar's SpellBook = AngmarSpellBook (data\ini\playertemplate.ini);
//   * a campaign game reads Rank.ini's SkillPointsNeededCampaign (RW 0x5FF924 -> RW 0x626355): rank 2 needs 100 skill points there (60 in a skirmish);
//     rank 1 grants SciencePurchasePointsGranted = 5 (the 5 points at the start);
//   * the map's 'Power Restrictions' script runs PLAYER_SET_MAX_SPELLPOINTS(<Local Player>, 15) (RW 0x7BD15B): the rank cap becomes the first rank whose
//     summed grants reach 15: rank 1 grants 5 and ranks 2 .. 11 grant 1 each (PLAYER_PURCHASE_POINTS_GRANTED, data\ini\gamedata.ini), so rank 11;
//     addSkillPoints (RW 0x782AA4) stops at it: 15 points in all;
//   * the store (AngmarSpellStoreCommandSet) sells Chill Wind for EVIL_RANK_1_COST = 5 (data\ini\science.ini); a bought power is cast from the spell
//     book bar at a position: Chill Wind is an OCLSpecialPower creating SpellBookChillWind (OCL_SpellBookChillWind).
// The flow is the player's: skill points as kills award them (the logic's addSkillPoints, 50 at a time), the store bought through its model (the clicks,
// the purchase messages of its close through the command list), the cast through the spell book bar's press and ground click (its message through the
// command list), the result checked in the world.
TEST_CASE("ownerfix1 campaign powers: MAP ANG Angmar has a spell book, the campaign ranks, the 15-point cap; Chill Wind is earned, bought and cast")
{
	OPENBFME_REQUIRE_START(s);
	Mission m;
	m.load(*s);
	Player *p = m.logic().players().getLocalPlayer();
	REQUIRE(p != nullptr);
	REQUIRE(p->getPlayerTemplate() != nullptr);
	CHECK(p->getPlayerTemplate()->getName() == "FactionAngmar");
	PlayerScience &sc = p->science();
	CHECK(sc.mode().campaign);
	CHECK_FALSE(sc.mode().skirmishOrMultiplayer);
	CHECK(sc.getRankLevel() == 1);
	CHECK(sc.getSciencePurchasePoints() == 5);
	CHECK(sc.getSkillPointsLevelUp() == 100);
	Object *book = SpecialPowerModules::findSpellBookObject(m.logic(), *p);
	REQUIRE_MESSAGE(book != nullptr, "the local player has no spell book object in the campaign mission");
	CHECK(book->getTemplate()->getName() == "AngmarSpellBook");
	// the map's 'Power Restrictions' (one shot, at the start)
	m.step(5);
	CHECK(sc.getRankLevelCap() == 11);
	// earn: skill points in kill-sized steps until no rank comes any more
	for (int i = 0; i < 60; ++i)
	{
		sc.addSkillPoints(50.0f, true);
	}
	CHECK(sc.getRankLevel() == 11);
	CHECK(sc.getSciencePurchasePoints() == 15);
	// the store: Chill Wind (AngmarSpellStoreCommandSet slot 2) is available for 5
	SpellStoreModel store;
	std::string error;
	REQUIRE_MESSAGE(store.open(s->world->commands(), m.logic(), *p, &error), error);
	int chill = -1;
	for (const SpellStoreModel::Button &b : store.buttons())
	{
		if (b.scienceName == "SCIENCE_ChillWind")
		{
			chill = b.index;
			CHECK(b.cost == 5);
			CHECK(b.state == SpellStoreModel::STATE_AVAILABLE);
		}
	}
	REQUIRE(chill == 1);
	CHECK(store.click(chill));
	const std::vector<GameMessage> buy = store.close();
	REQUIRE(buy.size() == 1u);
	for (const GameMessage &msg : buy)
	{
		m.game->commands().append(msg);
	}
	m.step(2);
	CHECK(sc.hasScience(TheScienceStore->getScienceFromInternalName("SCIENCE_ChillWind")));
	CHECK(sc.getSciencePurchasePoints() == 10);
	// the cast: the spell book bar's Chill Wind is owned and ready; press, then the ground click in front of the Witch King
	InGameSpellBookModel bar;
	REQUIRE(bar.refresh(s->world->commands(), m.logic(), *p));
	int slot = -1;
	for (const InGameSpellBookModel::Button &b : bar.buttons())
	{
		if (b.power && b.power->getName() == "SpellBookChillWind")
		{
			slot = b.index;
			CHECK(b.owned);
			CHECK(b.ready);
		}
	}
	REQUIRE(slot == 1); // AngmarSpellBookCommandSet slot 2 (data\ini\commandset.ini)
	Object *wk = m.unit("Witch King");
	REQUIRE(wk != nullptr);
	Coord3D at = *wk->getPosition();
	at.x += 150.0f;
	at.z = m.logic().getGroundHeight(at.x, at.y);
	const int before = countTemplate(m.logic(), "SpellBookChillWind");
	std::vector<GameMessage> cast;
	CHECK(bar.press(slot, p->getPlayerIndex(), cast));
	REQUIRE(bar.targeting());
	REQUIRE(bar.clickWorld(at, p->getPlayerIndex(), cast));
	REQUIRE(cast.size() == 1u);
	m.game->commands().append(cast[0]);
	m.step(2);
	CHECK(countTemplate(m.logic(), "SpellBookChillWind") == before + 1);
	REQUIRE(bar.refresh(s->world->commands(), m.logic(), *p));
	for (const InGameSpellBookModel::Button &b : bar.buttons())
	{
		if (b.index == slot)
		{
			CHECK_FALSE(b.ready); // recharging after the cast
		}
	}
}

// PLAYER_SET_MAX_SPELLPOINTS's rank search (RW 0x782942) against Rank.ini's grants (rank 1: 5, every other rank: 1), independent sums: 5 -> rank 1,
// 6 -> 2, 15 -> 11, 0 -> 1 (the first rank already reaches it), more than the whole table grants -> 0 (no cap)
TEST_CASE("ownerfix1 rank for purchase points: RW 0x782942 over the retail rank table")
{
	OPENBFME_REQUIRE_START(s);
	Mission m;
	m.load(*s);
	Player *p = m.logic().players().getLocalPlayer();
	REQUIRE(p != nullptr);
	const PlayerScience &sc = p->science();
	CHECK(sc.rankForPurchasePoints(5) == 1);
	CHECK(sc.rankForPurchasePoints(6) == 2);
	CHECK(sc.rankForPurchasePoints(15) == 11);
	CHECK(sc.rankForPurchasePoints(0) == 1);
	CHECK(sc.rankForPurchasePoints(100000) == 0);
}

// Problem 3 (the owner: "the heroes don't seem to have the particles around them that indicate they are healing"). Retail: ActiveBody::attemptHealing
// (RW 0x8C2FC1, BFME2 decomp ActiveBody.cpp) plays the body's healing FX whenever a heal raises the health (RW 0x8C30F7 .. 0x8C3113), and every
// ActiveBodyModuleData starts with GameData's DefaultUnitHealingBuffFxList (RW 0x8C373F reads GlobalData + 0x9DC) = FX_DefaultUnitHealBuff in RotWK's
// gamedata.ini (data\ini\gamedata.ini: a BuffNugget of HealingBuff, the EXHealbuff glow). The Witch King has no HealingBuffFx row of his own and an
// AutoHealBehavior (StartsActive, HealingAmount 20, HealingDelay 1000, StartHealingDelay 15000, HealOnlyIfNotInCombat): hurt and alone, each of his
// heals must play exactly one FX_DefaultUnitHealBuff on him; a structure (template KindOf STRUCTURE, RW 0x8C30EE) plays none.
TEST_CASE("ownerfix1 healing glow: each self-heal of the Witch King plays GameData's FX_DefaultUnitHealBuff on him; a healed structure plays none")
{
	if (!hudtest::haveWorld("ownerfix1 healing glow"))
	{
		return;
	}
	hudtest::SharedWorld &s = hudtest::shared();
	hudtest::Rig r(s);
	CHECK(r.logic().settings().defaultUnitHealingBuffFxList == "FX_DefaultUnitHealBuff");
	float mx = 0, my = 0;
	REQUIRE(r.logic().terrain()->getExtent(0, mx, my));
	const Coord3D at = r.freeSpot(mx * 0.5f, my * 0.5f, 400.0f);
	Object *wk = r.make("AngmarWitchking", at.x, at.y);
	REQUIRE(wk != nullptr);
	r.frame(2);
	BodyModuleInterface *body = wk->getBodyModule();
	REQUIRE(body != nullptr);
	DamageInfo hurt;
	hurt.m_input.m_damageType = DAMAGE_UNRESISTABLE;
	hurt.m_input.m_amount = SimMath::mulf32(body->getMaxHealth(), 0.5f);
	wk->attemptDamage(hurt);
	float last = body->getHealth();
	int rises = 0, glows = 0, otherFx = 0;
	for (int f = 0; f < 150; ++f) // 30 s: StartHealingDelay 15 s, then a heal a second
	{
		r.frame(1);
		if (body->getHealth() > last)
		{
			++rises;
		}
		last = body->getHealth();
		for (const FXEvent &e : r.logic().fxEvents().frameEvents())
		{
			if (std::string(e.site) == "HealingBuffFx")
			{
				CHECK(e.primary == wk->getID());
				if (e.fxList && *e.fxList == "FX_DefaultUnitHealBuff")
				{
					++glows;
				}
				else
				{
					++otherFx;
				}
			}
		}
	}
	MESSAGE("the Witch King healed " << rises << " times in 30 s, " << glows << " healing glows");
	CHECK(rises >= 10); // tolerance: the heal count depends on when AutoHeal's random first wake lands; the property is one glow per heal
	CHECK(glows == rises);
	CHECK(otherFx == 0);
	// a structure: hurt, then healed by a HEALING hit (Object::attemptHealing RW 0x690532): its health rises and no healing glow plays
	const Coord3D at2 = r.freeSpot(at.x + 500.0f, at.y, 300.0f);
	Object *barracks = r.make("AngmarBarracks", at2.x, at2.y);
	REQUIRE(barracks != nullptr);
	REQUIRE(barracks->isKindOfName("STRUCTURE"));
	r.frame(1);
	BodyModuleInterface *bb = barracks->getBodyModule();
	DamageInfo hit;
	hit.m_input.m_damageType = DAMAGE_UNRESISTABLE;
	hit.m_input.m_amount = 300.0f;
	barracks->attemptDamage(hit);
	const float before = bb->getHealth();
	r.logic().fxEvents().beginFrame();
	barracks->attemptHealing(100.0f, nullptr);
	CHECK(bb->getHealth() == SimMath::addf32(before, 100.0f));
	int structureGlows = 0;
	for (const FXEvent &e : r.logic().fxEvents().frameEvents())
	{
		if (std::string(e.site) == "HealingBuffFx")
		{
			++structureGlows;
		}
	}
	CHECK(structureGlows == 0);
	// the same HEALING hit on the hurt Witch King plays it once (the control of the structure case)
	wk->attemptDamage(hit);
	r.logic().fxEvents().beginFrame();
	wk->attemptHealing(100.0f, nullptr);
	int unitGlows = 0;
	for (const FXEvent &e : r.logic().fxEvents().frameEvents())
	{
		unitGlows += std::string(e.site) == "HealingBuffFx" ? 1 : 0;
	}
	CHECK(unitGlows == 1);
}
