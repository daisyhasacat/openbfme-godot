// OpenBFME retail tests of the hero HUD (lane HERO-1 part 2, S-850): the fortress's REVIVE buttons on the Palantir show the player's hero records (image, cost,
// state, the revive / recruit progress) and a press recruits through the build-index message; a hero's ability buttons show locked / ready / recharging with the
// recharge clock, and a press casts the ability through the message path. They SKIP when ROTWK_INSTALL / BFME2_INSTALL are unset. GPL-3.0.

#include "HudTestUtil.h"

#include "Common/PlayerHeroList.h"
#include "Common/SpecialPower.h"
#include "GameClient/GUI/ShellServices.h"
#include "GameClient/InGameHud.h"
#include "GameLogic/Module/ProductionUpdate.h"
#include "GameLogic/Module/SpecialAbilityModules.h"
#include "GameLogic/Module/SpecialPowerModules.h"
#include "GameLogic/Object/ExperienceTracker.h"

using namespace hudtest;

namespace
{
struct HeroHudRig
{
	Rig rig;
	RecordingShellServices services;
	std::unique_ptr<InGameHud> hud;
	explicit HeroHudRig(SharedWorld &s) : rig(s)
	{
		InGameHud::Config cfg{ *rig.game, *s.world, *s.mount->fs, services, rig.view, s.mouse, s.meta, nullptr };
		hud = std::make_unique<InGameHud>(cfg);
		std::string error;
		REQUIRE_MESSAGE(hud->boot(&error), error);
	}
	~HeroHudRig() { hud.reset(); }
	void frames(int n)
	{
		for (int i = 0; i < n; ++i)
		{
			hud->update(0.033);
			rig.game->advance(0.033);
		}
	}
	// a left click on the object through the HUD's raw input: the selection reaches the logic as the lockstep message (MSG_CREATE_SELECTED_GROUP)
	void select(Object *o)
	{
		rig.lookAt(*o->getPosition());
		frames(5);
		const ICoord2D px = rig.screenOf({ o->getPosition()->x, o->getPosition()->y, 10.0f });
		hud->mouseMove(px.x, px.y);
		hud->mouseButton(HudInput::Button::Left, true, px.x, px.y, 0, rig.timeMs += 500);
		hud->mouseButton(HudInput::Button::Left, false, px.x, px.y, 0, rig.timeMs += 40);
		frames(20);
	}
	std::vector<ControlBarButton> buttons(GUICommandType type)
	{
		std::vector<ControlBarButton> out;
		for (const auto *v : { &hud->controlBar().palantirButtons(), &hud->controlBar().sideButtons() })
		{
			for (const ControlBarButton &b : *v)
			{
				if (b.button && b.button->m_command == type)
				{
					out.push_back(b);
				}
			}
		}
		return out;
	}
};
} // namespace

TEST_CASE("rotwk-c availability: common gates preserve hidden results and model masks use any match")
{
	if (!haveWorld("rotwk-c availability"))
	{
		return;
	}
	SharedWorld &s = shared();
	struct ButtonFixture
	{
		CommandSet *set;
		CommandButton button;
		~ButtonFixture() { set->setDynamicButton(0, nullptr); }
	} fixture{ s.world->commands().newDynamicCommandSet("RotwkC_Availability") };
	CommandButton &b = fixture.button;
	b.m_command = GUI_COMMAND_STOP;
	b.m_inPalantir = true;
	fixture.set->setDynamicButton(0, &b);
	HeroHudRig h(s);
	auto ctx = h.hud->enterContext(); // lane DEVPORT-3C: the stores (TheSpecialPowerStore below) are this world's, not a world an earlier test left
	const Coord3D c = h.rig.freeSpot(2800, 1400, 300.0f);
	Object *hero = h.rig.make("GondorAragornMP", c.x, c.y);
	REQUIRE(hero);
	hero->setCommandSetOverride("RotwkC_Availability");
	h.hud->input().ui().selectObject(hero->getID());
	REQUIRE(h.hud->input().ui().firstSelected() == hero->getID());
	auto state = [&]() {
		h.hud->controlBar().update();
		const auto &buttons = h.hud->controlBar().palantirButtons();
		return buttons.empty() ? ButtonState::Hidden : buttons.front().state;
	};
	REQUIRE(state() == ButtonState::Enabled);
	b.m_requireLevel = 100;
	CHECK(state() == ButtonState::Hidden);
	b.m_requireLevel = 0;
	hero->setScriptStatus(1, true);
	CHECK(state() == ButtonState::Hidden);
	hero->setScriptStatus(1, false);
	hero->setDisabled(DISABLED_UNMANNED, UPDATE_SLEEP_FOREVER);
	CHECK(state() == ButtonState::Hidden);
	hero->clearDisabled(DISABLED_UNMANNED);
	hero->setDisabled(DISABLED_TEMPORARILY_BUSY, UPDATE_SLEEP_FOREVER);
	CHECK(state() == ButtonState::Enabled);
	hero->setDisabled(DISABLED_HELD, UPDATE_SLEEP_FOREVER);
	CHECK(state() == ButtonState::Restricted);
	b.m_requireLevel = 100;
	CHECK(state() == ButtonState::Hidden);
	b.m_requireLevel = 0;
	hero->clearDisabled(DISABLED_HELD);
	hero->clearDisabled(DISABLED_TEMPORARILY_BUSY);
	b.m_options = COMMAND_OPTION_NEED_UPGRADE;
	b.m_neededUpgradeAny = true;
	CHECK(state() == ButtonState::Restricted); // even an empty ANY list has no satisfied upgrade
	b.m_neededUpgradeAny = false;
	CHECK(state() == ButtonState::Enabled);
	b.m_neededUpgrade = { "Upgrade_RingHero", "Upgrade_FortressRingHero" };
	h.rig.local->addCompletedUpgrade("Upgrade_RingHero");
	CHECK(state() == ButtonState::Restricted); // the player upgrade alone is insufficient
	hero->giveUpgrade("Upgrade_FortressRingHero");
	CHECK(state() == ButtonState::Enabled);
	b.m_options = 0;

	// Two bits in distinct words: one intersection suffices; a disable mask wins over the enable mask.
	b.m_enableOnModelCondition[0] = 1u;
	b.m_enableOnModelCondition[18] = 1u;
	hero->setModelConditionState(0, false);
	hero->setModelConditionState(576, false);
	CHECK(state() == ButtonState::Restricted);
	hero->setModelConditionState(576, true);
	CHECK(state() == ButtonState::Enabled);
	b.m_disableOnModelCondition[18] = 1u;
	CHECK(state() == ButtonState::Restricted);
	b.m_command = GUI_COMMAND_SPECIAL_POWER;
	b.m_specialPowerName = "SpecialAbilityAragornAthelas";
	CHECK(state() == ButtonState::Restricted); // SPECIAL_POWER defers the helper to its readiness branch; it does not bypass it
	b.m_enableOnModelCondition.fill(0);
	b.m_disableOnModelCondition.fill(0);
	REQUIRE(state() == ButtonState::Enabled);
	auto *power = dynamic_cast<SpecialPowerModule *>(SpecialPowerModules::findModule(*hero,
		TheSpecialPowerStore->findSpecialPowerTemplate(b.m_specialPowerName)));
	REQUIRE(power);
	power->pauseCountdown(true);
	CHECK(state() == ButtonState::NotReady); // a ready power paused at one retains its clock
	power->pauseCountdown(false);
	power->setReadyFrame(h.rig.logic().getFrame() + power->rechargeLength());
	power->pauseCountdown(true);
	CHECK(state() == ButtonState::Restricted); // paused at zero
	power->pauseCountdown(false);
	CHECK(state() == ButtonState::NotReady); // unpaused at zero
	b.m_disableOnModelCondition[18] = 1u;
	CHECK(state() == ButtonState::Restricted); // the deferred mask also gates recharge
	b.m_disableOnModelCondition.fill(0);
	Object *fighter = h.rig.make("GondorFighter", c.x + 100.0f, c.y);
	REQUIRE(fighter);
	fighter->setCommandSetOverride("RotwkC_Availability");
	h.hud->input().ui().deselectAll(false);
	h.hud->input().ui().selectObject(fighter->getID());
	CHECK(state() == ButtonState::Enabled); // no Athelas module: RW's default enabled result, not hidden
	b.m_showButton = false;
	CHECK(state() == ButtonState::Hidden);
	CHECK(h.hud->controlBar().pressButton(0, true) == false);
}

TEST_CASE("rotwk-c availability: hero query distinguishes idle, zero, partial, complete and zero duration")
{
	if (!haveWorld("rotwk-c hero availability"))
	{
		return;
	}
	HeroHudRig h(shared());
	const Coord3D c = h.rig.freeSpot(2800, 1400, 300.0f);
	Object *keep = h.rig.make("MenFortressCitadel", c.x, c.y);
	REQUIRE(keep);
	h.frames(30);
	PlayerHeroList &list = h.rig.local->heroes();
	HeroRecord *r = list.at(0);
	REQUIRE(r);
	unsigned cost = 123;
	CHECK(list.availabilityAt(h.rig.logic(), *h.rig.local, 0, keep, &cost) == 1.0);
	CHECK(cost == (unsigned)list.costAt(h.rig.logic(), *h.rig.local, 0, keep));
	const int frames = list.framesAt(h.rig.logic(), *h.rig.local, 0, keep);
	REQUIRE(frames > 0); // tolerance: the template's production time is data-driven; only a positive duration is required.
	r->startFrame = (int)h.rig.logic().getFrame();
	cost = 123;
	CHECK(list.availabilityAt(h.rig.logic(), *h.rig.local, 0, keep, &cost) == 0.0);
	CHECK(cost == 123); // active records never overwrite the caller's cost
	r->startFrame -= frames;
	CHECK(list.availabilityAt(h.rig.logic(), *h.rig.local, 0, keep, &cost) == 1.0);
	r->startFrame += frames / 2;
	const double partial = list.availabilityAt(h.rig.logic(), *h.rig.local, 0, keep, &cost);
	CHECK(partial > 0.0); // tolerance: frame rounding can produce any positive fractional progress.
	CHECK(partial < 1.0); // tolerance: frame rounding can produce any fractional progress below completion.
	r->seconds = 0;
	CHECK(list.availabilityAt(h.rig.logic(), *h.rig.local, 0, keep, &cost) == 0.0);
	CHECK(list.progressAt(h.rig.logic(), *h.rig.local, 0, keep) == 1.0); // the production query deliberately differs
	CHECK(list.availabilityAt(h.rig.logic(), *h.rig.local, -1, keep, &cost) == 0.0);
}

TEST_CASE("hero hud: the fortress's REVIVE buttons show the hero records and a press recruits; the hero's ability buttons show their state and recharge clock")
{
	if (!haveWorld("hero hud"))
	{
		return;
	}
	SharedWorld &s = shared();
	HeroHudRig h(s);
	h.rig.local->getMoney()->deposit(100000);
	const Coord3D c = h.rig.freeSpot(2800, 1400, 300.0f);
	Object *keep = h.rig.make("MenFortressCitadel", c.x, c.y);
	REQUIRE(keep != nullptr);
	h.rig.lookAt({ c.x, c.y, 0 });
	h.frames(20);
	PlayerHeroList &list = h.rig.local->heroes();
	REQUIRE(list.size() > 0);
	h.hud->setWindowSize(1024, 768);
	h.rig.view.setScreen(1024, 768);
	h.select(keep);
	REQUIRE(h.hud->input().ui().selected() == std::vector<ObjectID>{ keep->getID() });
	// the hero menu (Command_SelectRevivablesMenFortress pushes the range of the REVIVE slots)
	bool pushed = false;
	for (const ControlBarButton &b : h.buttons(GUI_COMMAND_PUSH_VISIBLE_COMMAND_RANGE))
	{
		if (!pushed && b.image == "UCCommon_GoodHeroes")
		{
			pushed = h.hud->controlBar().pressButton(b.slot, b.inPalantir);
		}
	}
	REQUIRE(pushed);
	h.frames(5);
	std::vector<ControlBarButton> revive = h.buttons(GUI_COMMAND_REVIVE);
	// RW 0x943D6F: the ring hero and Create-A-Hero slots hide without Upgrade_RingHero / Upgrade_AllowBuildCreateAHero (NEED_UPGRADE, HIDE_WHILE_DISABLED); each
	// generic slot shows its template's record (the seven heroes)
	size_t heroes = 0;
	for (const HeroRecord &r : list.records())
	{
		heroes += r.templateName != "CreateAHero" ? 1 : 0;
	}
	REQUIRE(revive.size() == heroes);
	int aragornIndex = -1;
	for (size_t i = 0; i < revive.size(); ++i)
	{
		REQUIRE(revive[i].reviveIndex >= 0);
		const HeroRecord *r = list.at(revive[i].reviveIndex);
		REQUIRE(r);
		CHECK(revive[i].cost == list.costAt(h.rig.logic(), *h.rig.local, revive[i].reviveIndex, keep));
		CHECK_MESSAGE(!revive[i].image.empty(), r->templateName << " has no button image");
		if (r->templateName == "GondorGandalf")
		{
			// RW 0x7810BC: Gandalf the Grey's image until SCIENCE_GandalftheWhite; the 2.01 data has no such science (RW 0x5FEF8F answers -1), so the template's
			CHECK(revive[i].image == "HIGandalf");
		}
		if (r->templateName == "GondorAragornMP")
		{
			aragornIndex = revive[i].reviveIndex;
			CHECK(revive[i].state == ButtonState::Enabled);
		}
	}
	REQUIRE(aragornIndex >= 0);
	const ControlBarButton *aragornButton = nullptr;
	for (const ControlBarButton &b : revive)
	{
		aragornButton = b.reviveIndex == aragornIndex ? &b : aragornButton;
	}
	REQUIRE(aragornButton != nullptr);
	MESSAGE("REVIVE buttons: " << revive.size() << ", Aragorn's image " << aragornButton->image << " cost " << aragornButton->cost);
	// a press recruits Aragorn: the button shows the progress, then the hero comes
	REQUIRE(h.hud->controlBar().pressButton(aragornButton->slot, aragornButton->inPalantir));
	h.frames(30);
	revive = h.buttons(GUI_COMMAND_REVIVE);
	const ControlBarButton *pending = nullptr;
	for (const ControlBarButton &b : revive)
	{
		if (b.reviveIndex == aragornIndex)
		{
			pending = &b;
		}
	}
	REQUIRE(pending != nullptr);
	CHECK(pending->state == ButtonState::NotReady);
	CHECK(pending->timer > 0.0f);
	CHECK(pending->timer < 1.0f);
	Object *aragorn = nullptr;
	for (int i = 0; i < 3000 && !aragorn; ++i)
	{
		h.frames(1);
		for (Object *o = h.rig.logic().getFirstObject(); o; o = o->getNextObject())
		{
			if (o->getTemplate()->getName() == "GondorAragornMP" && !o->isEffectivelyDead())
			{
				aragorn = o;
			}
		}
	}
	REQUIRE(aragorn != nullptr);
	// the hero's ability buttons: Elendil is locked (its level unlocks it), Athelas ready
	for (int i = 0; i < 60; ++i)
	{
		h.frames(1); // out of the fortress
	}
	h.select(aragorn);
	REQUIRE(h.hud->input().ui().selected() == std::vector<ObjectID>{ aragorn->getID() });
	auto powerButton = [&](const char *power) -> ControlBarButton {
		for (const ControlBarButton &b : h.buttons(GUI_COMMAND_SPECIAL_POWER))
		{
			if (b.button->m_specialPowerName == power)
			{
				return b;
			}
		}
		return ControlBarButton{};
	};
	const ControlBarButton elendil0 = powerButton("SpecialAbilityAragornElendil");
	REQUIRE(elendil0.button != nullptr);
	CHECK(elendil0.state == ButtonState::Restricted);
	aragorn->getExperienceTracker()->gainExpForLevel(20, true, false);
	h.frames(30);
	const ControlBarButton elendil = powerButton("SpecialAbilityAragornElendil");
	REQUIRE(elendil.button != nullptr);
	REQUIRE(elendil.state == ButtonState::Enabled);
	// the press casts it (no target): the update runs it, and the button recharges with its clock
	REQUIRE(h.hud->controlBar().pressButton(elendil.slot, elendil.inPalantir));
	SpecialAbilityUpdate *su = nullptr;
	for (const std::unique_ptr<BehaviorModule> &m : aragorn->modules())
	{
		SpecialAbilityUpdate *u = dynamic_cast<SpecialAbilityUpdate *>(m.get());
		su = (u && u->abilityData()->m_specialPowerTemplateName == "SpecialAbilityAragornElendil") ? u : su;
	}
	REQUIRE(su != nullptr);
	for (int i = 0; i < 600 && su->abilitiesTriggered() == 0; ++i)
	{
		h.frames(1);
	}
	CHECK(su->abilitiesTriggered() > 0);
	h.frames(10);
	const ControlBarButton after = powerButton("SpecialAbilityAragornElendil");
	CHECK(after.state == ButtonState::NotReady);
	CHECK(after.timer >= 0.0f);
	CHECK(after.timer < 1.0f);
	// the Palantir shows the clock under the button's timer key (a TimerOverlay's `_timerId` = "<clip path>_Timer") next to the ability's image ("<clip path>_Image")
	h.frames(5);
	bool timerShown = false;
	for (const auto &kv : h.hud->palantir()->images())
	{
		const std::string base = kv.first.size() > 6 ? kv.first.substr(0, kv.first.size() - 6) : kv.first;
		timerShown = timerShown || (kv.second.image == after.image && h.hud->palantir()->timerFor(base + "_Timer") >= 0.0f);
	}
	CHECK(timerShown);
	MESSAGE("Elendil recharge " << after.timer << ", a Palantir timer shown: " << timerShown);
}
