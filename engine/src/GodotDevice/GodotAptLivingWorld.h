// OpenBFME. GPL-3.0. Device presentation of the APT LivingWorldMap placeholder.
#pragma once
#include "GameClient/AptCanvas.h"
#include "GameClient/LivingWorldMap.h"
#include <godot_cpp/classes/sub_viewport.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/rect2.hpp>
#include <set>
namespace godot {
class RetailFileSystem;
class AptLivingWorldView {
public:
 ~AptLivingWorldView();
 void reset();
 void beginBuild(const std::set<std::string> *paths = nullptr);
 void draw(Node *owner, const Ref<RetailFileSystem> &fs, const LivingWorldMapStore &data,
           RID item, const Rect2 &rect, const AptCanvasOp &op);
 void endBuild();
 Dictionary report() const;
private:
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
};
}
