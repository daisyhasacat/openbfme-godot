// OpenBFME. GPL-3.0.
// See GameClient/GUI/AptScreens/AptCampaignMenu.h (lane CAMPSAVE-1).

#include "GameClient/GUI/AptScreens/AptCampaignMenu.h"

#include "GameClient/GUI/GameTextSource.h"
#include "GameClient/GUI/LoadScreenInfo.h"
#include "GameClient/GUI/ShellEnvironment.h"
#include "GameClient/GUI/ShellServices.h"

namespace
{
struct CommandRow
{
	const char *name;
	ShellAction action;
	const char *request;
};

// RW 0x927B9F's registration order
const CommandRow kCommands[] = {
	{ "AptCampaignMenu::OnBttnMainMenu", ShellAction::CampaignMenuMainMenu, "CampaignMenuMainMenu" },
	{ "AptCampaignMenu::OnBttnSaveGame", ShellAction::CampaignMenuSave, "CampaignMenuSave" },
	{ "AptCampaignMenu::OnBttnLoadGame", ShellAction::CampaignMenuLoad, "CampaignMenuLoad" },
	{ "AptCampaignMenu::OnBttnLastMission", ShellAction::CampaignMenuLastMission, "CampaignMenuLastMission" },
	{ "AptCampaignMenu::OnBttnNextMission", ShellAction::CampaignMenuNextMission, "CampaignMenuNextMission" },
};
} // namespace

const std::vector<std::string> &AptCampaignMenu::retailNames()
{
	static const std::vector<std::string> names = {
		"AptCampaignMenu::OnBttnMainMenu", "AptCampaignMenu::OnBttnSaveGame", "AptCampaignMenu::OnBttnLoadGame", "AptCampaignMenu::OnBttnLastMission",
		"AptCampaignMenu::OnBttnNextMission", "CampaignMenu:Victorious",
	};
	return names;
}

AptCampaignMenu::AptCampaignMenu(WindowManager &windows, Shell &shell, ShellEnvironment &environment)
	: AptScreen(windows, shell, "CampaignMenu.apt", "AptCampaignMenu"), m_env(environment)
{
	for (const CommandRow &row : kCommands)
	{
		const CommandRow r = row;
		registerCommand(r.name, [this, r](const std::string &) {
			// RW 0x927A07 (Main Menu / Last / Next Mission) -> RW 0x62215B: the engine's next update pops the menu (Shell::pop RW 0x75DB34), TheShell + 0x54
			// set for the missions. OpenBFME: the host pops it on those requests, as it starts the mission or shows the main menu
			post((int)r.action, r.request);
		});
	}
	// RW 0x927A45: "0", then "1" for index 0 with + 0x284 (a setting changes nothing)
	registerProvider("CampaignMenu:Victorious", [this](const std::string &, std::string &value, bool setting) {
		if (!setting)
		{
			value = victorious();
		}
		return true;
	});
	windows.markNumericProvider("CampaignMenu:Victorious"); // the movie tests it with Not: "0" must be false (as AptTimeLine's flags, S-1063)
	if (m_env.campaignMenu)
	{
		m_content = *m_env.campaignMenu;
	}
	else
	{
		windows.note("campaign-menu-unwired", "CampaignMenu.apt without the campaign's content (the host gave no CampaignMenuContext) [S-1360]");
	}
	// RW 0x927E08 .. 0x927E57: "CampaignName:" + the mission's name, through TheGameText (a label the table lacks shows retail's MISSING text)
	m_missionText = loadScreenU16ToUtf8(fetchOrMissing(m_env.gameText, "CampaignName:" + m_content.missionName));
	windows.setAptText("APT:CampaignMissionName", m_missionText);
	// RW 0x927E74 .. 0x927E96: the image record the movie's RenderImage clip names
	m_image = m_content.loadScreenImage;
	windows.setAptImage("AptCampaign::LoadImage", m_image);
}

AptCampaignMenu::~AptCampaignMenu() = default;

std::string AptCampaignMenu::victorious() const
{
	return m_content.victorious ? "1" : "0";
}

void AptCampaignMenu::post(int action, const std::string &name)
{
	m_requests.push_back(name);
	if (m_env.services)
	{
		m_env.services->request(ShellRequest{ (ShellAction)action, std::string() });
	}
	else
	{
		windows().note("command-unwired", name + ": the host gave no shell services [S-1064]");
	}
}
