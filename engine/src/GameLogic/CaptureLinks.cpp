// OpenBFME. GPL-3.0. Retail association plus the user-requested distant dock-flag fix.
#include "GameLogic/CaptureLinks.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/SimMath.h"
namespace
{
float distanceSquared(const Object &a, const Object &b)
{
	const auto &p = *a.getPosition(), &q = *b.getPosition();
	return SimMath::sumSquares3(SimMath::sseSub(p.x, q.x), SimMath::sseSub(p.y, q.y), SimMath::sseSub(p.z, q.z));
}
Object *nearestFlag(Object &building, float limit)
{
	Object *best = nullptr;
	for (auto *flag = building.logic().getFirstObject(); flag; flag = flag->getNextObject())
	{
		if (flag->isDestroyed() || !flag->isKindOfName("CAPTUREFLAG") || !flag->isKindOfName("CAPTURABLE"))
			continue;
		const float distance = distanceSquared(*flag, building);
		if (distance <= limit && (!best || distance < limit))
		{
			best = flag;
			limit = distance;
		}
	}
	return best;
}
} // namespace
Object *CaptureLinks::buildingForFlag(Object &flag)
{
 // Keep the HUD's retail object scan, including newly created objects before
 // their first partition update. Choose the same nearest linked building as capture.
 Object *native = nullptr;
 float nativeLimit = 22500.0f;
 for (auto *building = flag.logic().getFirstObject(); building; building = building->getNextObject())
 {
  if (building->isDestroyed() || !building->isKindOfName("LINKED_TO_FLAG")) continue;
  const float distance = distanceSquared(*building, flag);
  if (distance <= nativeLimit && (!native || distance < nativeLimit))
  { native = building; nativeLimit = distance; }
 }
 if (native) return native;
	Object *best = nullptr;
	float limit = 62500.0f;
	for (auto *dock = flag.logic().getFirstObject(); dock; dock = dock->getNextObject())
	{
		if (dock->isDestroyed() || !dock->isKindOfName("PORT") || !dock->isKindOfName("LINKED_TO_FLAG"))
			continue;
		const float distance = distanceSquared(*dock, flag);
		if (distance <= limit && (!best || distance < limit) && nearestFlag(*dock, 62500.0f) == &flag)
		{
			best = dock;
			limit = distance;
		}
	}
	return best;
}
Object *CaptureLinks::flagForBuilding(Object &building)
{
	if (!building.isKindOfName("LINKED_TO_FLAG"))
		return nullptr;
	auto *flag = nearestFlag(building, building.isKindOfName("PORT") ? 62500.0f : 22500.0f);
	return flag && buildingForFlag(*flag) == &building ? flag : nullptr;
}
