// OpenBFME. GPL-3.0. Device presentation of the APT LivingWorldMap placeholder.
#pragma once
#include "GameClient/AptCanvas.h"
#include "GameClient/LivingWorldMap.h"
#include "GameClient/LivingWorldTerritorySelection.h"
#include <godot_cpp/classes/sub_viewport.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/rect2.hpp>
#include <set>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/shader_material.hpp>
#include <vector>
class AptSkirmish;
class LivingWorldTerritorySelection;
namespace godot {
class RetailFileSystem;
class AptLivingWorldView {
public:
 ~AptLivingWorldView();
 void reset();
 void beginBuild(const std::set<std::string> *paths = nullptr);
 void draw(Node *owner, const Ref<RetailFileSystem> &fs, const LivingWorldMapStore &data, const LivingWorldCampaignManager &campaignManager,
           RID item, const Rect2 &rect, const AptCanvasOp &op, AptSkirmish *screen);
 bool mouseMove(const Vector2 &window, AptSkirmish *screen);
 bool mouseButton(bool down, bool left, AptSkirmish *screen);
 void endBuild();
 Dictionary report() const;
 Rect2 rect() const {return m_rect;}
 void drawOverlays(RID item, AptSkirmish *screen);
 Ref<ShaderMaterial> overlayMaterial() const {return m_overlayMaterial;}
private:
 struct RegionMesh { std::string name; MeshInstance3D *mesh=nullptr; Color tint; PackedVector3Array triangles; PackedVector3Array edges; };
 LivingWorldTerritorySelection m_snapshot;
 LivingWorldTerritorySelection *m_selection=nullptr;
 std::vector<RegionMesh> m_regions;
 Color m_borderColor;
 Color m_neutralColor;
 std::string m_hover;
 std::string m_campaign;
 std::string m_geometryKey;
 Ref<ShaderMaterial> m_overlayMaterial;
 bool m_dirty = true;
 bool m_pressed = false;
 void updateColours(AptSkirmish *screen);
 SubViewport *m_view = nullptr;
 ObjectID m_viewId;
 std::string m_path;
 bool m_used = false;
 bool m_active = false;
 int64_t m_updates = 0;
 bool m_failed = false;
 Rect2 m_rect;
 PackedStringArray m_errors;
 int64_t m_meshes = 0;
 int64_t m_oceanMeshes = 0;
};
}
