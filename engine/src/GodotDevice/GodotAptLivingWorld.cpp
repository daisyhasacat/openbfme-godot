// OpenBFME. GPL-3.0. See docs/TERRITORY_MAP.md and stop S-5304.
#include "GodotDevice/GodotAptLivingWorld.h"
#include "GodotDevice/GodotRetailFileSystem.h"
#include "GodotDevice/GodotW3DInstancer.h"
#include <godot_cpp/classes/camera3d.hpp>
#include <godot_cpp/classes/environment.hpp>
#include <godot_cpp/classes/rendering_server.hpp>
#include <godot_cpp/classes/viewport_texture.hpp>
#include <godot_cpp/core/object.hpp>
#include <algorithm>
#include <cmath>
namespace godot {
AptLivingWorldView::~AptLivingWorldView() { reset(); }
void AptLivingWorldView::reset()
{
 if (m_viewId.is_valid() && ObjectDB::get_instance(m_viewId)) memdelete(m_view);
 m_view = nullptr; m_viewId = ObjectID(); m_path.clear(); m_errors.clear();
 m_meshes = 0; m_used = false; m_active = false; m_updates = 0; m_failed = false;
}
void AptLivingWorldView::beginBuild(const std::set<std::string> *paths)
{
 if (!paths || paths->count(m_path)) m_used = false;
}
void AptLivingWorldView::endBuild()
{
 if (m_view && !m_used) { m_view->set_update_mode(SubViewport::UPDATE_DISABLED); m_active = false; }
}
Dictionary AptLivingWorldView::report() const
{
 Dictionary r; r["visible"] = m_used && m_view && !m_failed;
 r["render_updates"] = m_updates; r["meshes"] = m_meshes; r["errors"] = m_errors; r["rect"] = m_rect;
 r["unverified"] = "[S-5304] LivingWorldMap: bind-pose model, orthographic framing from Center/Extent; retail AptCenter/AptZoom projection, animated clouds, per-scene lighting and region effects are not ported; explicit FX NormalMap=None is presented without bump mapping";
 return r;
}
void AptLivingWorldView::draw(Node *owner, const Ref<RetailFileSystem> &fs, const LivingWorldMapStore &data,
                             RID item, const Rect2 &rect, const AptCanvasOp &op)
{
 m_used = true; m_path = op.path; m_rect = rect;
 if (rect.size.x <= 0 || rect.size.y <= 0 || m_failed) return;
 const auto &info = data.mapInfo();
 if (!info.defined || info.mapObject.empty() || info.extent.x <= 0 || info.extent.y <= 0) {
  m_errors.push_back("LivingWorldMap: missing map object or invalid map extent"); m_failed = true; return;
 }
 const Vector2i size(std::clamp((int)std::ceil(rect.size.x),1,2048),std::clamp((int)std::ceil(rect.size.y),1,2048));
 const bool redraw = !m_view || !m_active || m_view->get_size() != size;
 m_active = true;
 if (!m_view) {
  m_view = memnew(SubViewport); m_viewId = m_view->get_instance_id();
  m_view->set_update_mode(SubViewport::UPDATE_DISABLED);
  m_view->set_name("AptLivingWorldMap"); m_view->set_use_own_world_3d(true);
  m_view->set_size(size); owner->add_child(m_view);
  auto *inst = memnew(W3DInstancer); inst->set_name("MapModel");
  inst->set_auto_update(false); inst->set_playing(false); m_view->add_child(inst);
  const Dictionary setup = inst->setup(fs);
  if (!bool(setup.get("ok",false))) { m_errors.push_back("LivingWorldMap: W3D renderer setup failed"); m_failed=true; return; }
  const int64_t model = inst->add_model(String(info.mapObject.c_str()));
  const Dictionary report = inst->get_model_report(model);
  const Array errors = report.get("errors",Array());
  for (int64_t i=0; i<errors.size(); ++i) m_errors.push_back(errors[i]);
  const int64_t instance = model >= 0 ? inst->add_instance(model,Transform3D(),String(),0.0,1.0) : -1;
  if (instance < 0) { m_errors.push_back("LivingWorldMap: W3D map model did not load"); m_failed=true; return; }
  const Array subs = report.get("sub_objects",Array()); m_meshes = subs.size();
  PackedStringArray hidden;
  if (!info.cloudBorderSubObject.empty()) hidden.push_back(String(info.cloudBorderSubObject.c_str()));
  inst->set_instance_hidden_subobjects(instance,hidden);
  Ref<Environment> env; env.instantiate();
  env->set_background(Environment::BG_COLOR); env->set_bg_color(Color(0.025,0.035,0.02));
  env->set_tonemapper(Environment::TONE_MAPPER_LINEAR);
  auto *camera = memnew(Camera3D); camera->set_name("MapCamera");
  camera->set_environment(env); camera->set_projection(Camera3D::PROJECTION_ORTHOGONAL);
  camera->set_near(1.0); camera->set_far(30000.0); m_view->add_child(camera); camera->set_current(true);
 }
 auto *camera = Object::cast_to<Camera3D>(m_view->get_node_or_null(NodePath("MapCamera")));
 if (!camera) return;
 m_view->set_size(size);
 // INFERENCE S-5304: fit the authored map bounds; native zoom/projection is unverified.
 camera->set_keep_aspect_mode(Camera3D::KEEP_HEIGHT);
 camera->set_size(std::max(info.extent.y*2.0f,info.extent.x*2.0f*size.y/size.x)*1.05f);
 const Vector3 centre(info.center.x,0,-info.center.y);
 const float pitch = std::clamp(info.aptPitch,0.0f,1.4f);
 camera->set_position(centre+Vector3(0,10000.0f*std::cos(pitch),10000.0f*std::sin(pitch)));
 camera->look_at(centre,Vector3(0,0,-1));
 auto *inst = Object::cast_to<W3DInstancer>(m_view->get_node_or_null(NodePath("MapModel")));
 if (redraw) {
  if (inst) { inst->set_sort_camera(camera->get_global_transform()); inst->update_now(); }
  m_view->set_update_mode(SubViewport::UPDATE_ONCE); ++m_updates;
 }
 RenderingServer::get_singleton()->canvas_item_add_texture_rect(item,rect,m_view->get_texture()->get_rid(),false,Color(op.placeholderColor[0],op.placeholderColor[1],op.placeholderColor[2],op.placeholderColor[3]));
}
}
