// OpenBFME. GPL-3.0. OpenBFME hover aid; no simulation state or orders are changed.
#include "GameClient/WallUpgradePreview.h"
#include "GameClient/ControlBarCommands.h"
#include "Common/Thing/ThingTemplate.h"
#include "Common/Thing/ThingFactory.h"
#include "GameLogic/ObjectTemplateInfo.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Module/ConstructionModules.h"
#include "GameLogic/Module/ReplaceSelfUpgrade.h"
#include "GameLogic/WallSpan.h"
#include <algorithm>
#include <cmath>
namespace WallUpgradePreview {
namespace {
bool segment(const Object &o) {
 return !o.isDestroyed()&&!o.isEffectivelyDead()&&o.isInWorld()&&!o.isKindOfName("WALL_HUB")&&!o.isKindOfName("WALL_UPGRADE")
  &&(o.isKindOfName("WALL_SEGMENT")||o.isKindOfName("DEFENSIVE_WALL"));
}
}
bool expansion(GameLogic &logic,const Object &source,const CommandButton &button,bool &gate) {
 gate=false;
 if(!segment(source)||button.m_command!=GUI_COMMAND_OBJECT_UPGRADE||button.m_upgradeName.empty())return false;
 for(const auto &module:source.modules())if(const auto *replace=dynamic_cast<const ReplaceSelfUpgrade *>(module.get())) {
  const auto *data=dynamic_cast<const ReplaceSelfUpgradeModuleData *>(replace->muxData());
  if(!data||std::find(data->m_triggeredBy.begin(),data->m_triggeredBy.end(),button.m_upgradeName)==data->m_triggeredBy.end())continue;
  bool portal=false;
  for(const auto &name:data->m_pieces) {
   const auto *piece=logic.things().findTemplate(name);if(!piece)return false;
   gate|=MaskTest(ObjectTemplateInfoBuilder::build(*piece->getFinalOverride()).kindOf,(unsigned)ObjectTemplateInfoBuilder::kindOfIndex("WALL_GATE"));
   for(const auto &n:piece->getFinalOverride()->behaviorModules().nuggets()) {
    gate|=n.name=="GateOpenAndCloseBehavior";portal|=n.name=="DynamicPortalBehaviour";
   }
  }
  gate=gate&&!portal;return !data->m_pieces.empty();
 }
 return false;
}
std::vector<ObjectID> objects(GameLogic &logic,const Object &source,bool gate) {
 if(!segment(source))return {};
 std::vector<ObjectID> result{source.getID()};
 const auto *built=dynamic_cast<const GettingBuiltBehavior *>(source.findModule("GettingBuiltBehavior"));
 if(!built)return result;
 const double c=std::cos(source.getOrientation()),s=std::sin(source.getOrientation());
 const double step=2.0*(WallSpan::halfExtentY(*source.getTemplate())-1.0);
 if(step<=0)return result;
 for(const auto &slot:built->linkedPieces()) {
  const Object *o=logic.findObjectByID(slot.id);
  if(!o||o==&source||!segment(*o)||o->getControllingPlayer()!=source.getControllingPlayer())continue;
  const double dx=slot.pos.x-source.getPosition()->x,dy=slot.pos.y-source.getPosition()->y;
  if(std::abs(dx*c+dy*s)>1.0||std::abs(std::cos(o->getOrientation()-source.getOrientation()))<.99)continue;
  const double along=-dx*s+dy*c;
  const int index=(int)std::lround(along/step),distance=std::abs(index);
  if(std::abs(along-index*step)>1.0||distance==0)continue;
  if(gate ? distance%2==1 : distance%2==0)result.push_back(o->getID());
 }
 std::sort(result.begin(),result.end());result.erase(std::unique(result.begin(),result.end()),result.end());return result;
}
}
