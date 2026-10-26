// OpenBFME. GPL-3.0. See docs/TERRITORY_MAP.md and stop S-5304.
#include "GodotDevice/GodotAptLivingWorld.h"
#include "GodotDevice/GodotRetailFileSystem.h"
#include "GodotDevice/GodotW3DInstancer.h"
#include "GodotDevice/GodotW3DModelBuilder.h"
#include "GameClient/GUI/AptScreens/AptSkirmish.h"
#include "GameClient/GUI/Skirmish/SkirmishGameSetup.h"
#include <godot_cpp/classes/array_mesh.hpp>
#include <godot_cpp/classes/shader.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/camera3d.hpp>
#include <godot_cpp/classes/environment.hpp>
#include <godot_cpp/classes/rendering_server.hpp>
#include <godot_cpp/classes/viewport_texture.hpp>
#include <godot_cpp/core/object.hpp>
#include <algorithm>
#include <cmath>
#include <map>
#include <array>
namespace godot {
AptLivingWorldView::~AptLivingWorldView() { reset(); }
void AptLivingWorldView::reset()
{
 if (m_viewId.is_valid() && ObjectDB::get_instance(m_viewId)) memdelete(m_view);
 m_selection=nullptr; m_snapshot=LivingWorldTerritorySelection(); m_geometryKey.clear(); m_regions.clear(); m_hover.clear(); m_campaign.clear(); m_overlayMaterial.unref(); m_dirty=true; m_pressed=false;
 m_view = nullptr; m_viewId = ObjectID(); m_path.clear(); m_errors.clear();
 m_meshes = 0; m_oceanMeshes=0; m_used = false; m_active = false; m_updates = 0; m_failed = false;
}
void AptLivingWorldView::beginBuild(const std::set<std::string> *paths)
{
 if (!paths || paths->count(m_path)) m_used = false;
}
void AptLivingWorldView::endBuild()
{
 if (m_view && !m_used) { m_view->set_update_mode(SubViewport::UPDATE_DISABLED); m_active = false; m_selection=nullptr; }
}
Dictionary AptLivingWorldView::report() const
{
 Dictionary r; r["visible"] = m_used && m_view && !m_failed;
 r["hover"] = String(m_hover.c_str()); r["region_meshes"] = (int64_t)m_regions.size();
 Array starts; if (!m_snapshot.regions().regions.empty()) for (const auto &name:m_snapshot.starts()) starts.push_back(String(name.c_str())); r["starts"]=starts;
 Array regions;
 if(m_view && m_used && !m_snapshot.regions().regions.empty()) {
  auto *camera=Object::cast_to<Camera3D>(m_view->get_node_or_null(NodePath("MapCamera")));
  if(camera) for(const auto &entry:m_regions) {
   Dictionary region;region["name"]=String(entry.name.c_str());region["holder"]=m_snapshot.holder(entry.name);region["allowed"]=m_snapshot.allowed(entry.name);
   PackedVector2Array points;
   for(int64_t i=0;i+2<entry.triangles.size();i+=std::max<int64_t>(3,(entry.triangles.size()/36)*3)){
    const Vector3 p=entry.mesh->get_global_transform().xform((entry.triangles[i]+entry.triangles[i+1]+entry.triangles[i+2])/3.0f);
    const Vector2 pixel=camera->unproject_position(p); points.push_back(m_rect.position+pixel/Vector2(m_view->get_size())*m_rect.size);
   }
   region["pick_points"]=points;
   for(const auto &r:m_snapshot.regions().regions)if(r.name==entry.name){region["label"]=String(r.displayName.c_str());region["description"]=String(r.displayDescription.c_str());region["map"]=String(r.mapName.c_str());}
   regions.push_back(region);
  }
 }
 r["regions"]=regions;
 r["render_updates"] = m_updates; r["meshes"] = m_meshes; r["ocean_meshes"]=m_oceanMeshes; r["errors"] = m_errors; r["rect"] = m_rect;
 r["unverified"] = "[S-5304] LivingWorldMap: bind-pose model, orthographic crop from Center/Extent; retail AptCenter/AptZoom projection, animated clouds, per-scene lighting and retail effect timing remain unverified; selections use lobby-local region names until strategic serialization is ported; explicit FX NormalMap=None is presented without bump mapping";
 return r;
}
void AptLivingWorldView::draw(Node *owner, const Ref<RetailFileSystem> &fs, const LivingWorldMapStore &data, const LivingWorldCampaignManager &campaignManager,
                             RID item, const Rect2 &rect, const AptCanvasOp &op, AptSkirmish *screen)
{
 m_used = true; m_path = op.path; m_rect = rect;
 if (rect.size.x <= 0 || rect.size.y <= 0 || m_failed) return;
 const auto &info = data.mapInfo();
 if (!screen || !screen->setup()) return;
 m_selection=&screen->territorySelection();
 const StrategicScenario *scenarioSource=nullptr;
 for (const auto &candidate:screen->setup()->source().strategicScenarios()) if (candidate.campaignIndex==screen->strategicScenario()) scenarioSource=&candidate;
 if (!scenarioSource) return;
 const auto &campaigns=campaignManager.campaigns();
 if (scenarioSource->campaignIndex<0 || scenarioSource->campaignIndex>=(int)campaigns.size()) return;
 const auto &campaign=campaigns[(size_t)scenarioSource->campaignIndex];
 if (!campaign.scenario) return;
 const auto *regions=data.findRegionCampaign(campaign.scenario->regionCampaign);
 if (!regions) { if(m_errors.is_empty())m_errors.push_back("Missing region campaign"); return; }
 const std::string geometryKey=info.mapObject+"|"+regions->name+"|"+regions->regionEffectsManagerName;
 if(m_view && geometryKey!=m_geometryKey){reset();m_used=true;m_path=op.path;m_rect=rect;m_selection=&screen->territorySelection();}
 m_geometryKey=geometryKey;
 std::array<LivingWorldTerritoryPlayer,8> players{};
 const auto &game=screen->setup()->info();
 for (int i=0;i<8 && i<game.slotCount();++i) {
  const auto &slot=game.slots[i]; auto &player=players[i]; player.occupied=slot.isOccupied() && slot.playerTemplate!=PLAYERTEMPLATE_OBSERVER;
  player.selectable=i==screen->setup()->localSlot() || (screen->territoryHostEditable() && slot.isAI());
  player.team=slot.teamNumber;
  const auto &factions=screen->setup()->source().factions();
  if (slot.playerTemplate>=0 && slot.playerTemplate<(int)factions.size()) player.faction=factions[(size_t)slot.playerTemplate].templateName;
  const auto &colors=screen->setup()->source().colors();
  if (slot.color>=0 && slot.color<(int)colors.size()) player.color=colors[(size_t)slot.color].rgb;
 }
 m_selection->configure(*regions,*campaign.scenario,campaign.name,players);m_snapshot=*m_selection;
 if (m_campaign!=campaign.name) {m_campaign=campaign.name;m_hover.clear();screen->hoverStrategicTerritory(nullptr);m_dirty=true;}

 if (!info.defined || info.mapObject.empty() || info.extent.x <= 0 || info.extent.y <= 0) {
  m_errors.push_back("LivingWorldMap: missing map object or invalid map extent"); m_failed = true; return;
 }
 const Vector2i size(std::clamp((int)std::ceil(rect.size.x),1,2048),std::clamp((int)std::ceil(rect.size.y),1,2048));
 bool redraw = !m_view || !m_active || m_view->get_size() != size;
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
  for(int64_t i=0;i<subs.size();++i) {
   Dictionary sub=subs[i]; const String name=sub.get("name",String());
   const String leaf=name.get_slice(".",name.contains(".")?1:0).to_lower();
   if(leaf==String(info.textLayerSubObject.c_str()).to_lower()) hidden.push_back(name);
  }
  // The LivingMap ocean is a WaterShader.FX surface, not normalmapped.fx.
  // Reuse the standing-water shader and its authored textures instead of the
  // generic FX approximation that rendered the ocean white.
  Ref<W3DModelBuilder> waterBuilder;waterBuilder.instantiate();
  Node3D *waterRoot=waterBuilder->build_model(fs,String(info.mapObject.c_str()));
  if(waterRoot) {
   m_view->add_child(waterRoot);
   const auto meshes=waterRoot->find_children("*","MeshInstance3D",true,false);
   for(int64_t i=0;i<meshes.size();++i) {
    auto *mesh=Object::cast_to<MeshInstance3D>(meshes[i]);
    if(!mesh->has_meta("w3d_standing_water")){mesh->hide();continue;}
    ++m_oceanMeshes;
    Dictionary water=mesh->get_meta("w3d_standing_water");
    Ref<ShaderMaterial> material;material.instantiate();
    Ref<Shader> shader=ResourceLoader::get_singleton()->load("res://shaders/water_standing.gdshader");material->set_shader(shader);
    for(const char *key:{"dimming","bump_scale","specular_exponent","bump_uv_scale","env_uv_scale","wave_uv_scale","wind_angle","wind_speed","wave_angle","wave_speed"}) material->set_shader_parameter(key,water[key]);
    material->set_shader_parameter("material_diffuse",water["diffuse"]);
    material->set_shader_parameter("blend_terrain_alpha",false);
    material->set_shader_parameter("light_color",Vector3(1,1,1));
    for(const auto &pair:std::vector<std::pair<const char *,const char *>>{{"bump_texture","bump_tex"},{"env_texture","sky_tex"},{"wave_texture","wave_tex"}}) {
     const String name=water[pair.first];if(name.is_empty() || name.nocasecmp_to("None")==0)continue;
     const auto texture=waterBuilder->load_texture(fs,name);if(texture.is_null())m_errors.push_back(String("Missing map ocean texture: ")+name);
     else material->set_shader_parameter(pair.second,texture);
    }
    mesh->set_material_override(material);mesh->show();hidden.push_back(mesh->get_meta("w3d_mesh",String()));
   }
  }
  if(!waterRoot)m_errors.push_back("Could not inspect authored map ocean geometry");
  if(!inst->set_instance_hidden_subobjects(instance,hidden)) m_errors.push_back("Could not hide authored shell map layers");
  const auto *effects=data.findRegionEffects(regions->regionEffectsManagerName);
  if (effects) {
   m_borderColor=Color(effects->regionBorderColor.red,effects->regionBorderColor.green,effects->regionBorderColor.blue,0.8);
   m_neutralColor=Color(effects->neutralRegionColor.red,effects->neutralRegionColor.green,effects->neutralRegionColor.blue,0.3);
   Ref<W3DModelBuilder> builder; builder.instantiate();
   Node3D *overlay=builder->build_model(fs,String(effects->regionObject.c_str()));
   if (overlay) {
    m_view->add_child(overlay);
    for (const auto &region:regions->regions) {
     for (int64_t i=0;i<overlay->find_children("*","MeshInstance3D",true,false).size();++i) {
      auto *mesh=Object::cast_to<MeshInstance3D>(overlay->find_children("*","MeshInstance3D",true,false)[i]);
      const String meshName=mesh->get_meta("w3d_mesh",String());
      if (meshName.get_slice(".",1).nocasecmp_to(String(region.subObject.c_str()))!=0) continue;
      RegionMesh entry; entry.name=region.name;entry.mesh=mesh;entry.tint=Color(0,0,0,0);mesh->hide();
      entry.triangles=mesh->get_mesh()->get_faces();
      std::map<std::array<float,6>,int> edges;
      for(int64_t t=0;t+2<entry.triangles.size();t+=3)for(int edge=0;edge<3;++edge){
       Vector3 a=entry.triangles[t+edge],b=entry.triangles[t+(edge+1)%3];
       if(std::array<float,3>{b.x,b.y,b.z}<std::array<float,3>{a.x,a.y,a.z})std::swap(a,b);
       ++edges[{a.x,a.y,a.z,b.x,b.y,b.z}];
      }
      for(const auto &edge:edges)if(edge.second==1){const auto &e=edge.first;entry.edges.push_back(Vector3(e[0],e[1],e[2]));entry.edges.push_back(Vector3(e[3],e[4],e[5]));}
      m_regions.push_back(entry);
     }
    }
    for (int64_t i=0;i<overlay->find_children("*","MeshInstance3D",true,false).size();++i) {
     auto *mesh=Object::cast_to<MeshInstance3D>(overlay->find_children("*","MeshInstance3D",true,false)[i]);
     bool matched=false;for (const auto &entry:m_regions) if(entry.mesh==mesh) matched=true;
     if(!matched) mesh->hide();
    }
   } else m_errors.push_back("Could not load region picking geometry");
  } else m_errors.push_back("Missing region effects");
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
 camera->set_size(info.extent.y*2.0f*0.78f);
 const Vector3 centre(info.center.x,0,-info.center.y);
 const float pitch = std::clamp(info.aptPitch,0.0f,1.4f);
 camera->set_position(centre+Vector3(0,10000.0f*std::cos(pitch),10000.0f*std::sin(pitch)));
 camera->look_at(centre,Vector3(0,0,-1));
 auto *inst = Object::cast_to<W3DInstancer>(m_view->get_node_or_null(NodePath("MapModel")));
 updateColours(screen);
 if (redraw) {
  m_dirty=false;
  if (inst) { inst->set_sort_camera(camera->get_global_transform()); inst->update_now(); }
  m_view->set_update_mode(SubViewport::UPDATE_ONCE); ++m_updates;
 }
 RenderingServer::get_singleton()->canvas_item_add_texture_rect(item,rect,m_view->get_texture()->get_rid(),false,Color(op.placeholderColor[0],op.placeholderColor[1],op.placeholderColor[2],op.placeholderColor[3]));
}
}

namespace godot {
void AptLivingWorldView::updateColours(AptSkirmish *screen)
{
 if (!m_selection) return;
 const auto hoverGroup=m_selection->group(m_hover);
 for (auto &entry:m_regions) {
  const int holder=m_selection->holder(entry.name);
  Color color(0,0,0,0);
  if (holder>=0) {const auto rgb=m_selection->players()[(size_t)holder].color;color=rgb>0xFFFFFF?m_neutralColor:Color(((rgb>>16)&255)/255.0f,((rgb>>8)&255)/255.0f,(rgb&255)/255.0f,0.38f);}
  if (std::find(hoverGroup.begin(),hoverGroup.end(),entry.name)!=hoverGroup.end() && m_selection->allowed(entry.name)) {
   color=holder>=0?color.lightened(0.12):m_neutralColor;color.a=holder>=0?0.55:0.22;
  }
  if (entry.tint!=color) {entry.tint=color;m_dirty=true;}
 }
}
bool AptLivingWorldView::mouseMove(const Vector2 &window,AptSkirmish *screen)
{
 if (!m_view || !m_used || !screen || !screen->territoryInputEnabled()) return false;
 std::string hit;
 if(m_rect.has_point(window)) {
  auto *camera=Object::cast_to<Camera3D>(m_view->get_node_or_null(NodePath("MapCamera")));
  const Vector2 pixel=(window-m_rect.position)/m_rect.size*Vector2(m_view->get_size());
  const Vector3 origin=camera->project_ray_origin(pixel), direction=camera->project_ray_normal(pixel);
  float nearest=1e30f;
  for(const auto &entry:m_regions) {
   const auto xf=entry.mesh->get_global_transform();
   for(int64_t i=0;i+2<entry.triangles.size();i+=3) {
    const Vector3 a=xf.xform(entry.triangles[i]),b=xf.xform(entry.triangles[i+1]),c=xf.xform(entry.triangles[i+2]);
    const Vector3 e1=b-a,e2=c-a,p=direction.cross(e2);const float det=e1.dot(p);
    if(std::abs(det)<0.00001f) continue;
    const float inv=1.0f/det;const Vector3 t=origin-a;const float u=t.dot(p)*inv;if(u<0 || u>1)continue;
    const Vector3 q=t.cross(e1);const float v=direction.dot(q)*inv;if(v<0 || u+v>1)continue;
    const float distance=e2.dot(q)*inv;if(distance>=0 && distance<nearest){nearest=distance;hit=entry.name;}
   }
  }
 }
 if(hit!=m_hover) {
  m_hover=hit;const LivingWorldRegion *region=nullptr;
  for(const auto &r:m_selection->regions().regions)if(r.name==hit)region=&r;
  screen->hoverStrategicTerritory(region);updateColours(screen);
  m_dirty=false;
 }
 return m_rect.has_point(window);
}
bool AptLivingWorldView::mouseButton(bool down,bool left,AptSkirmish *screen)
{
 if(!screen || !screen->territoryInputEnabled() || m_hover.empty() || !m_used){if(!down)m_pressed=false;return false;}
 if(down){m_pressed=true;return true;}
 if(!m_pressed)return false;m_pressed=false;
 if(m_selection->pick(m_hover,left)){m_snapshot=*m_selection;updateColours(screen);m_dirty=false;}
 return true;
}
}

namespace godot {
void AptLivingWorldView::drawOverlays(RID item, AptSkirmish *screen)
{
 if(!m_view || !screen || !screen->setup())return;
 m_used=true;m_selection=&screen->territorySelection();
 auto players=m_selection->players();
 const auto &game=screen->setup()->info();const auto &colors=screen->setup()->source().colors();
 for(int i=0;i<8;++i){
  if(i>=game.slotCount()){players[i].occupied=false;continue;}
  const auto &slot=game.slots[i];players[i].occupied=slot.isOccupied() && slot.playerTemplate!=PLAYERTEMPLATE_OBSERVER;
  players[i].team=slot.teamNumber;
  const auto &factions=screen->setup()->source().factions();players[i].faction.clear();
  if(slot.playerTemplate>=0 && slot.playerTemplate<(int)factions.size())players[i].faction=factions[(size_t)slot.playerTemplate].templateName;
  players[i].color=slot.color>=0 && slot.color<(int)colors.size()?colors[(size_t)slot.color].rgb:0x1000000;
 }
 m_selection->updatePlayers(players);m_snapshot=*m_selection;updateColours(screen);
 auto *camera=Object::cast_to<Camera3D>(m_view->get_node_or_null(NodePath("MapCamera")));
 if(!camera)return;
 if(m_overlayMaterial.is_null()) {
  Ref<Shader> shader;shader.instantiate();
  shader->set_code("shader_type canvas_item; render_mode unshaded; uniform vec4 clip_rect; varying vec2 pos;void vertex(){pos=VERTEX;}void fragment(){if(pos.x<clip_rect.x||pos.y<clip_rect.y||pos.x>clip_rect.z||pos.y>clip_rect.w)discard;}");
  m_overlayMaterial.instantiate();m_overlayMaterial->set_shader(shader);
 }
 m_overlayMaterial->set_shader_parameter("clip_rect",Vector4(m_rect.position.x,m_rect.position.y,(m_rect.position+m_rect.size).x,(m_rect.position+m_rect.size).y));
 auto *rs=RenderingServer::get_singleton();rs->canvas_item_set_material(item,m_overlayMaterial->get_rid());
 for(const auto &entry:m_regions) {
  const Color color=entry.tint;if(color.a==0)continue;
  PackedVector2Array points;PackedInt32Array indices;PackedColorArray colors;
  for(int64_t i=0;i<entry.triangles.size();++i){
   const auto p=camera->unproject_position(entry.mesh->get_global_transform().xform(entry.triangles[i]));
   points.push_back(m_rect.position+p/Vector2(m_view->get_size())*m_rect.size);indices.push_back((int32_t)i);colors.push_back(color);
  }
  rs->canvas_item_add_triangle_array(item,indices,points,colors,PackedVector2Array());
 }
 for(const auto &entry:m_regions) {
  Color border=m_borderColor;
  const int holder=m_snapshot.holder(entry.name);
  if(holder>=0){const auto rgb=m_snapshot.players()[(size_t)holder].color;border=rgb>0xFFFFFF?Color(m_neutralColor.r,m_neutralColor.g,m_neutralColor.b,1):Color(((rgb>>16)&255)/255.0f,((rgb>>8)&255)/255.0f,(rgb&255)/255.0f,1);}
  for(int64_t i=0;i+1<entry.edges.size();i+=2){
   const auto transform=entry.mesh->get_global_transform();
   const Vector2 a=camera->unproject_position(transform.xform(entry.edges[i])),b=camera->unproject_position(transform.xform(entry.edges[i+1]));
   rs->canvas_item_add_line(item,m_rect.position+a/Vector2(m_view->get_size())*m_rect.size,m_rect.position+b/Vector2(m_view->get_size())*m_rect.size,border,holder>=0?1.4:1.0,true);
  }
 }

}
}
