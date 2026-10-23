// OpenBFME. GPL-3.0. Optional authored gameplay, not a retail implementation.
#include "GameLogic/Module/FishingUpdate.h"
#include "Common/Player.h"
#include "Common/StateHash.h"
#include "Common/Thing/ModuleFactory.h"
#include "GameLogic/Economy.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/ObjectFilter.h"
#include "GameLogic/ObjectFilterMatch.h"
#include "GameLogic/SimMath.h"
#include "GameLogic/SnapshotXfer.h"
#include <cstddef>
#include <stdexcept>

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#endif

namespace {
void parseTargets(INI *ini, void *instance, void *store, const void *) {
  auto f = std::make_shared<ObjectFilter>();
  ParseObjectFilter(ini, instance, f.get(), nullptr);
  *static_cast<std::shared_ptr<ObjectFilter> *>(store) = std::move(f);
}
#define OFF(m) (int)offsetof(FishingUpdateModuleData, m)
const FieldParse fields[] = {
    {"TargetFilter", parseTargets, nullptr, OFF(targets)},
    {"Range", INI::parseReal, nullptr, OFF(range)},
    {"IncomeInterval", INI::parseDurationUnsignedInt, nullptr, OFF(interval)},
    {"IncomeAmount", INI::parseInt, nullptr, OFF(amount)},
    {nullptr, nullptr, nullptr, 0}};
#undef OFF
} // namespace
void FishingUpdateModuleData::buildFieldParse(MultiIniFieldParse &p) {
  p.add(fields);
}
void FishingUpdate::registerExtension(ModuleFactory &modules) {
  modules.registerExtension("OpenBFMEFishingUpdate", 1);
  modules.bindTypedData<FishingUpdateModuleData>("OpenBFMEFishingUpdate",
                                                 MODULETYPE_BEHAVIOR);
  modules.bindModuleProc(
      "OpenBFMEFishingUpdate", MODULETYPE_BEHAVIOR,
      [](Thing *thing, const ModuleData *data,
         const ModuleFactory::ModuleTemplate &) -> std::unique_ptr<Module> {
        return std::make_unique<FishingUpdate>(
            thing, dynamic_cast<const FishingUpdateModuleData *>(data));
      });
}
FishingUpdate::FishingUpdate(Thing *thing, const FishingUpdateModuleData *data)
    : UpdateModule(thing, data), m_data(data) {
  if (!data || !data->targets ||
      !ObjectFilterMatch::isValid(data->targets.get()) ||
      !(data->range > 0.0f) || data->interval == 0 || data->amount <= 0)
    throw std::runtime_error("OpenBFMEFishingUpdate requires a target filter, "
                             "positive range, interval and income");
  setWakeFrame(getObject(), UPDATE_SLEEP_NONE);
}
FishingUpdate *FishingUpdate::find(Object &o) {
  return dynamic_cast<FishingUpdate *>(o.findModule("OpenBFMEFishingUpdate"));
}
bool FishingUpdate::canFish(Object *point) const {
  const Object *self = getObject();
  return point && point != self && !point->isDestroyed() &&
         !point->isEffectivelyDead() && !self->isDestroyed() &&
         !self->isEffectivelyDead() && !self->getContainedBy() &&
         self->getControllingPlayer() && self->getAIUpdateInterface() &&
         self->getAIUpdateInterface()->allowedToRespondToCommand(
             CMD_FROM_PLAYER, 0) &&
         ObjectFilterMatch::allows(self->logic(), *m_data->targets, *point,
                                   self->getControllingPlayer());
}
bool FishingUpdate::inRange(const Object &point) const {
  const Coord3D &a = *getObject()->getPosition(), &b = *point.getPosition();
  return SimMath::sumSquares2(SimMath::sseSub(a.x, b.x),
                              SimMath::sseSub(a.y, b.y)) <=
         SimMath::sseMul(m_data->range, m_data->range);
}
bool FishingUpdate::order(Object *point) {
  if (!canFish(point))
    return false;
  Object *self = getObject();
  // This normal movement order uses the ship's water pathfinder. Set our task
  // AFTER it accepts the move, because every new AI command cancels the
  // preceding task.
  self->getAIUpdateInterface()->aiMoveToPosition(*point->getPosition(),
                                                 CMD_FROM_PLAYER);
  m_target = point->getID();
  m_owner = self->getControllingPlayer()->getPlayerIndex();
  m_due = 0;
  m_working = false;
  return true;
}
void FishingUpdate::cancel() {
  m_target = INVALID_ID;
  m_owner = -1;
  m_due = 0;
  m_working = false;
}
void cancelFishingCommand(Object &object) {
  if (auto *f = FishingUpdate::find(object))
    f->cancel();
}
UpdateSleepTime FishingUpdate::update() {
  if (m_target == INVALID_ID)
    return UPDATE_SLEEP_NONE;
  Object *self = getObject();
  Object *point = self->logic().findObjectByID(m_target);
  auto *owner = self->getControllingPlayer();
  if (!owner || owner->getPlayerIndex() != m_owner || !canFish(point)) {
    cancel();
    return UPDATE_SLEEP_NONE;
  }
  if (!inRange(*point)) {
    // No partial interval survives leaving the fishing radius.
    m_working = false;
    m_due = 0;
    if (!self->getAIUpdateInterface()->isMoving())
      cancel();
    return UPDATE_SLEEP_NONE;
  }
  const auto now = self->logic().getFrame();
  if (!m_working) {
    // Halt beside the school, retaining the explicit task after the idle
    // command.
    const ObjectID pointID = m_target;
    const int ownerID = m_owner;
    self->getAIUpdateInterface()->aiIdle(CMD_FROM_AI);
    m_target = pointID;
    m_owner = ownerID;
    m_working = true;
    m_due = now + m_data->interval;
  }
  if (now >= m_due) {
    auto &economy = self->logic().economy();
    const int amount = economy.applyIncomeMultipliers(
        *owner, SimMath::sseFromInt32(m_data->amount));
    if (amount > 0) {
      owner->depositMoney((std::uint32_t)amount, true);
      economy.noteIncome(*self, *owner, amount);
    }
    m_due = now + m_data->interval;
  }
  return UPDATE_SLEEP_NONE;
}
void FishingUpdate::crc(StateHasher &h) const {
  UpdateModule::crc(h);
  h.addU32(m_target);
  h.addU32(m_owner);
  h.addU32(m_due);
  h.addU32(m_working ? 1u : 0u);
}
void FishingUpdate::xfer(Xfer &x) {
  SnapshotXfer::version(x, 1, 1);
  xferUpdateModule(x);
  x.xferObjectID(&m_target);
  x.xferInt(&m_owner);
  x.xferUnsignedInt(&m_due);
  x.xferBool(&m_working);
}
