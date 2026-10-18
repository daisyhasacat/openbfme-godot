// OpenBFME unit tests. GPL-3.0.
// Lane SAVE-UI1: the owner's report of 2026-10-15 (the quit menu's Save left the front-end background over the game and no way back), through the
// real shell, the retail QuitMenu.apt / SaveLoad.apt / Background.apt movies and the screens' own commands. The expected backgrounds are the binary's:
// the quit menu's ctor RW 0x6230B6(2) (RW 0x92262A), its dtor RW 0x622C88(0) (RW 0x921A96) or, after Exit (+ 0x27C), RW 0x6230B6(1) (RW 0x921A7C);
// SaveLoad's ctor shows the front-end background only when none is up (RW 0x818BC2 .. 0x818BDE, + 0x2A4) and its dtor hides what it showed (RW
// 0x818501); its input procedure (RW 0x816627) closes it on Escape's release in state 0; after the saved box's Ok (state 10, RW 0x816879) its dtor
// closes the quit menu (RW 0x8184C0). The game-side flow (clicks, keys, the HUD) is tests/saveui1_test.gd. SKIP loudly without the retail installs.

#include "doctest.h"

#include "StartShellFx.h"

#include "GameClient/GUI/AptScreens/AptQuitMenu.h"
#include "GameClient/GUI/AptScreens/AptSaveLoad.h"
#include "GameClient/GUI/Gadget.h"
#include "GameClient/GUI/GameWindow.h"
#include "GameClient/GUI/SaveLoadInfo.h"
#include "GameClient/GUI/ShellEnvironment.h"

#include "Libraries/Source/Apt/AptButtonInst.h"
#include "Libraries/Source/Apt/Apt.h"

#include <algorithm>
#include <functional>
#include <memory>
#include <string>

namespace
{
// the buttons of a level the pointer can press now (each at its centre: the button the hit test answers there), sorted
std::vector<std::string> pressableButtons(starttest::ShellFx &fx, int level)
{
	std::vector<std::string> out;
	std::function<void(AptCharacterInst *)> walk = [&](AptCharacterInst *c) {
		if (!c)
		{
			return;
		}
		if (AptButtonInst *b = c->asButton())
		{
			float x0, y0, x1, y1, x, y;
			if (b->contentBounds(x0, y0, x1, y1))
			{
				b->globalMatrix().apply((x0 + x1) / 2, (y0 + y1) / 2, x, y);
				if (fx.wm->apt().input().hitTestButtons(x, y) == b)
				{
					out.push_back(b->targetPath());
				}
			}
		}
		if (AptSpriteInst *sp = c->asSprite())
		{
			for (AptCharacterInst *k : sp->children())
			{
				walk(k);
			}
		}
	};
	walk(fx.wm->apt().level(level));
	std::sort(out.begin(), out.end());
	return out;
}

struct Game
{
	starttest::ShellFx fx;
	QuitMenuContext context;
	SaveLoadInfo info;
	std::unique_ptr<AptQuitMenu> quit;

	explicit Game(starttest::Shared &s) : fx(s, 7)
	{
		fx.text.set("GUI:NewSaveGame", "**NEW SAVE FILE**");
		fx.text.set("GUI:DefaultSaveFileName", "Saved Game %d");
		fx.text.set("APT:SaveGameProgress", "Save Progress");
		fx.text.set("GUI:GameSaved", "Game Saved");
		fx.environment.services = &fx.services;
		REQUIRE(fx.wm->loadBackground());
		fx.tick(3);
		REQUIRE(fx.wm->backgroundMode() == 0); // a running game: no background (the game's start hid it, RW 0x622C88)
		context.inGame = true;
		context.gameMode = 2; // a skirmish
		context.gameKind = 3;
		fx.environment.quitMenu = &context;
		info.mode = 3;
		info.flags = 2;
		info.inGame = true;
		fx.environment.saveLoad = &info;
	}
	void openQuit()
	{
		quit = std::make_unique<AptQuitMenu>(*fx.wm, *fx.shell, fx.environment);
		REQUIRE(quit->level() >= 0); // tolerance: loaded into the first free slot, whichever the window manager has
		fx.tick(10);
		fx.shell->setOverlay(quit.get()); // the device's quit_menu open: the menu has its place on the shell (OPTS-6)
	}
	AptSaveLoad *pushPage()
	{
		fx.shell->push("SaveLoad.apt");
		REQUIRE(fx.shell->errors().empty());
		AptSaveLoad *page = dynamic_cast<AptSaveLoad *>(fx.shell->top());
		REQUIRE(page);
		fx.tick(30);
		return page;
	}
	bool pageUp() { return fx.shell->top() && fx.shell->top()->filename() == "SaveLoad.apt"; }
};
} // namespace

TEST_CASE("saveui1 retail: the quit menu shows the in-game background and hides it when it closes; Exit leaves the front-end one up")
{
	OPENBFME_REQUIRE_START(s);
	Game g(*s);
	g.openQuit();
	CHECK(g.fx.wm->backgroundMode() == 2);
	SUBCASE("Resume (no Exit): hidden, the game is seen again")
	{
		g.quit.reset();
		CHECK(g.fx.wm->backgroundMode() == 0);
	}
	SUBCASE("Exit (+ 0x27C): the front-end background")
	{
		g.fx.wm->fscommand("AptQuitMenu::ExitMission", "");
		CHECK(g.fx.services.countRequests(ShellAction::QuitMenuExit) == 1);
		g.quit.reset();
		CHECK(g.fx.wm->backgroundMode() == 1);
	}
	SUBCASE("a skirmish's Restart (+ 0x27D): the background stays through the restart")
	{
		g.fx.wm->fscommand("AptQuitMenu::RestartMission", "");
		CHECK(g.fx.services.countRequests(ShellAction::QuitMenuRestart) == 1);
		g.quit.reset();
		CHECK(g.fx.wm->backgroundMode() == 2);
	}
}

TEST_CASE("saveui1 retail: SaveLoad over the quit menu keeps the in-game background; with none up it shows the front-end one and hides it when it goes")
{
	OPENBFME_REQUIRE_START(s);
	Game g(*s);
	SUBCASE("over the quit menu (the owner's flow)")
	{
		g.openQuit();
		AptSaveLoad *page = g.pushPage();
		CHECK_FALSE(page->showedBackground());
		CHECK(g.fx.wm->backgroundMode() == 2);
		g.fx.wm->fscommand("AptSaveLoad::Cancel", "");
		g.fx.tick(3);
		CHECK_FALSE(g.pageUp());
		CHECK(g.fx.wm->backgroundMode() == 2); // the menu's background, not the page's front-end one
		g.quit.reset();
		CHECK(g.fx.wm->backgroundMode() == 0); // Resume: nothing left over the game
	}
	SUBCASE("with no background up: shown, and hidden when the page closes (before: left up over the game)")
	{
		AptSaveLoad *page = g.pushPage();
		CHECK(page->showedBackground());
		CHECK(g.fx.wm->backgroundMode() == 1);
		g.fx.wm->fscommand("AptSaveLoad::Cancel", "");
		g.fx.tick(3);
		CHECK_FALSE(g.pageUp());
		CHECK(g.fx.wm->backgroundMode() == 0);
	}
}

TEST_CASE("saveui1 retail: Escape closes SaveLoad on its release in state 0 and is used in every state; other keys are not")
{
	OPENBFME_REQUIRE_START(s);
	Game g(*s);
	g.openQuit();
	AptSaveLoad *page = g.pushPage();
	CHECK_FALSE(page->inputMessage(GWM_CHAR, KEY_ENTER, KEY_STATE_UP));
	CHECK(page->inputMessage(GWM_CHAR, KEY_ESC, KEY_STATE_DOWN));
	g.fx.tick(3);
	REQUIRE(g.pageUp()); // the press does nothing
	SUBCASE("a confirmation is up (state 19): used, the page stays")
	{
		GadgetTextEntrySetText(page->fileNameEntry(), u"Saved Game 1");
		g.info.saves.push_back(SaveLoadInfo::Save{});
		g.info.saves.back().path = "/s/Saved Game 1.obfmesave";
		g.info.saves.back().fileNameUtf8 = "Saved Game 1";
		g.info.saves.back().gameKind = 2;
		g.info.saves.back().loadable = true;
		++g.info.generation;
		g.fx.tick(3);
		GadgetTextEntrySetText(page->fileNameEntry(), u"Saved Game 1");
		g.fx.wm->fscommand("AptSaveLoad::Save", "");
		g.fx.tick(2);
		REQUIRE(page->state() == 19);
		CHECK(page->inputMessage(GWM_CHAR, KEY_ESC, KEY_STATE_UP));
		g.fx.tick(3);
		CHECK(g.pageUp());
	}
	SUBCASE("state 0: the release closes it, the quit menu and its background stay")
	{
		CHECK(page->inputMessage(GWM_CHAR, KEY_ESC, KEY_STATE_UP));
		g.fx.tick(3);
		CHECK_FALSE(g.pageUp());
		CHECK(g.quit->level() >= 0); // tolerance: still loaded (its slot is the window manager's choice)
		CHECK(g.fx.wm->backgroundMode() == 2);
		CHECK(g.fx.services.countRequests(ShellAction::QuitMenuReturn) == 0);
	}
}

TEST_CASE("saveui1 retail: the saved box's Ok closes the page (state 10) and asks the quit menu to close; a failed save's Ok stays on the page")
{
	OPENBFME_REQUIRE_START(s);
	Game g(*s);
	g.openQuit();
	AptSaveLoad *page = g.pushPage();
	g.fx.wm->fscommand("AptSaveLoad::Save", "");
	g.fx.tick(2);
	REQUIRE(g.fx.services.countRequests(ShellAction::SaveGameFile) == 1);
	bool ok = true;
	SUBCASE("saved")
	{
		ok = true;
	}
	SUBCASE("the host's failure")
	{
		ok = false;
	}
	CAPTURE(ok);
	page->saveFinished(ok, ok ? "" : "writing the save failed");
	g.fx.tick(10);
	CHECK(page->boxText() == (ok ? std::u16string(u"Game Saved") : std::u16string(u"writing the save failed")));
	// the box's Ok (GuiFX.apt's MessageBox at the window manager's level 11)
	g.fx.wm->fscommand("_level11.MessageBox_OnButtonOk", "");
	g.fx.tick(3);
	if (ok)
	{
		CHECK_FALSE(g.pageUp());
		CHECK(g.fx.services.countRequests(ShellAction::QuitMenuReturn) == 1);
	}
	else
	{
		REQUIRE(g.pageUp());
		CHECK(page->state() == 0);
		CHECK(g.fx.services.countRequests(ShellAction::QuitMenuReturn) == 0);
	}
}

// Retail's WindowLayout shutdown unloads the screen's movie and its init loads it again, the C++ screen staying (RW 0x812A37 -> AptPlayer::HideLevel RW
// 0x622463; RW 0x812913 -> AptPlayer::ShowLevel RW 0x6221E0, tier A). The quit menu is the screen the others open over: it must come back with every
// control, the disabled ones disabled, and the shell must keep no pointer to it that outlives it.
TEST_CASE("saveui1 retail: a screen over the quit menu unloads its movie and the pop loads it again (the controls restored, the overlay never dangling)")
{
	OPENBFME_REQUIRE_START(s);
	Game g(*s);
	g.context.replay = true; // a replay disables Save and Load (RW 0x921E3C / 0x921F83): the restored menu must keep them disabled
	g.openQuit();
	g.fx.tick(200); // the movie's intro
	const int level = g.quit->level();
	auto pressable = [&]() { return pressableButtons(g.fx, level); };
	const std::vector<std::string> before = pressable();
	REQUIRE_FALSE(before.empty());
	const int initialised = g.quit->commandCalls("AptQuitMenu::OnInitialized");
	REQUIRE(initialised >= 1); // tolerance: the movie's InitialSetup ran at least once (each load runs it)
	REQUIRE(g.fx.shell->overlay() == g.quit.get());
	AptSaveLoad *page = g.pushPage();
	REQUIRE(page);
	CHECK(g.quit->isHidden());
	CHECK(g.quit->movieUnloaded());
	CHECK_FALSE(g.fx.wm->isAptWindowLoaded(level));            // the movie left its level
	CHECK(g.fx.wm->apt().level(level) == nullptr);
	CHECK(g.fx.wm->aptWindow(level).index == level);          // the slot and the record stay (UnloadLevel, not RemoveLevel)
	CHECK(g.fx.shell->overlay() == g.quit.get());
	CHECK(g.fx.shell->inputTop() == page);
	g.fx.wm->fscommand("AptSaveLoad::Cancel", "");
	g.fx.tick(3);
	REQUIRE_FALSE(g.pageUp());
	CHECK(g.fx.shell->inputTop() == g.quit.get());
	CHECK_FALSE(g.quit->isHidden());
	for (int i = 0; i < 30 && !g.fx.wm->isAptWindowLoaded(level); ++i)
	{
		g.fx.tick(1);
	}
	CHECK(g.fx.wm->isAptWindowLoaded(level));                // ShowLevel: the movie is loaded again, into the same level
	CHECK(g.quit->level() == level);
	CHECK_FALSE(g.quit->movieUnloaded());
	g.fx.tick(200);
	CHECK(g.quit->commandCalls("AptQuitMenu::OnInitialized") == initialised + 1); // the movie started over: its setup ran again
	CHECK(pressable() == before);                             // every control back, the disabled ones (Save, Load) still disabled
	CHECK(g.fx.wm->focusedLevel() == level);
	// the menu goes: the shell keeps no pointer to it
	g.quit.reset();
	CHECK(g.fx.shell->overlay() == nullptr);
	CHECK(g.fx.shell->inputTop() == nullptr);
}
