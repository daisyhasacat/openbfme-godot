// OpenBFME. GPL-3.0. Host-authored Lua decisions, portable peer/replay execution.
#include "doctest.h"
#include "CombatTestUtil.h"
#include "ProdTestUtil.h"
#include "Common/Recorder.h"
#include "GameLogic/ScriptEngine/BfmeLuaActions.h"
#include "GameNetwork/NetPacket.h"
#include <limits>

TEST_CASE("Lua network: bounded typed codec survives wire and rejects malformed data")
{
	BfmeLuaRuntime::Mutation m;
	m.collection = "players"; m.id = "1"; m.field = "money"; m.value = 4321;
	m.args = {true, "hello", BfmeLuaValue::Table{{"point", BfmeLuaValue::List{1, 2, 3}}}};
	const auto message = BfmeLuaActions::encode(10, 0x100000002ull, m);
	NetByteWriter wire; std::string error;
	REQUIRE(NetPacket::writeGameMessage(wire, message, &error));
	NetByteReader reader(wire.data());
	auto received = NetPacket::readGameMessage(reader, &error);
	REQUIRE(received);
	std::uint64_t request = 0; BfmeLuaRuntime::Mutation back;
	REQUIRE_MESSAGE(BfmeLuaActions::decode(*received, request, back, error), error);
	CHECK(request == 0x100000002ull);
	CHECK(back.collection == m.collection); CHECK(back.id == m.id); CHECK(back.field == m.field);
	CHECK(std::get<double>(back.value.data) == 4321);
	REQUIRE(back.args.size() == 3);
	CHECK(std::get<bool>(back.args[0].data));
	CHECK(std::get<std::string>(back.args[1].data) == "hello");
	CHECK(std::get<BfmeLuaValue::List>(std::get<BfmeLuaValue::Table>(back.args[2].data).at("point").data).size() == 3);
	auto extra = message; extra.appendIntegerArgument(0);
	CHECK_FALSE(BfmeLuaActions::decode(extra, request, back, error));
	GameMessage huge(MSG_OPENBFME_LUA_ACTION, 10); huge.appendIntegerArgument(16385);
	CHECK_FALSE(BfmeLuaActions::decode(huge, request, back, error));
	CHECK_THROWS(BfmeLuaActions::encode(10, 0, m));
	m.value = std::numeric_limits<double>::infinity();
	CHECK_THROWS(BfmeLuaActions::encode(10, 1, m));
	m.value = std::string(4097, 'x');
	CHECK_THROWS(BfmeLuaActions::encode(10, 1, m));
	m.value = 0;
	for (int i=0; i<10; ++i) m.value = BfmeLuaValue::List{m.value};
	CHECK_THROWS(BfmeLuaActions::encode(10, 1, m));
}

TEST_CASE("Lua network: only fixed host writes, retries and stale requests cannot apply again")
{
	combattest::CombatWorld w;
	const int host = w.playerOf('A')->getPlayerIndex();
	w.logic->luaActions().authorityPlayer = host;
	BfmeLuaRuntime::Mutation m; m.collection = "players"; m.id = std::to_string(host); m.field = "money"; m.value = 4321;
	const int before = w.cash('A');
	auto forged = BfmeLuaActions::encode(w.playerOf('B')->getPlayerIndex(), 100, m);
	CHECK_FALSE(BfmeLuaActions::dispatch(*w.logic, w.dispatcher, forged).empty());
	CHECK(w.cash('A') == before); CHECK(w.logic->luaActions().lastRequest == 0);
	auto accepted = BfmeLuaActions::encode(host, 1, m);
	CHECK(BfmeLuaActions::dispatch(*w.logic, w.dispatcher, accepted).empty());
	CHECK(w.cash('A') == 4321); CHECK(w.logic->luaActions().outcome(1).status == "applied");
	m.value = 123;
	CHECK(BfmeLuaActions::dispatch(*w.logic, w.dispatcher, BfmeLuaActions::encode(host, 1, m)).empty());
	CHECK(w.cash('A') == 4321);
	w.logic->luaActions().outcomes.clear(); // expiry of UI results must not allow old commands
	CHECK_FALSE(BfmeLuaActions::dispatch(*w.logic, w.dispatcher, accepted).empty());
	CHECK(w.cash('A') == 4321);
	m.field = "unknown";
	CHECK_FALSE(BfmeLuaActions::dispatch(*w.logic, w.dispatcher, BfmeLuaActions::encode(host, 2, m)).empty());
	CHECK(w.cash('A') == 4321);
	w.logic->luaActions().authorityPlayer = -1;
	CHECK_FALSE(BfmeLuaActions::dispatch(*w.logic, w.dispatcher, BfmeLuaActions::encode(host, 3, m)).empty());
}

TEST_CASE("Lua network: recorded action plays without Lua and retains START authority")
{
	std::vector<std::uint8_t> bytes; std::uint32_t liveHash = 0; int host = -1;
	{
		combattest::CombatWorld w; host = w.playerOf('A')->getPlayerIndex();
		w.logic->luaActions().authorityPlayer = host;
		BfmeLuaRuntime::Mutation m; m.collection="players"; m.id=std::to_string(host); m.field="money"; m.value=9876;
		CommandList commands; commands.append(BfmeLuaActions::encode(host, 1, m));
		ReplayHeader h; h.game.game.slots[0].state = SLOT_PLAYER; h.game.game.luaAuthoritySlot = 0;
		h.game.game.peaceTimeMinutes = 5; h.game.game.fortressConstruction = true; h.game.game.slots[0].handicap = -50;
		ReplayWriter writer; writer.setKeepCopy(true); std::string error;
		REQUIRE_MESSAGE(writer.open("", h, &error), error);
		writer.recordFrame(0, commands);
		w.dispatcher.dispatch(commands.messages().front());
		liveHash = w.logic->computeStateHash();
		writer.recordHash(1, liveHash);
		REQUIRE_MESSAGE(writer.logUpTo(1, liveHash, bytes, &error), error);
	}
	ReplayFile replay; std::string error;
	REQUIRE_MESSAGE(ReplayFile::parse(bytes, replay, &error), error);
	CHECK(replay.header.game.game.luaAuthoritySlot == 0);
	CHECK(replay.header.game.game.peaceTimeMinutes == 5);
	CHECK(replay.header.game.game.fortressConstruction);
	CHECK(replay.header.game.game.slots[0].handicap == -50);
	// Build legacy headers independently: the upstream and fork used versions 7..9 for different layouts.
	NetByteReader header(bytes);
	for (int i = 0; i < 12; ++i) header.u8();
	header.str(); header.str(); header.str(); header.str(); // identity and recording build
	NewGameMessage setup;
	REQUIRE_MESSAGE(NetPacket::readNewGameMessage(header, setup, &error), error);
	const auto setupEnd = bytes.size() - header.remaining();
	auto legacyReplay = [&](int version, bool local) {
		NewGameMessage legacySetup = setup;
		legacySetup.game.peaceTimeMinutes = 0;
		legacySetup.game.luaAuthoritySlot = -1;
		legacySetup.game.fortressConstruction = false;
		NetByteWriter base; NetPacket::writeNewGameMessage(base, legacySetup);
		auto body = base.data(); body.resize(body.size() - 4); // no Cheat Mode
		const auto profileAt = body.size() - 4 * LEGACY_MAX_SLOTS - 4;
		if (version < 7 || (local && version < 9)) body.resize(body.size() - 4 * LEGACY_MAX_SLOTS);
		if (local && version >= 7) {
			NetByteWriter fields; fields.i32(5); if (version >= 8) fields.i32(0);
			body.insert(body.begin() + profileAt, fields.data().begin(), fields.data().end());
		}
		NetByteWriter prefix;
		for (int i = 0; i < 8; ++i) prefix.u8(bytes[i]);
		prefix.u32(version); prefix.str(replay.header.profile.text);
		auto oldBytes = prefix.data(); oldBytes.insert(oldBytes.end(), body.begin(), body.end());
		oldBytes.insert(oldBytes.end(), bytes.begin() + setupEnd, bytes.end());
		return oldBytes;
	};
	for (int version : { 5, 6, 7, 8, 9 }) {
		ReplayFile old;
		REQUIRE_MESSAGE(ReplayFile::parse(legacyReplay(version, true), old, &error), error);
		const int expectedAuthority = version >= 8 ? 0 : -1;
		CHECK(old.header.game.game.luaAuthoritySlot == expectedAuthority);
		const int expectedPeace = version >= 7 ? 5 : 0;
		CHECK(old.header.game.game.peaceTimeMinutes == expectedPeace);
	}
	ReplayFile upstreamSeven;
	REQUIRE_MESSAGE(ReplayFile::parse(legacyReplay(7, false), upstreamSeven, &error), error);
	CHECK(upstreamSeven.header.game.game.luaAuthoritySlot == -1);
	CHECK(upstreamSeven.header.game.game.slots[0].handicap == -50);
	combattest::CombatWorld w; w.logic->luaActions().authorityPlayer = host;
	REQUIRE(replay.frames.at(0).size() == 1);
	w.dispatcher.dispatch(replay.frames.at(0).front());
	CHECK(w.cash('A') == 9876);
	CHECK(w.logic->computeStateHash() == liveHash);
}

TEST_CASE("Lua network: native orders execute immediately in the committed frame and restore selection")
{
	combattest::CombatWorld w;
	auto unit=w.unit("Swordsman",'A',100,100);
	auto other=w.unit("Swordsman",'A',140,100);
	REQUIRE(unit); REQUIRE(other);
	auto player=w.playerOf('A'); const int host=player->getPlayerIndex();
	w.logic->luaActions().authorityPlayer=host;
	GameMessage select(MSG_CREATE_SELECTED_GROUP_NO_SOUND,host); select.appendBooleanArgument(true); select.appendObjectIDArgument(other->getID());
	w.dispatcher.dispatch(select); const auto before=player->selection();
	BfmeLuaRuntime::Mutation m; m.collection="orders"; m.id=std::to_string(host); m.field=":moveTo";
	m.args={BfmeLuaValue::List{unit->getID()},BfmeLuaValue::Table{{"x",200},{"y",100}}};
	CHECK(BfmeLuaActions::dispatch(*w.logic,w.dispatcher,BfmeLuaActions::encode(host,1,m)).empty());
	CHECK(player->selection()==before);
	CHECK(w.commands.messages().empty()); // never feeds derived commands back into network input
	CHECK(w.logic->luaActions().outcome(1).status=="applied");
}

TEST_CASE("Lua network: committed construction pays once and returns the peer-created object")
{
	combattest::CombatWorld w(
		"Object LuaHall\n"
		" BuildCost = 300\n BuildTime = 20\n KindOf = STRUCTURE SELECTABLE IMMOBILE\n"
		" Behavior = GettingBuiltBehavior ModuleTag_Build\n SpawnTimer = 300\n End\n"
		" Body = ActiveBody ModuleTag_Body\n MaxHealth = 1000\n MaxHealthDamaged = 500\n MaxHealthReallyDamaged = 100\n End\nEnd\n");
	w.logic->productionSettings()=prodtest::defaultSettings();
	w.logic->settings().buildRulesLoaded=true;
	w.logic->settings().allowedHeightVariationForBuilding=10.0f;
	auto player=w.playerOf('A'); const int host=player->getPlayerIndex();
	player->getMoney()->deposit(1000,nullptr,true); const int cash=w.cash('A');
	w.logic->luaActions().authorityPlayer=host;
	BfmeLuaRuntime::Mutation m; m.collection="match"; m.field=":construct"; m.value="LuaHall";
	m.args={300,300,0,host,1,0,0};
	auto message=BfmeLuaActions::encode(host,1,m);
	const auto error=BfmeLuaActions::dispatch(*w.logic,w.dispatcher,message);
	REQUIRE_MESSAGE(error.empty(),error);
	auto outcome=w.logic->luaActions().outcome(1);
	CHECK(outcome.status=="building");
	auto building=w.logic->findObjectByID(outcome.object);
	REQUIRE(building); CHECK(building->isUnderConstruction()); CHECK(building->getControllingPlayer()==player);
	CHECK(w.cash('A')==cash-300);
	CHECK(BfmeLuaActions::dispatch(*w.logic,w.dispatcher,message).empty());
	CHECK(w.cash('A')==cash-300);
}
