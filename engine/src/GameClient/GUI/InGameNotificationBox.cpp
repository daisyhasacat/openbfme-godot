// OpenBFME. GPL-3.0. See InGameNotificationBox.h. Donor: local Open-BFME-2
// GUI/InGame/InGameNotificationBoxMovieClip.cpp and
// InGameNotificationBoxUpdateNotice.cpp.
#include "GameClient/GUI/InGameNotificationBox.h"
#include "Common/ArchiveFileSystem.h"
#include "Common/Audio/AudioEntryPoints.h"
#include "Common/Audio/GameAudio.h"
#include "GameClient/GUI/AptGadgetLayer.h"
#include "GameClient/GUI/WindowManager.h"
#include "GameClient/GameText.h"
#include "Libraries/Source/Apt/AptValue.h"
#include <cmath>
#include <codecvt>
#include <cstdio>
#include <iomanip>
#include <locale>
#include <sstream>
#include <stdexcept>
namespace
{
std::string trim(std::string s)
{
	auto comment = s.find(';');
	if (comment != std::string::npos)
		s.resize(comment);
	auto b = s.find_first_not_of(" \t\r\n");
	if (b == std::string::npos)
		return {};
	auto e = s.find_last_not_of(" \t\r\n");
	return s.substr(b, e - b + 1);
}
} // namespace
InGameNotificationBox::InGameNotificationBox(WindowManager &wm, ArchiveFileSystem &fs, const GameTextTable &text) : m_windows(wm), m_text(text)
{
	std::vector<uint8_t> bytes;
	std::string error;
	if (!fs.readFile("data/ini/ingamenotificationbox.ini", bytes, &error))
		throw std::runtime_error(error);
	std::istringstream input(std::string(bytes.begin(), bytes.end()));
	std::string line, name;
	Type defaults;
	Type *current = &defaults;
	bool block = false;
	unsigned fields = 0;
	while (std::getline(input, line))
	{
		line = trim(line);
		if (line.empty())
			continue;
		if (line == "InGameNotificationBox")
		{
			block = true;
			continue;
		}
		if (line == "End")
		{
			if (current != &defaults)
				current = &defaults;
			else
				block = false;
			continue;
		}
		if (!block)
			throw std::runtime_error("Unexpected notification INI content: " + line);
		if (line.rfind("NotificationType ", 0) == 0)
		{
			name = trim(line.substr(17));
			m_types[name] = defaults;
			current = &m_types[name];
			continue;
		}
		auto eq = line.find('=');
		if (eq == std::string::npos)
			throw std::runtime_error("Invalid notification INI field: " + line);
		auto key = trim(line.substr(0, eq));
		auto value = trim(line.substr(eq + 1));
		if (key == "DefaultMessageFont" || key == "MessageFont")
		{
			std::istringstream v(value);
			std::string bold;
			v >> std::quoted(current->font.name) >> current->font.pointSize >> bold;
			if (!v || (bold != "Yes" && bold != "No"))
				throw std::runtime_error("Invalid notification font: " + value);
			current->font.bold = bold == "Yes";
			fields |= 1;
		}
		else if (key == "DefaultMessageColor" || key == "MessageColor")
		{
			int r, g, b;
			if (std::sscanf(value.c_str(), "R:%d G:%d B:%d", &r, &g, &b) != 3)
				throw std::runtime_error("Invalid notification color: " + value);
			current->color = 0xff000000u | (r << 16) | (g << 8) | b;
			fields |= 2;
		}
		else if (key == "DefaultOpenAudio" || key == "OpenAudio")
		{
			current->sound = value;
			fields |= 4;
		}
		else if (key == "Title")
			current->title = value;
		else if (key == "Icon")
			current->icon = value;
		else
			throw std::runtime_error("Unsupported notification field: " + key);
	}
	if (fields != 7 || m_types.empty() || block)
		throw std::runtime_error("Incomplete InGameNotificationBox.ini");
	m_level = wm.loadAptWindow("Apt\\", "InGameNotificationBox.apt", true, 0, -1);
	if (m_level < 0)
		throw std::runtime_error("Cannot load InGameNotificationBox.apt");
	m_prefix = "_level" + std::to_string(m_level);
	wm.registerCommand(m_prefix + "_OnInitialized", [this](const std::string &) { m_ready = true; });
	wm.registerCommand(m_prefix + "_OnOpen", [this](const std::string &) { m_openAnimationDone = true; });
	wm.registerCommand(m_prefix + "_OnClosed", [this](const std::string &) {
		m_visible = false;
		m_message.clear();
	});
	wm.registerCommand(m_prefix + "_OnCloseButtonClicked", [this](const std::string &) { clear(); });
	wm.registerProvider(m_prefix + "_MessageWidth", [this](const std::string &, std::string &value, bool setting) {
		if (setting)
			m_width = std::stof(value);
		return setting;
	});
	wm.registerComponent(m_prefix + "_Message", [](AptComponentRequest &) -> std::shared_ptr<GameWindow> { return nullptr; });
	wm.setRenderCallback(m_prefix + "_Message", [this](float x, float y, float w, float h, GadgetDrawList &out) {
		if (!m_visible || m_message.empty())
			return;
		auto *layer = m_windows.gadgetLayer();
		if (!layer)
			return;
		int height = layer->gadgets().fontMetrics().wrappedHeight(m_active.font, m_message, (int)w);
		GadgetDrawCommand c;
		c.kind = GadgetDrawCommand::Kind::Text;
		c.x0 = (int)x;
		c.y0 = (int)std::floor(y + (h - height + 1) * 0.5f);
		c.x1 = (int)(x + w);
		c.y1 = c.y0 + height;
		c.text = m_message;
		c.font = m_active.font;
		c.color = m_active.color;
		c.wrapWidth = (int)w;
		c.wrapCentered = true;
		out.commands.push_back(c);
	});
	wm.showAptWindow(m_level);
}
InGameNotificationBox::~InGameNotificationBox()
{
	for (const char *name : {"_OnInitialized", "_OnOpen", "_OnClosed", "_OnCloseButtonClicked"})
		m_windows.unregisterCommand(m_prefix + name);
	m_windows.unregisterProvider(m_prefix + "_MessageWidth");
	m_windows.unregisterComponent(m_prefix + "_Message");
	m_windows.clearRenderCallback(m_prefix + "_Message");
	m_windows.unloadAptWindow(m_level);
}
void InGameNotificationBox::invoke(const std::string &name, const std::vector<std::string> &args)
{
	std::string error;
	if (!m_windows.invokeAS(m_level, name, args, nullptr, &error))
		throw std::runtime_error("Notification " + name + ": " + error);
}
void InGameNotificationBox::open(const std::string &type, const std::string &label, double seconds)
{
	auto it = m_types.find(type);
	if (it == m_types.end())
		throw std::runtime_error("Unknown notification type: " + type);
	std::string text;
	if (!m_text.lookup(label, text))
		throw std::runtime_error("Missing notification text: " + label);
	// Game text is UTF-8; preserve non-ASCII characters in the native UTF-16 string.
	m_message = std::wstring_convert<std::codecvt_utf8_utf16<char16_t>, char16_t>().from_bytes(text);
	m_active = it->second;
	m_remaining = seconds * 1000.0;
	m_pending = true;
}
void InGameNotificationBox::update(double ms)
{
	if (m_pending && m_ready && m_width > 0)
	{
		std::string title;
		if (!m_active.title.empty() && !m_text.lookup(m_active.title, title))
			throw std::runtime_error("Missing notification title: " + m_active.title);
		m_windows.setAptText("APT:" + m_prefix + "_Title", title);
		std::string error;
		if (!m_windows.invokeASAtValues(m_level, "", "SetIconVisibility", { AptValue::boolean(!m_active.icon.empty()) }, &error))
			throw std::runtime_error("Notification icon visibility: " + error);
		if (!m_active.icon.empty())
			m_windows.setAptImage(m_prefix + "_Icon", m_active.icon);
		auto *layer = m_windows.gadgetLayer();
		if (!layer)
			throw std::runtime_error("Notification needs native font metrics");
		int height = layer->gadgets().fontMetrics().wrappedHeight(m_active.font, m_message, (int)m_width);
		if (!m_windows.invokeASAtValues(m_level, "", "Open", { AptValue::number(height), AptValue::boolean(false), AptValue::string("main") }, &error))
			throw std::runtime_error("Notification Open: " + error);
		m_sound = m_active.sound;
		if (!m_sound.empty() && m_sound != "None")
		{
			auto *audio = AudioApi::current();
			if (!audio)
				throw std::runtime_error("Notification has no audio manager");
			audio->addAudioEvent(AudioEventRTS(m_sound, 2));
		}
		m_openAnimationDone = false;
		++m_opened;
		m_pending = false;
		m_visible = true;
	}
	else if (m_visible && m_openAnimationDone && m_remaining > 0)
	{
		m_remaining -= ms;
		if (m_remaining <= 0)
			clear();
	}
}
void InGameNotificationBox::clear()
{
	m_pending = false;
	m_remaining = 0;
	if (m_visible)
	{
		invoke("Close");
		m_visible = false;
	}
}
