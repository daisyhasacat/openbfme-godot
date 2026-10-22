// OpenBFME. GPL-3.0.
// DONOR: Open-BFME-2 FormationAssistantParseFormation (4251C4),
// FormationTemplate (425842), FormationSelectionCriteria (424185),
// Rva004233F5Lookup, FormationBuild (424E77), FormationBuildRow (42481E),
// FormationAssistant (422377).
#include "GameClient/FormationAssistant.h"
#include "Common/ArchiveFileSystem.h"
#include "Common/AsciiString.h"
#include "Common/INI.h"
#include "Common/INIException.h"
#include "GameLogic/Combat/CombatQueries.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/SimMath.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Object/ObjectGeometry.h"
#include "GameLogic/ObjectFilterMatch.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
namespace
{
using F = FormationAssistant;
#define FIELD(T, N, P, M) {N, P, nullptr, (int)offsetof(T, M)}
void parseDecal(INI *ini, void *instance, void *, const void *user)
{
	auto &preview = *static_cast<F::Preview *>(instance);
	auto &decal = user ? preview.item : preview.large;
	const FieldParse fields[] = {FIELD(F::Decal, "Texture", INI::parseAsciiString, texture),
								 FIELD(F::Decal, "Width", INI::parseReal, width),
								 FIELD(F::Decal, "Height", INI::parseReal, height),
								 {nullptr, nullptr, nullptr, 0}};
	ini->initFromINI(&decal, fields);
}
void parseUnit(INI *ini, void *instance, void *, const void *)
{
	auto &fa = *static_cast<F *>(instance);
	F::Unit unit;
	unit.name = AsciiStringUtil::lowered(ini->getNextAsciiString());
	const FieldParse fields[] = {FIELD(F::Unit, "PreviewObject", INI::parseAsciiString, preview),
								 FIELD(F::Unit, "ObjectFilter", ParseObjectFilter, filter),
								 {nullptr, nullptr, nullptr, 0}};
	ini->initFromINI(&unit, fields);
	fa.units.push_back(std::move(unit));
}
void parseRow(INI *ini, void *instance, void *, const void *)
{
	auto &rows = static_cast<F::Template *>(instance)->rows;
	F::Row row;
	for (std::string token = ini->getNextAsciiString(); !token.empty(); token = ini->getNextAsciiString())
	{
		std::vector<std::string> cell;
		size_t begin = 0;
		do
		{
			size_t end = token.find('|', begin);
			cell.push_back(AsciiStringUtil::lowered(token.substr(begin, end == std::string::npos ? end : end - begin)));
			if (end == std::string::npos)
				break;
			begin = end + 1;
		} while (true);
		row.push_back(std::move(cell));
	}
	if (!rows.empty() && rows.front().size() != row.size())
		throw INIException(3, "Formation row widths differ");
	rows.push_back(std::move(row));
}
void parseRows(INI *ini, void *instance, void *, const void *)
{
	const FieldParse fields[] = {{"Row", parseRow, nullptr, 0}, {nullptr, nullptr, nullptr, 0}};
	ini->initFromINI(instance, fields);
}
void parseTemplate(INI *ini, void *instance, void *, const void *)
{
	auto &fa = *static_cast<F *>(instance);
	F::Template t;
	t.name = AsciiStringUtil::lowered(ini->getNextAsciiString());
	const FieldParse fields[] = {{"Rows", parseRows, nullptr, 0}, {nullptr, nullptr, nullptr, 0}};
	ini->initFromINI(&t, fields);
	for (const auto &row : t.rows) for (const auto &cell : row) for (const auto &name : cell)
		if (std::none_of(fa.units.begin(),fa.units.end(),[&](const F::Unit &unit){return unit.name==name;}))
			throw INIException(3,"Unknown formation unit definition '%s'",name.c_str());
	fa.templates.push_back(std::move(t));
}
void parseSelection(INI *ini, void *instance, void *, const void *)
{
	auto &fa = *static_cast<F *>(instance);
	const std::string name = AsciiStringUtil::lowered(ini->getNextAsciiString());
	F::Selection s;
	s.distance = std::numeric_limits<float>::max();
	for (size_t i = 0; i < fa.templates.size(); ++i)
		if (fa.templates[i].name == name)
			s.type = (int)i;
	if (s.type < 0)
		throw INIException(3, "Unknown formation selection template '%s'", name.c_str());
	const FieldParse fields[] = {FIELD(F::Selection, "MaxDragLength", INI::parseReal, distance),
								 FIELD(F::Selection, "MaxUnitsSelected", INI::parseInt, count),
								 {nullptr, nullptr, nullptr, 0}};
	ini->initFromINI(&s, fields);
	fa.selections.push_back(s);
}
const FieldParse fields[] = {FIELD(F, "DefaultPreviewObject", INI::parseAsciiString, defaultPreview),
							 FIELD(F, "RowPadding", INI::parseReal, rowPadding),
							 FIELD(F, "ColumnPadding", INI::parseReal, columnPadding),
							 FIELD(F, "FacingArrowHeadTemplate", INI::parseAsciiString, head),
							 FIELD(F, "FacingArrowBodyTemplate", INI::parseAsciiString, body),
							 FIELD(F, "FacingArrowBaseTemplate", INI::parseAsciiString, base),
							 FIELD(F, "ActivationDragDistance", INI::parseReal, activationDistance),
							 FIELD(F, "ActivationTime", INI::parseReal, activationTime),
							 FIELD(F, "ValidObjectFilter", ParseObjectFilter, valid),
							 {"UnitDefinition", parseUnit, nullptr, 0},
							 {"FormationTemplate", parseTemplate, nullptr, 0},
							 {"FormationSelection", parseSelection, nullptr, 0},
							 {nullptr, nullptr, nullptr, 0}};
#undef FIELD
void registerBlocks(INIEnvironment &env, F &fa)
{
	env.blocks.registerBlock("Object",
							 [&fa](INI *ini)
							 {
								 const std::string name = AsciiStringUtil::lowered(ini->getNextAsciiString());
								 F::Preview preview;
								 const FieldParse fields[] = {{"FormationPreviewDecal", parseDecal, nullptr, 0},
															  {"FormationPreviewItemDecal", parseDecal, (void *)1, 0},
															  {nullptr, nullptr, nullptr, 0}};
								 ini->initFromINI(&preview, fields);
								 fa.previews[name] = preview;
							 });
	env.blocks.registerBlock("FormationAssistant", [&fa](INI *ini) { ini->initFromINI(&fa, fields); });
}
std::vector<size_t> centerOut(size_t count)
{
	std::vector<size_t> out;
	if (!count)
		return out;
	int index = (int)(count - 1) / 2, step = 1, sign = 1;
	for (size_t i = 0; i < count; ++i)
	{
		out.push_back((size_t)index);
		index += step * sign;
		++step;
		sign = -sign;
	}
	return out;
}
} // namespace
bool FormationAssistant::load(ArchiveFileSystem &fs, std::string *error)
{
	*this = FormationAssistant();
	INIEnvironment env;
	env.fileSystem = &fs;
	registerBlocks(env, *this);
	try
	{
		INI ini(env);
		ini.load("data/ini/formationassistant.ini", INI_LOAD_OVERWRITE);
		if (templates.empty()) { if(error) *error="FormationAssistant has no formation templates"; return false; }
		return true;
	}
	catch (const std::exception &e)
	{
		if (error)
			*error = e.what();
		return false;
	}
}
bool FormationAssistant::scan(const std::string &text, std::string *error)
{
	*this = FormationAssistant();
	INIEnvironment env;
	registerBlocks(env, *this);
	try
	{
		INI ini(env);
		ini.loadMemory("formationassistant.ini", std::vector<std::uint8_t>(text.begin(), text.end()), INI_LOAD_OVERWRITE);
		if (templates.empty()) { if(error) *error="FormationAssistant has no formation templates"; return false; }
		return true;
	}
	catch (const std::exception &e)
	{
		if (error)
			*error = e.what();
		return false;
	}
}
int FormationAssistant::choose(float distance, size_t count) const
{
	if (selections.empty())
		return -1;
	for (const auto &s : selections)
		if (activationDistance > distance ? count <= (size_t)s.count : distance <= s.distance)
			return s.type;
	return selections.back().type;
}
const FormationAssistant::Decal *FormationAssistant::decal(const std::string &name, bool item) const
{
	auto it = previews.find(AsciiStringUtil::lowered(name));
	if (it == previews.end())
		return nullptr;
	return item && !it->second.item.texture.empty() ? &it->second.item : &it->second.large;
}
std::vector<FormationAssistant::Slot> FormationAssistant::layout(GameLogic &logic, const std::vector<unsigned> &ids, int type, const Coord3D &origin) const
{
	std::vector<Slot> result;
	if (type < 0 || (size_t)type >= templates.size())
		return result;
	std::vector<Object *> objects;
	for (auto id : ids)
		if (auto *o = logic.findObjectByID(id); o && !o->isDestroyed() && !o->isEffectivelyDead() && !o->isKindOfName("HORDE_MEMBER"))
		{
			const auto *ai = o->getAIUpdateInterface();
			if (ai && !ai->isImmobile() && ObjectFilterMatch::allows(logic, valid, *o, nullptr)) objects.push_back(o);
		}
	std::stable_sort(objects.begin(), objects.end(),
					 [&](Object *a, Object *b)
					 {
						 auto distance = [&](Object *o)
						 {
							 auto p = o->getPosition();
							 const float dx=SimMath::subf32(p->x,origin.x),dy=SimMath::subf32(p->y,origin.y);
							 return SimMath::addf32(SimMath::mulf32(dx,dx),SimMath::mulf32(dy,dy));
						 };
						 return distance(a) < distance(b);
					 });
	int y = 0;
	for (const auto &cells : templates[type].rows)
		while (!objects.empty())
		{
			struct Cell
			{
				Object *object = nullptr;
				const Unit *unit = nullptr;
				int width = 0, height = 0, x = 0;
			};
			std::vector<Cell> row(cells.size());
			bool placed = false;
			for (auto index : centerOut(cells.size()))
				for (const auto &candidate : cells[index])
				{
					auto unit = std::find_if(units.begin(), units.end(), [&](const Unit &u) { return u.name == candidate; });
					if (unit == units.end())
						continue;
					auto obj =
						std::find_if(objects.begin(), objects.end(), [&](Object *o) { return ObjectFilterMatch::allows(logic, unit->filter, *o, nullptr); });
					if (obj == objects.end())
						continue;
					auto &slot = row[index];
					slot.object = *obj;
					slot.unit = &*unit;
					slot.width = slot.height = SimMath::cvttss2si(SimMath::addf32(1,SimMath::mulf32(2,CombatQueries::boundingCircleRadius(**obj))));
					if ((*obj)->isKindOfName("HORDE"))
					{
						const auto &shape = CombatQueries::geometry(**obj);
						slot.width = SimMath::cvttss2si(SimMath::addf32(SimMath::mulf32(shape.minorRadius,2),4));
						slot.height = SimMath::cvttss2si(SimMath::addf32(SimMath::mulf32(shape.majorRadius,2),4));
					}
					objects.erase(obj);
					placed = true;
					break;
				}
			if (!placed)
				break;
			int width = 0, height = 0;
			for (auto &slot : row)
				if (slot.object)
				{
					slot.x = width + slot.width / 2;
					width = SimMath::cvttss2si(SimMath::addf32((float)(slot.width + width),columnPadding));
					height = std::max(height, slot.height);
				}
			const int center = SimMath::cvttss2si(SimMath::mulf32(SimMath::subf32((float)width,columnPadding),.5f));
			for (auto index : centerOut(row.size()))
			{
				const auto &slot = row[index];
				if (slot.object)
					result.push_back({slot.object->getID(), slot.unit->preview.empty() ? defaultPreview : slot.unit->preview,
									  Coord3D{(float)(slot.x - center), (float)y, 0}});
			}
			y = SimMath::cvttss2si(SimMath::addf32((float)(height+y),rowPadding));
		}
	return result;
}
