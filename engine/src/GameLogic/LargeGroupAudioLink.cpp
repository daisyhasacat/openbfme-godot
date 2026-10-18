// OpenBFME. GPL-3.0. See GameLogic/LargeGroupAudioLink.h (lane AUDIO-4).

#include "GameLogic/LargeGroupAudioLink.h"

#include "Common/StateHash.h"
#include "Common/Xfer.h"

void LargeGroupAudioLink::notify(const LargeGroupAudioEvent &event)
{
	// RW 0x60D5CB / 0x60D5FF / 0x60D633: `cmp byte [this + 0x38], 0; jne ret`, then `TheGameLogic + 0x40 == [this + 0x3C]: ret`
	if (m_disabled || (std::int32_t)event.frame == m_gateFrame)
	{
		++m_dropped;
		return;
	}
	++m_delivered;
	if (m_sink)
	{
		m_sink->onLargeGroupAudioEvent(event);
	}
}

void LargeGroupAudioLink::crc(StateHasher &h) const
{
	h.addBool(m_disabled);  // + 0x38
	h.addI32(m_gateFrame);  // + 0x3C
}

// lane SAVE-4d. Retail's LargeGroupAudio::xfer (RW 0x60D899, window (1, 5), tier A) starts with these two fields: version, the byte + 0x38, and only when
// it is clear the int + 0x3C (from version 2), then the bytes + 0x3A / + 0x39 and the audio maps (TheLargeGroupAudio's sounds, client state here). The
// port holds the gate only, so the block is OpenBFME's own (S-4250): version 1, + 0x38, + 0x3C (always, the modules compare it while disabled too).
void LargeGroupAudioLink::xfer(Xfer &x)
{
	XferVersion v{ 1, 1 };
	x.xferVersion(v);
	bool disabled = x.isLoading() ? false : m_disabled;
	std::int32_t gateFrame = x.isLoading() ? 0 : m_gateFrame;
	x.xferBool(&disabled);
	x.xferInt(&gateFrame);
	if (!x.isLoading())
	{
		return;
	}
	// the two states the gate can be in: disabled with the constructor's -1 (RW 0x60DA8B / 0x60DA95), or enabled at a game's frame (RW 0x60D4D3)
	if (disabled ? gateFrame != -1 : gateFrame < 0)
	{
		throw XferException("large group audio: a gate " + std::string(disabled ? "disabled" : "enabled") + " at frame " + std::to_string(gateFrame));
	}
	m_disabled = disabled;
	m_gateFrame = gateFrame;
}
