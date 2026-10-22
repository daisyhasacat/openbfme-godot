// OpenBFME. GPL-3.0. BFME2 InGameNotificationBoxMovieClip (0x4E6C34,
// UpdateNotice 0x4E701E): native APT frame, configured sound and custom text.
#pragma once
#include "GameClient/GUI/GameWindow.h"
#include <map>
#include <string>
#include <vector>

class ArchiveFileSystem;
class WindowManager;
class GameTextTable;
class InGameNotificationBox
{
  public:
	InGameNotificationBox(WindowManager &windows, ArchiveFileSystem &fs, const GameTextTable &text);
	~InGameNotificationBox();
	void open(const std::string &type, const std::string &label, double seconds);
	void update(double ms);
	void clear();
	int level() const
	{
		return m_level;
	}
	const std::string &sound() const
	{
		return m_sound;
	}
	unsigned opened() const
	{
		return m_opened;
	}
	bool visible() const
	{
		return m_visible;
	}
	bool pending() const
	{
		return m_pending;
	}
	float width() const
	{
		return m_width;
	}

  private:
	struct Type
	{
		std::string title, icon, sound;
		GameFont font;
		unsigned color = 0;
	};
	WindowManager &m_windows;
	const GameTextTable &m_text;
	std::map<std::string, Type> m_types;
	Type m_active;
	UnicodeString m_message;
	std::string m_prefix, m_sound;
	int m_level = -1;
	bool m_ready = false, m_pending = false, m_visible = false;
	bool m_openAnimationDone = false;
	float m_width = 0;
	double m_remaining = 0;
	unsigned m_opened = 0;
	void invoke(const std::string &name, const std::vector<std::string> &args = {});
};
