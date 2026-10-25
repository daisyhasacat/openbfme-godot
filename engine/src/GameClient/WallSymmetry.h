// OpenBFME. GPL-3.0. Optional client placement aid; no new simulation command.
#pragma once
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Module/CastleModules.h"
#include "GameLogic/Module/ConstructionModules.h"
#include "GameLogic/WallSpan.h"
#include <cmath>

namespace WallSymmetry
{
inline bool liveHub(const Object &o)
{
    return o.isKindOfName("WALL_HUB") && o.isInWorld() && !o.isDestroyed() && !o.isEffectivelyDead() && !o.isUnderConstruction();
}
// Expansion occupants inherit membership from their pad. Standalone hubs only
// qualify through one recorded span to a fortress hub (never a proximity guess).
inline Object *castleOf(GameLogic &logic, const Object &hub)
{
    const auto *member = dynamic_cast<const CastleMemberBehavior *>(hub.findModule("CastleMemberBehavior"));
    if (!member) {
        const Object *pad = logic.findObjectByID(hub.getProducerID());
        member = pad && (pad->isKindOfName("EXPANSION_PAD") || pad->isKindOfName("BASE_FOUNDATION") || pad->isKindOfName("BASE_DEFENSE_FOUNDATION"))
            ? dynamic_cast<const CastleMemberBehavior *>(pad->findModule("CastleMemberBehavior")) : nullptr;
    }
    Object *castle = member ? logic.findObjectByID(member->castleId()) : nullptr;
    return castle && castle->isInWorld() && !castle->isDestroyed() && !castle->isEffectivelyDead()
        && castle->getControllingPlayer() == hub.getControllingPlayer() ? castle : nullptr;
}
inline Object *anchor(GameLogic &logic, const Object &hub)
{
    if (castleOf(logic, hub)) return const_cast<Object *>(&hub);
    const auto *built = dynamic_cast<const GettingBuiltBehavior *>(hub.findModule("GettingBuiltBehavior"));
    if (!built) return nullptr;
    Object *result = nullptr;
    for (const auto &piece : built->linkedPieces()) {
        Object *other = logic.findObjectByID(piece.id);
        if (!other || !liveHub(*other) || other->getControllingPlayer() != hub.getControllingPlayer() || !castleOf(logic, *other)) continue;
        if (!result || other->getID() < result->getID()) result = other;
    }
    return result;
}
inline Coord3D reflect(const Coord3D &point, const Object &castle)
{
    const Coord3D &c = *castle.getPosition();
    const float x = std::cos(castle.getOrientation()), y = std::sin(castle.getOrientation());
    const float along = (point.x-c.x)*x + (point.y-c.y)*y;
    return {c.x + 2*along*x - (point.x-c.x), c.y + 2*along*y - (point.y-c.y), point.z};
}
inline Object *partner(GameLogic &logic, const Object &hub, const Coord3D &end, unsigned options, Coord3D &otherEnd)
{
    if (!liveHub(hub)) return nullptr;
    Object *root = anchor(logic, hub);
    Object *castle = root ? castleOf(logic, *root) : nullptr;
    if (!castle) return nullptr;
    const float dx = root->getPosition()->x-castle->getPosition()->x;
    const float dy = root->getPosition()->y-castle->getPosition()->y;
    // SAGE local +X is forward. Front corner hubs lie on opposite sides of it.
    const float c = std::cos(castle->getOrientation()), s = std::sin(castle->getOrientation());
    if (dx*c+dy*s <= 0 || std::abs(-dx*s+dy*c) <= 1) return nullptr;
    const Coord3D reflected = reflect(*hub.getPosition(), *castle);
    Object *best = nullptr;
    float bestDistance = 1.0f; // placement-coordinate rounding only (one world unit)
    for (Object *o = logic.getFirstObject(); o; o = o->getNextObject()) {
        if (o == &hub || !liveHub(*o) || o->getControllingPlayer() != hub.getControllingPlayer()
            || o->getTemplate() != hub.getTemplate() || !WallHubBehavior::find(*o, options)) continue;
        Object *otherRoot = anchor(logic, *o);
        if (!otherRoot || castleOf(logic, *otherRoot) != castle || ((root == &hub) != (otherRoot == o))) continue;
        const Coord3D rootReflection = reflect(*root->getPosition(), *castle);
        const float rx = otherRoot->getPosition()->x-rootReflection.x, ry = otherRoot->getPosition()->y-rootReflection.y;
        if (rx*rx+ry*ry > 1) continue;
        const float ox = o->getPosition()->x-reflected.x, oy = o->getPosition()->y-reflected.y;
        const float distance = ox*ox+oy*oy;
        if (distance < bestDistance || (distance == bestDistance && best && o->getID() < best->getID())) {
            best = o; bestDistance = distance;
        }
    }
    if (best) {
        otherEnd = reflect(end, *castle);
        otherEnd.z = logic.getGroundHeight(otherEnd.x, otherEnd.y);
    }
    return best;
}
}
