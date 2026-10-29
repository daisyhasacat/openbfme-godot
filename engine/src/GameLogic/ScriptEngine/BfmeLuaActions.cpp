// OpenBFME. GPL-3.0. Portable gameplay adapter extracted from GodotBfmeLua{,AI}.cpp.
// OpenBFME extension: Lua executes on the authority; peers execute typed outcomes, never source text.
#include "GameLogic/ScriptEngine/BfmeLuaActions.h"
#include "Common/BuildAssistant.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/PlayerScience.h"
#include "Common/Science.h"
#include "Common/SpecialPower.h"
#include "Common/Thing/ThingTemplate.h"
#include "Common/Thing/ThingFactory.h"
#include "Common/Upgrade.h"
#include "GameLogic/Construction.h"
#include "GameLogic/ConstructionSite.h"
#include "GameLogic/BuildPlacement.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/GameLogicDispatch.h"
#include "GameLogic/Damage.h"
#include "GameLogic/Module/DozerAIUpdate.h"
#include "GameLogic/Module/ConstructionModules.h"
#include "GameLogic/Module/ProductionUpdate.h"
#include "GameLogic/Module/SpecialPowerModules.h"
#include "GameLogic/Module/StancesBehavior.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Object/ExperienceTracker.h"
#include "GameLogic/ObjectTemplateInfo.h"
#include "GameLogic/SimMath.h"
#include "GameLogic/SkirmishAI/SkirmishAIManager.h"
#include "GameLogic/System/ShroudManager.h"
#include "GameNetwork/NetPacket.h"
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>
namespace {
using Value = BfmeLuaValue;
using Table = Value::Table;
using List = Value::List;
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
bool kind(const ObjectTemplateInfo &info, int bit)
{
	return (info.kindOf[bit / 32] & (1u << (bit % 32))) != 0;
}
double number(const Value &v, const char *what, double lo, double hi)
{
	auto n = std::get_if<double>(&v.data);
	if (!n || !std::isfinite(*n) || *n < lo || *n > hi)
		throw std::runtime_error(std::string("invalid ") + what);
	return *n;
}
const Value &at(const std::vector<Value> &args, size_t i, const char *what)
{
	if (i >= args.size())
		throw std::runtime_error(std::string("missing ") + what);
	return args[i];
}
std::string text(const Value &v, const char *what)
{
	auto s = std::get_if<std::string>(&v.data);
	if (!s || s->empty())
		throw std::runtime_error(std::string("invalid ") + what);
	return *s;
}
ObjectID objectId(const Value &v, const char *what)
{
	return (ObjectID)SimMath::ftol2(number(v, what, 1, 2147483647));
}
Coord3D location(const Value &v, GameLogic &logic)
{
	auto t = std::get_if<Table>(&v.data);
	if (!t || !t->count("x") || !t->count("y"))
		throw std::runtime_error("expected a position {x=..., y=...}");
	Coord3D p;
	p.x = SimMath::fstpDword(number(t->at("x"), "position x", -1e7, 1e7));
	p.y = SimMath::fstpDword(number(t->at("y"), "position y", -1e7, 1e7));
	p.z = logic.getGroundHeight(p.x, p.y);
	return p;
}
std::vector<ObjectID> idList(const Value &v)
{
	auto list = std::get_if<List>(&v.data);
	if (!list || list->empty())
		throw std::runtime_error("expected a list of object ids");
	std::vector<ObjectID> out;
	for (const Value &id : *list)
		out.push_back(objectId(id, "object id"));
	return out;
}
unsigned construct(GameLogic &logic, const BfmeLuaRuntime::Mutation &m)
{
	if (m.args.size() != 6 && m.args.size() != 7) throw std::runtime_error("invalid construction arguments");
	auto name = std::get_if<std::string>(&m.value.data);
	auto tt = name ? logic.things().findTemplate(*name) : nullptr;
	if (!tt || !kind(logic.templateInfo(tt->getFinalOverride()), 7))
		throw std::runtime_error("unknown building template");
	tt = tt->getFinalOverride();
	Coord3D pos{SimMath::fstpDword(numeric(m.args[0], -1e7, 1e7, false)), SimMath::fstpDword(numeric(m.args[1], -1e7, 1e7, false)), 0};
	int builderId = int(SimMath::ftol2(numeric(m.args[2], 0, 2147483647)));
	int playerId = int(SimMath::ftol2(numeric(m.args[3], 0, 19)));
	float speed = SimMath::fstpDword(numeric(m.args[4], 0.01, 100, false));
	float angle = SimMath::fstpDword(numeric(m.args[5], -1e6, 1e6, false));
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
	const int plotId = m.args.size() == 7 ? int(SimMath::ftol2(numeric(m.args[6], 0, 2147483647))) : 0;
	if (plotId)
	{
		auto plot = logic.findObjectByID(plotId);
		if (builderId || !plot || plot->isDestroyed() || plot->isEffectivelyDead() || plot->getControllingPlayer() != player ||
			!(plot->isKindOfName("BASE_FOUNDATION") || plot->isKindOfName("BASE_DEFENSE_FOUNDATION") || plot->isKindOfName("EXPANSION_PAD")))
			throw std::runtime_error("plot must be a living foundation owned by the player");
		auto obj = Construction::constructOnPlot(*plot, *tt, *plot->getPosition(), angle, *player, false);
		if (!obj) throw std::runtime_error("plot construction refused (occupied or unavailable)");
		obj->setScriptConstructionSpeed(speed);
		return obj->getID();
	}
	// Wall spans deliberately join/overlap their neighboring pieces. A layout
	// restores the final segment/tower/gate templates directly, without deleting
	// neighbors a second time through ReplaceSelfUpgrade.
	auto wall = [](const ObjectTemplateInfo &info) {
		for (const char *k : {"WALL_SEGMENT", "WALL_HUB", "WALL_UPGRADE", "WALL_GATE", "DEFENSIVE_WALL"})
		{
			int bit = ObjectTemplateInfoBuilder::kindOfIndex(k);
			if (bit >= 0 && kind(info, bit)) return true;
		}
		return false;
	};
	const bool joiningWall = !builderId && wall(logic.templateInfo(tt));
	if (joiningWall)
	{
		std::vector<BuildPlacement::Footprint> mine, other;
		BuildPlacement::footprintsOf(logic, *tt, pos, angle, mine);
		for (auto o = logic.getFirstObject(); o; o = o->getNextObject())
		{
			if (ConstructionSite::isNeutralDecoration(logic, *o)) continue;
			if (o->isDestroyed() || !o->isInWorld() || !o->getTemplate() || !o->isKindOfName("IMMOBILE") ||
				o->isKindOfName("INERT") || o->isKindOfName("CLEARED_BY_BUILD") || o->isKindOfName("MINE")) continue;
			if (o->getControllingPlayer() == player && wall(logic.templateInfo(o->getTemplate()))) continue;
			BuildPlacement::footprintsOf(logic, *o->getTemplate(), *o->getPosition(), o->getOrientation(), other);
			for (const auto &a : mine) for (const auto &b : other)
				if (BuildPlacement::footprintsCollide(a, b)) throw std::runtime_error("wall site overlaps another structure");
		}
	}
	// Scripted construction may select any structure template, but placement and
	// builder pathfinding use the same gates as normal construction.
	if (BuildPlacement::isLocationLegalToBuild(logic, pos, *tt, angle,
		LLF_TERRAIN_RESTRICTIONS | (joiningWall ? 0 : LLF_NO_OBJECT_OVERLAP), builder, player) != LBC_OK)
		throw std::runtime_error("illegal construction site");
	auto obj = dozer ? dozer->construct(*tt, pos, angle, *player)
		: Construction::constructWithoutBuilder(logic, *tt, pos, angle, *player);
	if (!obj) throw std::runtime_error("construction refused (site or builder unavailable)");
	obj->setScriptConstructionSpeed(speed);
	return obj->getID();
}
std::string order(GameLogic &logic, GameLogicDispatch &dispatcher, const BfmeLuaRuntime::Mutation &m)
{
	const int pi = numberId(m.id);
	Player *player = logic.players().getNthPlayer(pi);
	if (!player)
		return "expired player";
	const std::string order = m.field.substr(1);
	struct ImmediateCommands { GameLogicDispatch &d; void append(const GameMessage &m) { d.dispatch(m); } } commands{dispatcher};
	// player orders: no selection
	if (order == "purchaseScience")
	{
		const ScienceType st = TheScienceStore ? TheScienceStore->getScienceFromInternalName(text(at(m.args, 0, "science"), "science")) : SCIENCE_INVALID;
		if (st == SCIENCE_INVALID)
			return "unknown science";
		if (player->science().hasScience(st))
			return "science already owned";
		if (!player->science().isCapableOfPurchasingScience(st))
			return "science unavailable (prerequisites or points)";
		GameMessage msg(MSG_PURCHASE_SCIENCE, pi);
		msg.appendIntegerArgument(pi);
		msg.appendIntegerArgument(st);
		commands.append(msg);
		return {};
	}
	if (order == "castPower")
	{
		const SpecialPowerTemplate *t = TheSpecialPowerStore ? TheSpecialPowerStore->findSpecialPowerTemplate(text(at(m.args, 0, "power"), "power")) : nullptr;
		::Object *book = SpecialPowerModules::getSpellBookObject(logic, *player);
		if (!t)
			return "unknown power";
		if (!book)
			return "player has no spell book";
		SpecialPowerModuleInterface *sp = SpecialPowerModules::findModule(*book, t);
		if (!sp)
			return "power is not in the player's spell book";
		if (!sp->isReady())
			return "power is not ready";
		const Value *target = m.args.size() > 1 ? &m.args[1] : nullptr;
		if (!target || std::holds_alternative<std::monostate>(target->data))
		{
			GameMessage msg(MSG_DO_SPELLBOOK_SPECIAL_POWER, pi);
			msg.appendIntegerArgument((int)t->getID());
			msg.appendIntegerArgument(0);
			commands.append(msg);
		}
		else if (std::holds_alternative<double>(target->data))
		{
			GameMessage msg(MSG_DO_SPECIAL_POWER_AT_OBJECT, pi);
			msg.appendIntegerArgument((int)t->getID());
			msg.appendObjectIDArgument(objectId(*target, "target"));
			msg.appendIntegerArgument(0);
			msg.appendObjectIDArgument(book->getID());
			commands.append(msg);
		}
		else
		{
			GameMessage msg(MSG_DO_SPECIAL_POWER_AT_LOCATION, pi);
			msg.appendIntegerArgument((int)t->getID());
			msg.appendLocationArgument(location(*target, logic));
			msg.appendObjectIDArgument(INVALID_ID);
			msg.appendIntegerArgument(0);
			msg.appendObjectIDArgument(book->getID());
			commands.append(msg);
		}
		return {};
	}
	// group orders: every object alive and controlled by the ordering player
	const std::vector<::ObjectID> ids = idList(at(m.args, 0, "objects"));
	for (::ObjectID id : ids)
	{
		const ::Object *obj = logic.findObjectByID(id);
		if (!obj || obj->isDestroyed() || obj->isEffectivelyDead())
			return "expired object " + std::to_string(id);
		if (obj->getControllingPlayer() != player)
			return "object " + std::to_string(id) + " is not controlled by the ordering player";
	}
	if (order == "setRallyPoint")
	{
		const Coord3D dest = location(at(m.args, 1, "position"), logic);
		for (::ObjectID id : ids)
		{
			GameMessage msg(MSG_SET_RALLY_POINT, pi);
			msg.appendObjectIDArgument(id);
			msg.appendLocationArgument(dest);
			msg.appendBooleanArgument(false);
			msg.appendObjectIDArgument(INVALID_ID);
			commands.append(msg);
		}
		return {};
	}
	GameMessage msg(MSG_BEGIN_NETWORK_MESSAGES, pi); // replaced by make() below
	auto make = [&](GameMessageType type) { msg = GameMessage(type, pi); };
	if (order == "moveTo" || order == "attackMoveTo" || order == "forceMoveTo" || order == "addWaypoint" || order == "attackGround")
	{
		make(order == "moveTo" ? MSG_DO_MOVETO : order == "attackMoveTo" ? MSG_DO_ATTACKMOVETO : order == "forceMoveTo" ? MSG_DO_FORCEMOVETO
			: order == "addWaypoint" ? MSG_ADD_WAYPOINT : MSG_DO_FORCE_ATTACK_GROUND);
		msg.appendLocationArgument(location(at(m.args, 1, "position"), logic));
	}
	else if (order == "attack" || order == "forceAttack")
	{
		const ::ObjectID victim = objectId(at(m.args, 1, "target"), "target");
		const ::Object *v = logic.findObjectByID(victim);
		if (!v || v->isDestroyed() || v->isEffectivelyDead())
			return "expired target";
		make(order == "attack" ? MSG_DO_ATTACK_OBJECT : MSG_DO_FORCE_ATTACK_OBJECT);
		msg.appendObjectIDArgument(victim);
		if (order == "forceAttack")
			msg.appendLocationArgument(*v->getPosition());
	}
	else if (order == "stop")
		make(MSG_DO_STOP);
	else if (order == "enter" || order == "repair" || order == "resumeConstruction")
	{
		const ::ObjectID target = objectId(at(m.args, 1, "target"), "target");
		const ::Object *t = logic.findObjectByID(target);
		if (!t || t->isDestroyed())
			return "expired target";
		make(order == "enter" ? MSG_ENTER : order == "repair" ? MSG_DO_REPAIR : MSG_RESUME_CONSTRUCTION);
		if (order == "enter")
			msg.appendObjectIDArgument(INVALID_ID); // GarrisonCommands reads the container from argument 1
		msg.appendObjectIDArgument(target);
	}
	else if (order == "evacuate")
		make(MSG_EVACUATE);
	else if (order == "sell")
		make(MSG_SELL);
	else if (order == "setStance")
	{
		static const std::map<std::string, StanceType> stances{{"battle", STANCE_BATTLE}, {"aggressive", STANCE_AGGRESSIVE},
			{"holdGround", STANCE_HOLD_GROUND}, {"porcupine", STANCE_PORCUPINE}};
		auto s = stances.find(text(at(m.args, 1, "stance"), "stance"));
		if (s == stances.end())
			return "unknown stance (battle, aggressive, holdGround, porcupine)";
		make(MSG_CHANGE_STANCE);
		msg.appendIntegerArgument(s->second);
	}
	else if (order == "researchUpgrade" || order == "cancelUpgrade")
	{
		const UpgradeTemplate *u = TheUpgradeCenter ? TheUpgradeCenter->findUpgrade(text(at(m.args, 1, "upgrade"), "upgrade")) : nullptr;
		if (!u)
			return "unknown upgrade";
		make(order == "researchUpgrade" ? MSG_QUEUE_UPGRADE : MSG_CANCEL_UPGRADE);
		if (order == "researchUpgrade")
			msg.appendIntegerArgument((int)ids.front()); // argument 0 is the producer's id (not read, the control bar writes it)
		msg.appendIntegerArgument(u->getMaskBit());
	}
	else if (order == "useAbility")
	{
		const SpecialPowerTemplate *t = TheSpecialPowerStore ? TheSpecialPowerStore->findSpecialPowerTemplate(text(at(m.args, 1, "ability"), "ability")) : nullptr;
		if (!t)
			return "unknown ability";
		bool owned = false;
		for (::ObjectID id : ids)
			owned = owned || SpecialPowerModules::findModule(*logic.findObjectByID(id), t) != nullptr;
		if (!owned)
			return "none of the objects has that ability";
		const Value *target = m.args.size() > 2 ? &m.args[2] : nullptr;
		if (!target || std::holds_alternative<std::monostate>(target->data))
		{
			make(MSG_DO_SPECIAL_POWER);
			msg.appendIntegerArgument((int)t->getID());
			msg.appendIntegerArgument(0);
			msg.appendObjectIDArgument(INVALID_ID); // the source: the selection
		}
		else if (std::holds_alternative<double>(target->data))
		{
			make(MSG_DO_SPECIAL_POWER_AT_OBJECT);
			msg.appendIntegerArgument((int)t->getID());
			msg.appendObjectIDArgument(objectId(*target, "target"));
			msg.appendIntegerArgument(0);
			msg.appendObjectIDArgument(INVALID_ID);
		}
		else
		{
			make(MSG_DO_SPECIAL_POWER_AT_LOCATION);
			msg.appendIntegerArgument((int)t->getID());
			msg.appendLocationArgument(location(*target, logic));
			msg.appendObjectIDArgument(INVALID_ID);
			msg.appendIntegerArgument(0);
			msg.appendObjectIDArgument(INVALID_ID);
		}
	}
	else
		return "unsupported order: " + order;
	// select the ordered objects, give the order, then the player's own selection again (a human's next click must not order the script's units)
	const std::vector<::ObjectID> previous = player->selection();
	GameMessage select(MSG_CREATE_SELECTED_GROUP_NO_SOUND, pi);
	select.appendBooleanArgument(true);
	for (::ObjectID id : ids)
		select.appendObjectIDArgument(id);
	commands.append(select);
	commands.append(msg);
	if (previous.empty())
		commands.append(GameMessage(MSG_DESTROY_SELECTED_GROUP, pi));
	else
	{
		GameMessage restore(MSG_CREATE_SELECTED_GROUP_NO_SOUND, pi);
		restore.appendBooleanArgument(true);
		for (::ObjectID id : previous)
			restore.appendObjectIDArgument(id);
		commands.append(restore);
	}
	return {};
}

} // namespace

std::string BfmeLuaActions::apply(GameLogic &logic, GameLogicDispatch &dispatcher, const BfmeLuaRuntime::Mutation &m, unsigned &created)
{
	if (m.collection == "templates" || m.collection == "unitTypes" || m.collection == "buildingTypes")
	{
		auto base = logic.things().findTemplate(m.id);
		if (!base)
			return "expired template";
		auto tt = const_cast<ThingTemplate *>(base->getFinalOverride());
		static const std::map<std::string, std::string> fields{
			{"cost", "BuildCost"},		{"refund", "RefundValue"},		{"buildTime", "BuildTime"},
			{"trainTime", "BuildTime"}, {"visionRange", "VisionRange"}, {"shroudClearingRange", "ShroudClearingRange"}};
		auto f = fields.find(m.field);
		if (f == fields.end())
			return "unsupported template field";
		bool integer = m.field == "cost" || m.field == "refund";
		double n = numeric(m.value, 0, integer ? 65535 : 1e30, integer);
		FieldValue value = integer ? FieldValue((long long)SimMath::ftol2(n)) : FieldValue(SimMath::fstpDword(n));
		return tt->setRuntimeField(f->second, std::move(value)) ? "" : "template field is unavailable";
	}
	if (m.collection == "orders")
		return order(logic, dispatcher, m);
	if (m.collection == "match")
	{
		if (m.field == ":construct") { created = construct(logic, m); return {}; }
		if (m.field == ":clearNeutralDecorations")
		{
			const auto value = std::get_if<bool>(&m.value.data);
			if (!value) return "expected enabled boolean";
			logic.settings().clearNeutralDecorations = *value;
			return {};
		}
		return "this match action is not supported by the synchronized Lua channel";
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
			return logic.skirmishAI().setScriptControlled(p->getPlayerIndex(), !*enabled) ? "" : "player has no skirmish AI";
		}
		if (m.field == "revealMap" || m.field == "fogOfWar")
		{
			const bool *value = std::get_if<bool>(&m.value.data);
			if (!value || !logic.shroud()) return "map visibility is unavailable";
			if (m.field == "fogOfWar") logic.shroud()->setCheatFogDisabled(p->getPlayerIndex(), !*value);
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
			auto other = logic.findObjectByID(unsigned(SimMath::ftol2(numeric(m.value, 1, 2147483647))));
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
			auto tt = name ? logic.things().findTemplate(*name) : nullptr;
			if (!tt || !kind(logic.templateInfo(tt->getFinalOverride()), 1) || kind(logic.templateInfo(tt->getFinalOverride()), 7))
				return "unknown unit template";
			tt = tt->getFinalOverride();
			if (m.args.size() != 1) return "expected production count";
			const int count = int(SimMath::ftol2(numeric(m.args[0], 1, 1000)));
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
			obj->kill(DAMAGE_UNRESISTABLE, DEATH_NORMAL);
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

namespace {
void bound(bool ok, const char *message) { if (!ok) throw std::runtime_error(message); }
void stringBound(const std::string &s) { bound(s.size() <= 4096, "Lua action string exceeds 4096 bytes"); }
void putValue(NetByteWriter &w, const Value &v, unsigned depth, unsigned &nodes)
{
	bound(depth <= 8 && ++nodes <= 2048, "Lua action value is too complex");
	w.u8((std::uint8_t)v.data.index());
	if (auto b = std::get_if<bool>(&v.data)) w.u8(*b ? 1 : 0);
	else if (auto n = std::get_if<double>(&v.data)) {
		bound(std::isfinite(*n), "Lua action number must be finite");
		std::uint64_t bits; std::memcpy(&bits, n, sizeof bits); w.u32((std::uint32_t)bits); w.u32((std::uint32_t)(bits >> 32));
	} else if (auto s = std::get_if<std::string>(&v.data)) { stringBound(*s); w.str(*s); }
	else if (auto t = std::get_if<Table>(&v.data)) {
		bound(t->size() <= 2048, "Lua action table is too large"); w.u32((std::uint32_t)t->size());
		for (const auto &kv : *t) { stringBound(kv.first); w.str(kv.first); putValue(w, kv.second, depth + 1, nodes); }
	} else if (auto a = std::get_if<List>(&v.data)) {
		bound(a->size() <= 2048, "Lua action list is too large"); w.u32((std::uint32_t)a->size());
		for (const auto &item : *a) putValue(w, item, depth + 1, nodes);
	}
	bound(w.data().size() <= BfmeLuaActions::kMaxBytes, "Lua action exceeds 16 KiB");
}
Value getValue(NetByteReader &r, unsigned depth, unsigned &nodes)
{
	bound(!r.failed() && depth <= 8 && ++nodes <= 2048, "Lua action value is too complex or truncated");
	const unsigned tag = r.u8();
	switch (tag) {
	case 0: return {};
	case 1: return r.flag();
	case 2: {
		const std::uint64_t lo = r.u32(), hi = r.u32(), bits = lo | (hi << 32);
		double n; std::memcpy(&n, &bits, sizeof n); bound(std::isfinite(n), "Lua action number must be finite"); return n;
	}
	case 3: { auto s = r.str(); stringBound(s); return s; }
	case 4: {
		Table out; const auto count = r.u32(); bound(count <= 2048, "Lua action table is too large");
		for (unsigned i = 0; i < count; ++i) {
			auto key = r.str(); stringBound(key); auto v = getValue(r, depth + 1, nodes);
			bound(out.emplace(std::move(key), std::move(v)).second, "duplicate Lua action table key");
		} return out;
	}
	case 5: {
		List out; const auto count = r.u32(); bound(count <= 2048, "Lua action list is too large");
		for (unsigned i = 0; i < count; ++i) out.push_back(getValue(r, depth + 1, nodes)); return out;
	}
	default: throw std::runtime_error("unknown Lua action value type");
	}
}
}

BfmeLuaActionState::Outcome BfmeLuaActionState::outcome(std::uint64_t request) const
{
	const auto it = outcomes.find(request); return it == outcomes.end() ? Outcome{request <= lastRequest ? "expired action result" : "pending", 0} : it->second;
}
bool BfmeLuaActions::presentationOnly(const BfmeLuaRuntime::Mutation &m) { return m.collection == "graphics"; }
GameMessage BfmeLuaActions::encode(int player, std::uint64_t request, const BfmeLuaRuntime::Mutation &m)
{
	bound(request != 0, "Lua action request must be nonzero");
	bound(m.collection.size() <= 64 && m.id.size() <= 256 && m.field.size() <= 64, "Lua action identity is too long");
	NetByteWriter w; w.u8(1); w.u32((std::uint32_t)request); w.u32((std::uint32_t)(request >> 32));
	w.str(m.collection); w.str(m.id); w.str(m.field);
	unsigned nodes = 0; putValue(w, m.value, 0, nodes); putValue(w, Value(m.args), 0, nodes);
	GameMessage out(MSG_OPENBFME_LUA_ACTION, player); out.appendIntegerArgument((int)w.data().size());
	for (size_t i = 0; i < w.data().size(); i += 4) {
		std::uint32_t word = 0; for (unsigned j = 0; j < 4 && i + j < w.data().size(); ++j) word |= (std::uint32_t)w.data()[i + j] << (8 * j);
		std::int32_t signedWord; std::memcpy(&signedWord, &word, sizeof word); out.appendIntegerArgument(signedWord);
	} return out;
}
bool BfmeLuaActions::decode(const GameMessage &message, std::uint64_t &request, BfmeLuaRuntime::Mutation &m, std::string &error)
{
	try {
		bound(message.getType() == MSG_OPENBFME_LUA_ACTION, "not a Lua action");
		const auto first = message.getArgument(0);
		bound(first && first->type == ARGUMENTDATATYPE_INTEGER && first->integer > 0 && first->integer <= (int)kMaxBytes, "invalid Lua action byte count");
		const unsigned n = (unsigned)first->integer;
		bound(message.getArgumentCount() == 1 + (n + 3) / 4, "invalid Lua action argument count");
		std::vector<std::uint8_t> bytes;
		for (size_t i = 1; i < message.getArgumentCount(); ++i) {
			const auto a = message.getArgument(i); bound(a->type == ARGUMENTDATATYPE_INTEGER, "Lua action word must be an integer");
			const auto word = (std::uint32_t)a->integer;
			for (unsigned j = 0; j < 4; ++j) {
				const auto b = (std::uint8_t)(word >> (8 * j));
				if (bytes.size() < n) bytes.push_back(b); else bound(b == 0, "nonzero Lua action padding");
			}
		}
		NetByteReader r(bytes); bound(r.u8() == 1, "unsupported Lua action version");
		const std::uint64_t lo = r.u32(), hi = r.u32(); request = lo | (hi << 32); bound(request != 0, "Lua action request must be nonzero");
		m.collection = r.str(); m.id = r.str(); m.field = r.str();
		bound(m.collection.size() <= 64 && m.id.size() <= 256 && m.field.size() <= 64, "Lua action identity is too long");
		unsigned nodes = 0; m.value = getValue(r, 0, nodes); auto args = getValue(r, 0, nodes);
		const auto list = std::get_if<List>(&args.data); bound(list != nullptr, "Lua action arguments must be a list"); m.args = *list;
		bound(!r.failed() && !r.badFlag() && r.atEnd(), "truncated or trailing Lua action bytes"); return true;
	} catch (const std::exception &e) { error = e.what(); return false; }
}
std::string BfmeLuaActions::dispatch(GameLogic &logic, GameLogicDispatch &dispatcher, const GameMessage &m)
{
	auto &state = logic.luaActions();
	if (state.authorityPlayer < 0 || m.getPlayerIndex() != state.authorityPlayer) return "only the configured host may submit Lua actions";
	BfmeLuaRuntime::Mutation mutation; std::uint64_t request = 0; std::string error;
	if (!decode(m, request, mutation, error)) return error;
	if (state.outcomes.count(request)) return {}; // exact reliable-stream retries never apply twice
	if (request <= state.lastRequest) return "expired or out-of-order Lua action request";
	state.lastRequest = request;
	unsigned created = 0;
	try { error = apply(logic, dispatcher, mutation, created); } catch (const std::exception &e) { error = e.what(); }
	state.outcomes[request] = {error.empty() ? (created ? "building" : "applied") : error, created};
	while (state.outcomes.size() > 1024) state.outcomes.erase(state.outcomes.begin());
	return error;
}
