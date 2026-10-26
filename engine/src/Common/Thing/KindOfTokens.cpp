// OpenBFME. GPL-3.0.
// See Common/Thing/KindOfTokens.h.

#include "Common/Thing/KindOfTokens.h"

#include "Common/AsciiString.h"
#include "Common/ModEngineFeatures.h"

#include <algorithm>
#include <string>
#include <vector>
#include <unordered_map>

// RW 0xDA0E68 (generated: GameLogic/BitFlagNames.cpp)
extern const char *const TheKindOfNames[];

namespace
{
const char *const *extendedNames()
{
	static const std::vector<const char *> table = [] {
		std::vector<const char *> t;
		for (int i = 0; TheKindOfNames[i]; ++i)
		{
			t.push_back(TheKindOfNames[i]);
		}
		t.push_back("HEROBAR");       // bit 222 (Edain Unchained .hbar)
		t.push_back("HEROBAR_GROUP"); // bit 223
		t.push_back(nullptr);
		return t;
	}();
	return table.data();
}

char lowerAscii(char c)
{
	return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c; // AsciiStringUtil::compareNoCase's folding
}

// PERF: runtime script and AI filters resolve these names millions of times in
// accelerated matches. Hash ASCII-folded views without allocating; the immutable
// retail name table owns the keys. Emplace in table order keeps the first index
// for duplicate folded names, exactly as the original scan did.
struct FoldedHash
{
 size_t operator()(std::string_view name) const
 {
  size_t hash = 2166136261u;
  for (char c : name) hash = (hash ^ (unsigned char)lowerAscii(c)) * 16777619u;
  return hash;
 }
};
struct FoldedEqual
{
 bool operator()(std::string_view a, std::string_view b) const
 {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i)
   if (lowerAscii(a[i]) != lowerAscii(b[i])) return false;
  return true;
 }
};
} // namespace

const char *const *KindOfTokens::names()
{
	return ModEngineFeatures::has(ModEngineFeature::EdainUnchainedHerobarKindOf) ? extendedNames() : TheKindOfNames;
}

int KindOfTokens::indexOf(std::string_view name)
{
 static const auto names = [] {
  std::unordered_map<std::string_view, int, FoldedHash, FoldedEqual> table;
  table.reserve(256);
  for (int i = 0; TheKindOfNames[i]; ++i) table.emplace(TheKindOfNames[i], i);
  return table;
 }();
 if (ModEngineFeatures::has(ModEngineFeature::EdainUnchainedHerobarKindOf)) {
  if (FoldedEqual{}(name, "HEROBAR")) return 222;
  if (FoldedEqual{}(name, "HEROBAR_GROUP")) return 223;
 }
 const auto found = names.find(name);
 return found == names.end() ? -1 : found->second;
}

// Mirror of the bit-string loop of RW 0x65621C / 0x655B0B for a token list (the shared loop is documented at GameLogic/BitFlags.h):
// a macro token (INI #define) expands to its whitespace separated words, each handled as a token, and a NONE inside a macro ends
// that macro's words only; NONE clears the set and ends the whole list; the first plain name clears the set; +X / -X edit it; mixing
// is an error. Unknown names are reported (retail's INI load would have thrown).
void KindOfTokens::parse(const RawTokens &raw, std::array<std::uint32_t, 7> &mask, std::string &error)
{
	bool sawNormal = false, sawPlusMinus = false;
	// returns false when the list (or the macro's pieces) ends
	auto handle = [&](const std::string &t) -> bool {
		if (AsciiStringUtil::compareNoCase(t, "NONE") == 0)
		{
			if (sawNormal || sawPlusMinus)
			{
				error = "NONE after other KindOf names";
			}
			mask.fill(0);
			return false;
		}
		const bool plus = !t.empty() && t[0] == '+';
		const bool minus = !t.empty() && t[0] == '-';
		const std::string name = (plus || minus) ? t.substr(1) : t;
		const int bit = indexOf(name);
		if (bit < 0)
		{
			error = "unknown KindOf name '" + name + "'";
			return true;
		}
		if (plus || minus)
		{
			if (sawNormal)
			{
				error = "KindOf: +/- after plain names";
			}
			sawPlusMinus = true;
			if (plus)
			{
				mask[(size_t)bit >> 5] |= 1u << (bit & 31);
			}
			else
			{
				mask[(size_t)bit >> 5] &= ~(1u << (bit & 31));
			}
		}
		else
		{
			if (sawPlusMinus)
			{
				error = "KindOf: plain name after +/-";
			}
			if (!sawNormal)
			{
				mask.fill(0);
			}
			sawNormal = true;
			mask[(size_t)bit >> 5] |= 1u << (bit & 31);
		}
		return true;
	};
	for (size_t ti = 0; ti < raw.tokens.size(); ++ti)
	{
		const std::string &t = raw.tokens[ti];
		if (t == "=")
		{
			continue;
		}
		const bool isMacro = ti < raw.macros.size() && raw.macros[ti].isMacro;
		if (isMacro)
		{
			const std::string &value = raw.macros[ti].value;
			size_t pos = 0;
			while (pos < value.size())
			{
				const size_t b = value.find_first_not_of(" \n\r\t", pos);
				if (b == std::string::npos)
				{
					break;
				}
				size_t e = value.find_first_of(" \n\r\t", b);
				if (e == std::string::npos)
				{
					e = value.size();
				}
				pos = e;
				if (!handle(value.substr(b, e - b)))
				{
					break; // only this macro's pieces end; the line goes on
				}
			}
			continue;
		}
		if (!handle(t))
		{
			return;
		}
	}
}

void KindOfTokens::parseTemplate(const ThingTemplate &tt, std::array<std::uint32_t, 7> &mask, std::string &error)
{
	mask.fill(0);
	if (!tt.kindOfRows().empty())
	{
		for (const RawTokens &raw : tt.kindOfRows())
		{
			parse(raw, mask, error);
		}
		return;
	}
	if (const FieldValue *k = tt.findField("KindOf"))
	{
		if (const RawTokens *raw = std::get_if<RawTokens>(k))
		{
			parse(*raw, mask, error);
		}
	}
}
