// OpenBFME. GPL-3.0. The bfme companion's adapter to live engine objects.
#include "Common/BuildAssistant.h"
#include "Common/Upgrade.h"
#include "GameLogic/Module/CastleModules.h"
#include "GameLogic/Module/ConstructionModules.h"
#include "GameLogic/Construction.h"
#include "GameLogic/ConstructionSite.h"
#include "GameLogic/BuildPlacement.h"
#include "GameLogic/Module/DozerAIUpdate.h"
#include "GameLogic/Module/ProductionUpdate.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameLogic/Module/BehaviorModule.h"
#include "GameLogic/Object/ExperienceTracker.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Object/RetailObjectWorld.h"
#include "GameLogic/ObjectTemplateInfo.h"
#include "GameLogic/SimMath.h"
#include "GameLogic/System/ShroudManager.h"
#include "GodotDevice/GodotGameWorld.h"
#include "GodotDevice/GodotInGameHud.h"
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/image.hpp>
#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
using Value = BfmeLuaValue;
using Table = Value::Table;
Value field(const ThingTemplate &tt, const char *name)
{
	const auto v = tt.findField(name);
	if (!v)
		return {};
	if (auto n = std::get_if<long long>(v))
		return double(*n);
	if (auto n = std::get_if<float>(v))
		return double(*n);
	if (auto s = std::get_if<std::string>(v))
		return *s;
	if (auto b = std::get_if<bool>(v))
		return *b;
	return {};
}
Table definition(const ThingTemplate &tt)
{
	return {{"id", tt.getName()},
			{"name", tt.getName()},
			{"displayName", field(tt, "DisplayName")},
			{"faction", field(tt, "Side")},
			{"side", field(tt, "Side")},
			{"cost", int(BuildAssistant::buildCost(tt))},
			{"refund", field(tt, "RefundValue")},
			{"buildTime", field(tt, "BuildTime")},
			{"trainTime", field(tt, "BuildTime")},
			{"visionRange", field(tt, "VisionRange")},
			{"shroudClearingRange", field(tt, "ShroudClearingRange")}};
}
// Companion API: use the same body flag and contained-object propagation as
// ScriptActionsUnits.cpp affectObjectPanelFlag("Indestructible", ...).
void setInvulnerable(Object &obj, bool enabled, int depth = 0)
{
	if (auto body = obj.getBodyModule()) body->setIndestructible(enabled);
	auto contain = obj.getContain();
	if (!contain || depth > 16 || !contain->getContainedItemsList()) return;
	for (auto member : *contain->getContainedItemsList())
		if (member) setInvulnerable(*member, enabled, depth + 1);
}
bool kind(const ObjectTemplateInfo &info, int bit)
{
	return (info.kindOf[bit / 32] & (1u << (bit % 32))) != 0;
}
int numberId(const std::string &id)
{
	size_t end = 0;
	long long n = std::stoll(id, &end);
	if (end != id.size() || n < 0 || n > std::numeric_limits<int>::max())
		throw std::runtime_error("invalid engine ID");
	return int(n);
}
double numeric(const Value &v, double lo, double hi, bool integer = true)
{
	auto n = std::get_if<double>(&v.data);
	if (!n || !std::isfinite(*n) || *n < lo || *n > hi || (integer && double(SimMath::ftol2(*n)) != *n))
		throw std::runtime_error("value out of range");
	return *n;
}
} // namespace
namespace godot
{
BfmeLuaRuntime::Snapshot GameWorld::bfme_snapshot()
{
	BfmeLuaRuntime::Snapshot out;
	out.paused = m_paused;
	if (!m_world)
		return out;
	if (!m_luaCatalog) // rebuilt when the definitions are loaded again (setup / a map's overrides reset it)
	{
		auto catalog = std::make_shared<BfmeLuaRuntime::Collections>();
		for (auto master : m_world->things().templates())
		{
			const auto tt = master->getFinalOverride();
			auto info = ObjectTemplateInfoBuilder::build(*tt);
			auto r = definition(*tt);
			(*catalog)["templates"][tt->getName()] = r;
			if (kind(info, 7))
				(*catalog)["buildingTypes"][tt->getName()] = r;
			else if (kind(info, 1))
				(*catalog)["unitTypes"][tt->getName()] = r;
		}
		m_luaCatalog = std::move(catalog);
	}
	out.catalog = m_luaCatalog;
	if (!m_game)
		return out;
	out.ready = true;
	if (auto hud = Object::cast_to<InGameHudNode>(ObjectDB::get_instance(ObjectID(m_luaHudSource))))
	{
		Coord3D point;
		if (hud->cursor_map_position(this, point))
			out.cursorPosition = Table{{"x", double(point.x)}, {"y", double(point.y)}, {"z", double(point.z)}};
		out.selection = hud->selected_ids(this);
	}
	auto &logic = m_game->logic();
	out.frame = logic.getFrame();
	auto &players = logic.players();
	if (auto p = players.getLocalPlayer())
		out.localPlayerId = p->getPlayerIndex();
	for (int i = 0; i < players.getPlayerCount(); ++i)
	{
		auto p = players.getNthPlayer(i);
		if (!p)
			continue;
		const auto &cp = p->commandPoints();
		const int limit = p->commandPointLimit();
		Table r{{"id", i},
				{"startSlot", p->getMultiplayerStartIndex()},
				{"name", p->getPlayerName()},
				{"displayName", p->getPlayerDisplayName()},
				{"username", p->getPlayerDisplayName()},
				{"money", double(p->getMoney()->countMoney())},
				{"gold", double(p->getMoney()->countMoney())},
				{"commandPoints", limit},
				{"commandPointsBase", cp.getBase()},
				{"commandPointsBonus", cp.getBonus()},
				{"commandPointsCap", cp.getCap()},
				{"commandPointsUsed", cp.getUsage()},
				{"commandPointsRemaining", cp.getAvailable(limit)},
				{"powerPoints", p->science().getSciencePurchasePoints()},
				{"powerpoint", p->science().getSciencePurchasePoints()},
				{"isAI", p->isSkirmishAI()},
				{"faction", p->getSide()}};
		out.collections["players"][std::to_string(i)] = std::move(r);
	}
	for (auto obj = logic.getFirstObject(); obj; obj = obj->getNextObject())
	{
		const auto tt = obj->getTemplate();
		if (!tt)
			continue;
		const auto &info = logic.templateInfo(tt);
		const bool building = kind(info, 7);
		if (!building && !kind(info, 1))
			continue;
		Table r = definition(*tt);
		const auto pos = obj->getPosition();
		r["id"] = double(obj->getID());
		r["objectId"] = double(obj->getID());
		r["typeId"] = tt->getName();
		r["position"] = Table{{"x", double(pos->x)}, {"y", double(pos->y)}, {"z", double(pos->z)}};
		r["x"] = double(pos->x);
		r["y"] = double(pos->y);
		r["z"] = double(pos->z);
		r["rotation"] = double(obj->getOrientation());
		if (auto p = obj->getControllingPlayer())
		{
			r["playerId"] = p->getPlayerIndex();
			r["player"] = p->getPlayerIndex();
		}
		if (auto body = obj->getBodyModule())
		{
			r["health"] = double(body->getHealth());
			r["maxHealth"] = double(body->getMaxHealth());
			r["invulnerable"] = body->isIndestructible();
		}
		if (auto xp = obj->getExperienceTracker())
			r["level"] = xp->getRank();
		if (building)
		{
			r["underConstruction"] = obj->isUnderConstruction();
			r["constructionPercent"] = double(obj->getConstructionPercent());
			r["builderId"] = double(obj->getBuilderID());
		}
		out.collections[building ? "buildingInstances" : "unitInstances"][std::to_string(obj->getID())] = std::move(r);
	}
	bfme_snapshot_ai(out);
	return out;
}
unsigned GameWorld::bfme_construct(const BfmeLuaRuntime::Mutation &m)
{
	if (!m_game || !m_world) throw std::runtime_error("no active match");
	auto &logic = m_game->logic();
	if (m.args.size() != 6 && m.args.size() != 7) throw std::runtime_error("invalid construction arguments");
	auto name = std::get_if<std::string>(&m.value.data);
	auto tt = name ? m_world->things().findTemplate(*name) : nullptr;
	if (!tt || !kind(logic.templateInfo(tt->getFinalOverride()), 7))
		throw std::runtime_error("unknown building template");
	tt = tt->getFinalOverride();
	Coord3D pos{float(numeric(m.args[0], -1e7, 1e7, false)), float(numeric(m.args[1], -1e7, 1e7, false)), 0};
	int builderId = int(numeric(m.args[2], 0, 2147483647));
	int playerId = int(numeric(m.args[3], 0, 19));
	if (numeric(m.args[4], 0.01, 100, false) != 1.0)
		throw std::runtime_error("construction speed: only the native speed (1) is supported");
	float angle = float(numeric(m.args[5], -1e6, 1e6, false));
	auto player = logic.players().getNthPlayer(playerId);
	if (!player || !player->getDefaultTeam()) throw std::runtime_error("invalid construction player");
	::Object *builder = builderId ? logic.findObjectByID(builderId) : nullptr;
	DozerAIUpdate *dozer = builder ? dynamic_cast<DozerAIUpdate *>(builder->getAIUpdateInterface()) : nullptr;
	if (builderId && (!builder || builder->isDestroyed() || builder->isEffectivelyDead() ||
		builder->getControllingPlayer() != player || !dozer))
		throw std::runtime_error("builder must be a living builder owned by the player");
	const int cost = BuildAssistant::calcCostToBuild(*tt, player, builder, -1);
	if (cost < 0 || player->getMoney()->countMoney() < (unsigned)cost)
		throw std::runtime_error("not enough money for construction");
	const int plotId = m.args.size() == 7 ? int(numeric(m.args[6], 0, 2147483647)) : 0;
	if (plotId)
	{
		auto plot = logic.findObjectByID(plotId);
		if (builderId || !plot || plot->isDestroyed() || plot->isEffectivelyDead() || plot->getControllingPlayer() != player ||
			!(plot->isKindOfName("BASE_FOUNDATION") || plot->isKindOfName("BASE_DEFENSE_FOUNDATION") || plot->isKindOfName("EXPANSION_PAD")))
			throw std::runtime_error("plot must be a living foundation owned by the player");
		auto obj = Construction::constructOnPlot(*plot, *tt, *plot->getPosition(), angle, *player, false);
		if (!obj) throw std::runtime_error("plot construction refused (occupied or unavailable)");
		return obj->getID();
	}
	if (!dozer)
		throw std::runtime_error("construction needs a builder unit (unit:construct) or a foundation plot");
	// Scripted construction may select any structure template, but placement and
	// builder pathfinding use the same gates as normal construction.
	if (BuildPlacement::isLocationLegalToBuild(logic, pos, *tt, angle,
		LLF_TERRAIN_RESTRICTIONS | LLF_NO_OBJECT_OVERLAP, builder, player) != LBC_OK)
		throw std::runtime_error("illegal construction site");
	auto obj = dozer->construct(*tt, pos, angle, *player);
	if (!obj) throw std::runtime_error("construction refused (site or builder unavailable)");
	return obj->getID();
}
std::string GameWorld::bfme_construction_status(unsigned id)
{
	if (!m_game) return "match ended";
	auto obj = m_game->logic().findObjectByID(id);
	if (!obj || obj->isDestroyed() || obj->isEffectivelyDead()) return "construction cancelled or building destroyed";
	return obj->isUnderConstruction() ? "building" : "complete";
}

std::string GameWorld::bfme_apply(const BfmeLuaRuntime::Mutation &m)
{
	if (m.collection == "templates" || m.collection == "unitTypes" || m.collection == "buildingTypes")
		return "template fields are read-only";
	if (!m_game || !m_world)
		return "no active match";
	auto &logic = m_game->logic();
	if (m.collection == "orders")
		return bfme_order(m);
	if (m.collection == "match")
	{
		if (m.field == ":setGameSpeed")
		{
			set_time_scale(numeric(m.value, 0, 100, false));
			return {};
		}
		if (m.field == ":pause")
		{
			m_paused = true;
			return {};
		}
		if (m.field == ":unpause")
		{
			m_paused = false;
			return {};
		}
		return "unsupported match action";
	}
	if (m.collection == "players")
	{
		auto p = logic.players().getNthPlayer(numberId(m.id));
		if (!p)
			return "expired player";
		if (m.field == ":setAIEnabled")
		{
			const bool *enabled = std::get_if<bool>(&m.value.data);
			if (!enabled)
				return "expected boolean";
			return bfme_set_ai(p->getPlayerIndex(), *enabled);
		}
		if (m.field == "revealMap" || m.field == "fogOfWar")
		{
			const bool *value = std::get_if<bool>(&m.value.data);
			if (!value || !logic.shroud()) return "map visibility is unavailable";
			if (m.field == "fogOfWar")
			{
				// the shroud's permanent reveal (RW 0xB4F5D0) / its undo (RW 0xB519B0), once per player however often it is asked
				const int index = p->getPlayerIndex();
				const bool revealed = m_luaFogRevealed.count(index) != 0;
				if (!*value && !revealed)
				{
					logic.shroud()->revealMapForPlayerPermanently(index);
					m_luaFogRevealed.insert(index);
				}
				else if (*value && revealed)
				{
					logic.shroud()->undoRevealMapForPlayerPermanently(index);
					m_luaFogRevealed.erase(index);
				}
			}
			else if (*value) logic.shroud()->revealMapForPlayer(p->getPlayerIndex());
			return {};
		}
		if (m.field == "money" || m.field == "gold")
		{
			auto amount = std::uint32_t(SimMath::ftol2(numeric(m.value, 0, 2147483647)));
			auto money = p->getMoney();
			// Use native delta bookkeeping, rather than resetting the money object or its income statistics.
			auto old = money->countMoney();
			if (amount > old)
				money->deposit(amount - old, nullptr, true);
			else if (amount < old)
				money->withdraw(old - amount, nullptr, true);
			return {};
		}
		if (m.field == "powerPoints" || m.field == "powerpoint")
		{
			int amount = int(SimMath::ftol2(numeric(m.value, 0, 1000000)));
			p->science().addSciencePurchasePoints(amount - p->science().getSciencePurchasePoints());
			return {};
		}
		auto &cp = p->commandPoints();
		int amount = int(SimMath::ftol2(numeric(m.value, m.field == "commandPointsBonus" ? -1000000 : 0, 1000000)));
		if (m.field == "commandPoints")
		{
			int extra = cp.getBonus();
			for (const auto &r : cp.records())
				if (!r.filter || p->hasObjectMatching(*r.filter, true))
					extra += r.value;
			cp.setFromScript(amount - extra, amount);
		}
		else if (m.field == "commandPointsBase")
			cp.setFromScript(amount, cp.getCap());
		else if (m.field == "commandPointsCap")
			cp.setFromScript(cp.getBase(), amount);
		else if (m.field == "commandPointsBonus")
			cp.addBonus(amount - cp.getBonus());
		else if (m.field == "commandPointsUsed")
			cp.addUsage(amount - cp.getUsage());
		else
			return "unsupported player field";
		return {};
	}
	if (m.collection == "unitInstances" || m.collection == "buildingInstances")
	{
		auto obj = logic.findObjectByID(numberId(m.id));
		if (!obj || obj->isDestroyed())
			return "expired object";
		if (m.field == ":setInvulnerable")
		{
			auto enabled = std::get_if<bool>(&m.value.data);
			if (!enabled) return "expected boolean";
			if (!obj->getBodyModule()) return "object has no body";
			setInvulnerable(*obj, *enabled);
			return {};
		}
		if (m.field == ":giveUpgrade")
		{
			auto name = std::get_if<std::string>(&m.value.data);
			auto u = name && TheUpgradeCenter ? TheUpgradeCenter->findUpgrade(*name) : nullptr;
			if (!u || u->getUpgradeType() != UPGRADE_TYPE_OBJECT) return "expected an object upgrade";
			obj->giveUpgrade(*name);
			return {};
		}
		if (m.field == ":linkWallPiece")
		{
			auto other = logic.findObjectByID(unsigned(numeric(m.value, 1, 2147483647)));
			auto gb = dynamic_cast<GettingBuiltBehavior *>(obj->findModule("GettingBuiltBehavior"));
			if (!gb || !other || other->isDestroyed() || other->getControllingPlayer() != obj->getControllingPlayer()) return "invalid linked wall piece";
			gb->addLinkedPiece(*other);
			return {};
		}
		if (m.field == ":trainUnit")
		{
			if (m.collection != "buildingInstances" || !obj->isKindOfName("STRUCTURE") || obj->isEffectivelyDead())
				return "producer must be a living building";
			auto production = obj->getProductionUpdate();
			if (!production || obj->isUnderConstruction()) return "building is not ready for production";
			auto name = std::get_if<std::string>(&m.value.data);
			auto tt = name ? m_world->things().findTemplate(*name) : nullptr;
			if (!tt || !kind(logic.templateInfo(tt->getFinalOverride()), 1) || kind(logic.templateInfo(tt->getFinalOverride()), 7))
				return "unknown unit template";
			tt = tt->getFinalOverride();
			if (m.args.size() != 1) return "expected production count";
			const int count = int(numeric(m.args[0], 1, 1000));
			for (int queued = 0; queued < count; ++queued)
			{
				// Same eligibility, unique IDs and queue path as MSG_QUEUE_UNIT_CREATE
				// (GameLogicDispatch::queueUnitCreate). One entry per requested recruitment,
				// so money/queue limits are rechecked rather than using the five-unit batch flag.
				const auto allowed = BuildAssistant::canMakeUnit(*obj, tt, -1);
				std::string reason;
				switch (allowed)
				{
				case CANMAKE_OK: break;
				case CANMAKE_NO_MONEY: reason = "not enough gold"; break;
				case CANMAKE_NO_PREREQUISITES: reason = "unit unavailable at this building or prerequisites missing"; break;
				case CANMAKE_FACTORY_IS_DISABLED: reason = "building disabled"; break;
				case CANMAKE_QUEUE_FULL: reason = "production queue full"; break;
				case CANMAKE_PARKING_PLACES_FULL: reason = "no room for units"; break;
				case CANMAKE_MAXED_OUT_FOR_PLAYER: reason = "player unit limit reached"; break;
				default: reason = "production unavailable (code " + std::to_string(allowed) + ")"; break;
				}
				if (reason.empty() && !production->queueCreateUnit(tt, -1, production->requestUniqueUnitID(), -1, false, {}, false))
					reason = "native production queue refused request";
				if (!reason.empty()) return "queued " + std::to_string(queued) + " of " + std::to_string(count) + ": " + reason;
			}
			return {};
		}
		if (m.field == ":kill")
		{
			obj->kill(DEATH_NORMAL);
			return {};
		}
		if (m.field == ":destroy")
		{
			logic.destroyObject(obj);
			return {};
		}
		if (m.field == "ownerPlayer")
		{
			auto p = logic.players().getNthPlayer(int(SimMath::ftol2(numeric(m.value, 0, 19))));
			if (!p || !p->getDefaultTeam())
				return "destination player has no default team";
			obj->setTeam(p->getDefaultTeam());
			return {};
		}
		if (m.field == "level")
		{
			auto xp = obj->getExperienceTracker();
			if (!xp || !xp->isTrainable())
				return "object has no trainable experience tracker";
			int level = int(SimMath::ftol2(numeric(m.value, 1, 100)));
			if (level < xp->getRank())
				xp->setExperienceAndLevel(0, true);
			xp->gainLevels(level - xp->getRank(), true);
			return xp->getRank() == level ? "" : "level unavailable for this object's experience levels";
		}
		return "unsupported instance field or action";
	}
	return "unknown collection";
}
} // namespace godot
