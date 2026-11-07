// OpenBFME. GPL-3.0.
// User-requested optional fishing mod; new gameplay, not a retail port.
#pragma once
#include "Common/INI.h"
#include "GameLogic/Module/UpdateModule.h"
#include <memory>
class ModuleFactory;
struct ObjectFilter;
class FishingUpdateModuleData : public ModuleData {
public:
  std::shared_ptr<ObjectFilter> targets;
  float range = 120.0f;
  UnsignedInt interval = 25;
  int amount = 25;
  static void buildFieldParse(MultiIniFieldParse &p);
};
class FishingUpdate : public UpdateModule {
public:
  OBFME_MODULE_XFER(FishingUpdate)
  FishingUpdate(Thing *thing, const FishingUpdateModuleData *data);
  static void registerExtension(ModuleFactory &modules);
  static FishingUpdate *find(Object &object);
  bool canFish(Object *point) const;
  bool order(Object *point);
  void cancel();
  ObjectID target() const { return m_target; }
  bool working() const { return m_working; }
  UpdateSleepTime update() override;
  void crc(StateHasher &hasher) const override;

private:
  bool inRange(const Object &point) const;
  const FishingUpdateModuleData *m_data;
  ObjectID m_target = INVALID_ID;
  int m_owner = -1;
  UnsignedInt m_due = 0;
  bool m_working = false;
};
void cancelFishingCommand(Object &object);
