// OpenBFME. GPL-3.0.
#pragma once
class Object;
namespace CaptureLinks
{
// Retail's 150-unit link first. Authored PORT sites may use a reciprocal nearest
// flag association out to 250 units (OpenBFME fix for Evendim's detached flags).
Object *buildingForFlag(Object &flag);
Object *flagForBuilding(Object &building);
} // namespace CaptureLinks
