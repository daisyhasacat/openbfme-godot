// OpenBFME. GPL-3.0.
// See GodotPlacementGhost.h (lane BUILD-5 r2).

#include "GodotDevice/GodotPlacementGhost.h"
#include "GameClient/CommandButtonHelp.h"
#include "GameClient/GUI/Image.h"
#include <godot_cpp/classes/viewport.hpp>
#include <cmath>

#include "Common/AsciiString.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameClient/Drawable.h"
#include "GameClient/DrawableManager.h"
#include "GameClient/InGameUI.h"
#include "GameClient/LiveGame.h"
#include "GameClient/PlacementGhost.h"
#include "GameEngineDevice/W3DDevice/GameClient/Drawable/Draw/W3DScriptedModelDraw.h"
#include "GameLogic/Map/CastleTemplates.h"
#include "GodotDevice/GodotRetailFileSystem.h"
#include "GodotDevice/GodotW3DInstancer.h"
#include "GodotDevice/GodotW3DModelBuilder.h"

#include <godot_cpp/classes/array_mesh.hpp>
#include <godot_cpp/classes/immediate_mesh.hpp>
#include "GameLogic/GameLogic.h"
#include "GameClient/FormationAssistant.h"
#include "GameLogic/Object/Contain/HordeContainRuntime.h"
#include "GameLogic/Object/Contain/HordeContainCore.h"
#include "GameLogic/Module/ConstructionModules.h"
#include "GameLogic/BuildPlacement.h"
#include "GameLogic/Object/Object.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "GameLogic/Module/TerrainResourceBehavior.h"
#include "GameLogic/System/TerrainResourceManager.h"
#include "GameLogic/Economy.h"
#include "Common/PlayerTemplate.h"
#include <array>
#include <map>
#include <limits>
#include <chrono>
#include <godot_cpp/classes/camera3d.hpp>
#include <godot_cpp/classes/viewport.hpp>
#include <cmath>
#include <godot_cpp/classes/decal.hpp>
#include <godot_cpp/classes/geometry_instance3d.hpp>
#include <godot_cpp/classes/multi_mesh_instance3d.hpp>
#include <godot_cpp/core/object.hpp>
#include <godot_cpp/variant/typed_array.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

namespace godot
{
namespace
{
String toGodotString(const std::string &s)
{
	return String::utf8(s.c_str());
}

// SAGE (x east, y north, z up) -> Godot (x, z, -y), the drawable's basis and scale (as GameWorld draws its drawables)
Transform3D toTransform(const float *basis, const Coord3D &pos, float scale)
{
	static const float P[3][3] = { { 1, 0, 0 }, { 0, 0, 1 }, { 0, -1, 0 } };
	float b[3][3];
	for (int i = 0; i < 3; ++i)
	{
		for (int j = 0; j < 3; ++j)
		{
			float v = 0.0f;
			for (int a = 0; a < 3; ++a)
			{
				for (int c = 0; c < 3; ++c)
				{
					v += P[i][a] * basis[a * 3 + c] * P[j][c];
				}
			}
			b[i][j] = v * scale;
		}
	}
	Basis basisG(Vector3(b[0][0], b[1][0], b[2][0]), Vector3(b[0][1], b[1][1], b[2][1]), Vector3(b[0][2], b[1][2], b[2][2]));
	return Transform3D(basisG, Vector3(pos.x, pos.z, -pos.y));
}

// a node the renderer made, by the id taken when it was made: freed only while it still exists (the world that holds it may have gone first)
void freeNode(uint64_t id)
{
	if (id == 0)
	{
		return;
	}
	if (Node *n = Object::cast_to<Node>(ObjectDB::get_instance(id)))
	{
		n->queue_free();
	}
}

Color houseColorOf(std::uint32_t argb)
{
	return Color(((argb >> 16) & 255) / 255.0f, ((argb >> 8) & 255) / 255.0f, (argb & 255) / 255.0f, 1.0f);
}
} // namespace

PlacementGhostRenderer::PlacementGhostRenderer(Node3D *parent, const Ref<RetailFileSystem> &fs)
	: m_parent(parent)
	, m_fs(fs)
{
	m_textureLoader.instantiate();
	if (m_fs.is_valid() && m_fs->archive_fs())
	{
		m_castles.reset(new CastleTemplateStore());
		m_castles->setLoader(CastleTemplateStore::fileSystemLoader(*m_fs->archive_fs()));
	}
	else
	{
		error("no mounted archives: the castle layouts cannot be read");
	}
}

PlacementGhostRenderer::~PlacementGhostRenderer()
{
	clear();
	freeNode(m_arrowId);
	m_arrow = nullptr;
}

void PlacementGhostRenderer::error(const std::string &e)
{
	for (const std::string &x : m_errors)
	{
		if (x == e)
		{
			return;
		}
	}
	m_errors.push_back(e);
	UtilityFunctions::push_error(toGodotString("[placement ghost] " + e));
}

void PlacementGhostRenderer::clear()
{
	freeNode(m_exitDirectionsId);
	m_exitDirectionsId = 0;
	m_exitDirections = nullptr;
	m_exitDirectionCount = 0;
	freeNode(m_formationId); m_formationId=0; m_formation=nullptr;
	freeNode(m_resourceDecalId);
	freeNode(m_resourceLabelId);
	m_resourceDecalId = m_resourceLabelId = 0;
	m_resourceDecal = nullptr;
	m_resourceLabel = nullptr;
	for (IconView &v : m_views)
	{
		freeNode(v.instId);
		freeNode(v.bibId);
		freeNode(v.shadowDecalId); // lane OWNERFIX-2
	}
	m_views.clear();
	freeNode(m_footprintId);
	m_footprintId = 0;
	m_footprint = nullptr;
	m_footprintCount = m_buildingAlignmentLines = 0;
	freeNode(m_wallAlignId);
	m_wallAlignId = 0;
	m_wallAlign = nullptr;
	m_wallAlignSpans = m_wallAlignSegments = 0;
 m_alignmentCache.clear();m_alignmentHub=INVALID_ID;m_alignmentFrame=~0u;
	freeNode(m_selectionCircleId);
	m_selectionCircleId = 0;
	m_selectionCircle = nullptr;
	freeNode(m_gridId);
	m_gridId = 0;
	m_grid = nullptr;
	m_gridSpacing = 0;
	for (auto &ring : m_existingResourceRings) freeNode(ring.nodeId);
	m_existingResourceRings.clear();
	m_existingResourceState.clear();
	freeNode(m_resourceDecalId);
	freeNode(m_resourceLabelId);
	m_resourceDecalId = m_resourceLabelId = 0;
	m_resourceDecal = nullptr;
	m_resourceLabel = nullptr;
}

void PlacementGhostRenderer::prewarm(LiveGame &game, const std::vector<std::string> &templates, unsigned source)
{
	InGameUI preview;
	for (const auto &name : templates)
	{
		preview.placeBuildAvailable(name, source);
		preview.setPlaceGhost(Coord3D{0, 0, 0}, 0, 0);
		sync(game, preview, 0, 0, true);
	}
	preview.placeBuildAvailable("", INVALID_ID);
	sync(game, preview, 0, 0, true);
	UtilityFunctions::print("PERF preloaded building previews: ", (int64_t)templates.size(), "; models=", (int64_t)m_modelLoads);
}

W3DInstancer *PlacementGhostRenderer::newInstancer(const char *name)
{
	W3DInstancer *inst = memnew(W3DInstancer);
	inst->set_name(name);
	m_parent->add_child(inst);
	const Dictionary s = inst->setup(m_fs);
	if (!(bool)s["ok"])
	{
		const Array e = s["errors"];
		for (int64_t i = 0; i < e.size(); ++i)
		{
			error(String(e[i]).utf8().get_data());
		}
	}
	inst->set_house_colors_enabled(true); // before the first add_model (W3DInstancer.h)
	inst->set_playing(false);             // the drawable's draw module poses it
	return inst;
}

void PlacementGhostRenderer::drawIcon(size_t i, PlacementIcons::Icon &icon, IconView &view)
{
	if (view.tt != icon.tt)
	{
		// The destination hub moves between slots as a wall span changes length.
		// Keep each slot's loaded models instead of rebuilding GPU resources every
		// time that slot alternates between a segment and the destination hub.
		for (const EntryView &entry : view.entries)
		{
			if (view.inst && entry.instance >= 0) view.inst->remove_instance(entry.instance);
		}
		view.entries.clear();
		view.tt = icon.tt;
	}
	Drawable &d = *icon.drawable;
	if (!view.inst)
	{
		view.inst = newInstancer(i == 0 ? "PlacementGhost" : "PlacementGhostPiece");
		view.inst->set_alpha_override(true); // the drawables' 0.45: ZH's translucent pass (W3DShaderKey::AlphaOverride)
		view.instId = view.inst->get_instance_id();
	}
	std::vector<DrawEntry> &entries = d.entries();
	view.entries.resize(entries.size());
	const Transform3D xf = toTransform(d.getBasis(), *d.getPosition(), d.getInstanceScale());
	int instances = 0;
	for (size_t k = 0; k < entries.size(); ++k)
	{
		DrawEntry &e = entries[k];
		EntryView &ev = view.entries[k];
		if (e.kind != W3D_DRAWKIND_MODEL || !e.draw)
		{
			continue;
		}
		W3DDrawPoseView f;
		e.draw->poseView(f);
		if (!f.model && f.modelName && !f.modelName->empty() && AsciiStringUtil::compareNoCase(*f.modelName, "None") != 0)
		{
			error(icon.tt->getName() + " " + e.className + ": the model " + *f.modelName + " does not load: the ghost does not draw it");
		}
		const std::string model = (f.model && !e.moduleHidden) ? AsciiStringUtil::lowered(*f.modelName) : std::string();
		if (model != ev.modelName)
		{
			if (ev.instance >= 0)
			{
				view.inst->remove_instance(ev.instance);
				ev.instance = -1;
			}
			ev.modelName = model;
			ev.hiddenModel = nullptr;
			ev.hidden.clear();
			if (!model.empty())
			{
				auto it = view.models.find(model);
				if (it == view.models.end()) ++m_modelLoads;
				int64_t mid = it == view.models.end() ? view.inst->add_model(toGodotString(*f.modelName)) : it->second;
				view.models[model] = mid;
				if (mid < 0)
				{
					error(icon.tt->getName() + ": the model " + *f.modelName + " does not build: " + String(Variant(view.inst->get_errors())).utf8().get_data());
				}
				else
				{
					ev.instance = view.inst->add_instance(mid, xf, String(), 0.0, 1.0);
					if (ev.instance >= 0 && d.getHouseColor() != 0)
					{
						view.inst->set_instance_house_color(ev.instance, houseColorOf(d.getHouseColor())); // RW 0x674504 (slot 0 only)
					}
				}
			}
		}
		if (ev.instance < 0)
		{
			continue;
		}
		++instances;
		view.inst->set_instance_transform(ev.instance, xf);
		view.inst->set_instance_pose_keyed(ev.instance, *f.clip0, f.clipKey0, f.frame0, *f.clip1, f.clipKey1, f.frame1, f.blendPercentage);
		if (f.model != ev.hiddenModel || f.hiddenGeneration != ev.hiddenGeneration)
		{
			ev.hiddenModel = f.model;
			ev.hiddenGeneration = f.hiddenGeneration;
			std::vector<std::string> names;
			if (f.model)
			{
				for (int h : *f.hiddenSubObjects)
				{
					names.push_back(f.model->SubObjects[(size_t)h].Name);
				}
			}
			if (names != ev.hidden)
			{
				PackedStringArray packed;
				for (const std::string &n : names)
				{
					packed.push_back(toGodotString(n));
				}
				if (!view.inst->set_instance_hidden_subobjects(ev.instance, packed))
				{
					error(icon.tt->getName() + ": the draw script hides a sub object the model lacks");
				}
				ev.hidden = std::move(names);
			}
		}
		view.inst->set_instance_opacity(ev.instance, d.drawOpacity()); // 0.45 (RW 0xC12408)
	}
	view.instances = instances;
	view.inst->update_now();
	// the tint (Drawable::colorTint RW 0x6757BB): IllegalBuildColor (1, 0, 0) or (0, 0, 0.5 / 1.7) for code 9
	Color tint(0, 0, 0, 0);
	if (icon.tint == PlacementIcons::TINT_ILLEGAL)
	{
		tint = Color(1, 0, 0, 1);
	}
	else if (icon.tint == PlacementIcons::TINT_CODE9)
	{
		tint = Color(0, 0, icon.tintBlue, 1);
	}
	else if (icon.tint == PlacementIcons::TINT_ALTERNATE)
	{
		tint = Color(.45f, .35f, 0, 1); // a warm gold added to the lighting, distinct from the red / blue refusals
	}
	applyTint(view, tint);
	updateBib(icon, view);
	updateShadowDecal(icon, view);
}

void PlacementGhostRenderer::updateShadowDecal(PlacementIcons::Icon &icon, IconView &view)
{
	// lane OWNERFIX-2: a model state's decal shadow (W3DModelDraw's Shadow = SHADOW_ALPHA_DECAL 0x20 / SHADOW_ADDITIVE_DECAL 0x40 / their _DYNAMIC forms 0x400 /
	// 0x800, bits of RW 0xD99EB0; ShadowTexture, ShadowSizeX / Y, ShadowOffsetX / Y; RW 0x4B92D3 parses them) is a projected decal under the drawable: the
	// retail data gives the well's / statue's BUILD_PLACEMENT_CURSOR state its area of effect (decal_hero_good, GONDOR_WELL_AOE_RADIUS_DECAL = 440).
	// INFERENCE (stop S-5032): drawn as the selection decals are (a Godot Decal on the terrain layer, S-2522) at the texture's own alpha, not faded by the
	// drawable's 0.45 opacity (the shadow's ShadowOpacity* fade is not run); only the placement ghost draws them (every other drawable's shadow is S-3304)
	const ModelShadowInfo *sh = nullptr;
	for (DrawEntry &e : icon.drawable->entries())
	{
		if (e.kind == W3D_DRAWKIND_MODEL && e.draw && !e.moduleHidden)
		{
			const ModelConditionInfo *info = e.draw->currentModelState();
			if (info && info->hasShadow && (info->shadow.type & 0xC60) && !info->shadow.texture.empty())
			{
				sh = &info->shadow;
				break;
			}
		}
	}
	Decal *decal = view.shadowDecalId ? Object::cast_to<Decal>(ObjectDB::get_instance(godot::ObjectID(view.shadowDecalId))) : nullptr;
	if (!sh)
	{
		if (decal)
		{
			decal->set_visible(false);
		}
		view.shadowTexture.clear();
		return;
	}
	const std::string file = sh->texture.find('.') == std::string::npos ? sh->texture + ".tga" : sh->texture;
	Ref<Texture2D> tex = texture(file);
	if (tex.is_null())
	{
		return; // texture() reported it
	}
	if (!decal)
	{
		decal = memnew(Decal);
		decal->set_name("PlacementShadowDecal");
		m_parent->add_child(decal);
		view.shadowDecalId = decal->get_instance_id();
	}
	if (view.shadowTexture != file)
	{
		decal->set_texture(Decal::TEXTURE_ALBEDO, tex);
		view.shadowTexture = file;
	}
	const Coord3D &p = *icon.drawable->getPosition();
	// SAGE (x, y, z up) -> Godot (x, z, -y); projected down over a box 200 units deep
	decal->set_position(Vector3(p.x + sh->offsetX, p.z, -(p.y + sh->offsetY)));
	decal->set_size(Vector3(sh->sizeX, 200.0f, sh->sizeY));
	decal->set_albedo_mix(1.0f);
	decal->set_cull_mask(1u << 19); // the terrain's layer (GodotMapTerrain)
	decal->set_upper_fade(0.0f);
	decal->set_lower_fade(0.0f);
	decal->set_visible(true);
	view.shadowSizeX = sh->sizeX;
	view.shadowSizeY = sh->sizeY;
}

void PlacementGhostRenderer::applyTint(IconView &view, const Color &tint)
{
	// Drawable::colorTint (RW 0x6757BB): the W3D shader's per node tint (w3dshadergen: added to the lights and the ambient, as ZH's W3DScene does); every draw
	// item node of the icon's own instancer takes it (a model added later takes it on the next frame)
	const Vector3 t = tint.a > 0.0f ? Vector3(tint.r, tint.g, tint.b) : Vector3();
	view.tint = tint;
	view.tintApplied = true;
	const TypedArray<Node> nodes = view.inst->find_children("*", "MultiMeshInstance3D", true, false);
	for (int64_t n = 0; n < nodes.size(); ++n)
	{
		if (GeometryInstance3D *g = Object::cast_to<GeometryInstance3D>(nodes[n]))
		{
			if (Variant(g->get_instance_shader_parameter("w3d_tint")) != Variant(t))
			{
				g->set_instance_shader_parameter("w3d_tint", t);
			}
		}
	}
}

Ref<Texture2D> PlacementGhostRenderer::texture(const std::string &name)
{
	auto it = m_textures.find(name);
	if (it != m_textures.end())
	{
		return it->second;
	}
	Ref<Texture2D> t = m_textureLoader->load_texture(m_fs, toGodotString(name));
	if (t.is_null())
	{
		error("the bib texture " + name + ": " + String(Variant(m_textureLoader->get_report())).utf8().get_data());
	}
	m_textures[name] = t;
	return t;
}

void PlacementGhostRenderer::updateBib(PlacementIcons::Icon &icon, IconView &view)
{
	// TerrainVisual vslot 0x30 (drawable, highlight, 0.0) / vslot 0x34 (remove), W3DTerrainVisual::addFactionBibDrawable RW 0x49249D
	const std::string tex = icon.bib == PlacementIcons::BIB_NONE ? std::string() : (icon.bib == PlacementIcons::BIB_NORMAL ? "TBBib.tga" : "TBRedBib.tga");
	if (tex.empty())
	{
		if (view.bib)
		{
			view.bib->set_visible(false);
		}
		view.bibTexture.clear();
		return;
	}
	const PlacementGhost::Bib bib = PlacementGhost::bibOf(*icon.tt);
	if (!bib.valid)
	{
		error(icon.tt->getName() + ": no bib: " + bib.error);
		return;
	}
	const Ref<Texture2D> t = texture(tex);
	if (t.is_null())
	{
		return;
	}
	if (view.bib && view.bibTemplate != icon.tt)
	{
		// Sol review r1 (community PR #37): the quad is the template's footprint (PlacementGhost::bibOf); a reused wall slot that now shows another
		// template (a hub, a segment) gets a new one
		freeNode(view.bibId);
		view.bib = nullptr;
		view.bibId = 0;
		view.bibTexture.clear();
	}
	if (!view.bib)
	{
		view.bibTemplate = icon.tt;
		Ref<ArrayMesh> mesh;
		mesh.instantiate();
		PackedVector3Array v;
		PackedVector2Array uv;
		PackedVector3Array n;
		// the corners in the drawable's frame, SAGE (x, y) -> Godot (x, -y); uv 0 (x0, y0) (0, 1), 1 (x1, y0) (1, 1), 2 (x1, y1) (1, 0), 3 (x0, y1) (0, 0)
		const Vector3 c[4] = { Vector3(bib.x0, 0.05f, -bib.y0), Vector3(bib.x1, 0.05f, -bib.y0), Vector3(bib.x1, 0.05f, -bib.y1), Vector3(bib.x0, 0.05f, -bib.y1) };
		const Vector2 u[4] = { Vector2(0, 1), Vector2(1, 1), Vector2(1, 0), Vector2(0, 0) };
		for (int k : { 0, 2, 1, 0, 3, 2 })
		{
			v.push_back(c[k]);
			uv.push_back(u[k]);
			n.push_back(Vector3(0, 1, 0));
		}
		Array arrays;
		arrays.resize(Mesh::ARRAY_MAX);
		arrays[Mesh::ARRAY_VERTEX] = v;
		arrays[Mesh::ARRAY_TEX_UV] = uv;
		arrays[Mesh::ARRAY_NORMAL] = n;
		mesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arrays);
		view.bib = memnew(MeshInstance3D);
		view.bib->set_name("PlacementBib");
		view.bib->set_mesh(mesh);
		view.bib->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
		Ref<StandardMaterial3D> m;
		m.instantiate();
		m->set_shading_mode(BaseMaterial3D::SHADING_MODE_UNSHADED);
		m->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA); // ZH W3DBibBuffer: SRCBLEND_SRC_ALPHA, DSTBLEND_ONE_MINUS_SRC_ALPHA, no depth write
		m->set_depth_draw_mode(BaseMaterial3D::DEPTH_DRAW_DISABLED);
		m->set_cull_mode(BaseMaterial3D::CULL_DISABLED);
		view.bib->set_material_override(m);
		m_parent->add_child(view.bib);
		view.bibId = view.bib->get_instance_id();
	}
	if (view.bibTexture != tex)
	{
		Ref<StandardMaterial3D> m = view.bib->get_material_override();
		m->set_texture(BaseMaterial3D::TEXTURE_ALBEDO, t);
		view.bibTexture = tex;
	}
	const Drawable &d = *icon.drawable;
	view.bib->set_transform(toTransform(d.getBasis(), *d.getPosition(), 1.0f));
	view.bib->set_visible(true);
}

// OpenBFME placement aid: static production exits, rendered from preview drawables only.
void PlacementGhostRenderer::updateExitDirections()
{
	m_exitDirectionCount = 0;
	if (m_exitDirections) m_exitDirections->hide();
	if (!m_icons) return;
	Ref<ImmediateMesh> mesh;
	mesh.instantiate();
	Ref<StandardMaterial3D> material;
	material.instantiate();
	material->set_shading_mode(BaseMaterial3D::SHADING_MODE_UNSHADED);
	material->set_flag(BaseMaterial3D::FLAG_DISABLE_DEPTH_TEST, true);
	material->set_cull_mode(BaseMaterial3D::CULL_DISABLED);
	material->set_albedo(Color(0.2f, 1.0f, 0.35f));
	bool begun = false;
	for (const auto &icon : m_icons->icons())
	{
		Coord3D create, rally;
		if (!icon.tt || !icon.drawable || !PlacementIcons::productionExit(*icon.tt, create, rally)) continue;
		const Drawable &drawable = *icon.drawable;
		const Transform3D transform = toTransform(drawable.getBasis(), *drawable.getPosition(), drawable.getInstanceScale());
		const Vector3 start = transform.xform(Vector3(create.x, create.z + 3.0f, -create.y));
		Vector3 direction(rally.x - create.x, 0, -(rally.y - create.y));
		if (direction.length_squared() == 0) continue; // no defined exit direction
		direction = transform.basis.xform(direction).normalized();
		const Vector3 tip = start + direction * 60.0f;
		const Vector3 side(-direction.z, 0, direction.x);
		const Vector3 neck = tip - direction * 15.0f;
		if (!begun) { mesh->surface_begin(Mesh::PRIMITIVE_TRIANGLES, material); begun = true; }
		for (const Vector3 &vertex : {start - side * 2, start + side * 2, neck + side * 2,
			start - side * 2, neck + side * 2, neck - side * 2,
			neck - side * 8, neck + side * 8, tip}) mesh->surface_add_vertex(vertex);
		++m_exitDirectionCount;
	}
	if (!begun) return;
	mesh->surface_end();
	if (!m_exitDirections)
	{
		m_exitDirections = memnew(MeshInstance3D);
		m_exitDirections->set_name("PlacementUnitExits");
		m_exitDirections->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
		m_parent->add_child(m_exitDirections);
		m_exitDirectionsId = m_exitDirections->get_instance_id();
	}
	m_exitDirections->set_mesh(mesh);
	m_exitDirections->show();
}

void PlacementGhostRenderer::updateArrow(const InGameUI &ui)
{
	// W3DInGameUI::drawPlaceAngle (RW 0x48EAD8): not anchored -> both out of the scene (RW 0x48EB53); anchored -> W_Arrow (the anchor below 5 pixels, the
	// arrow at or above: both W_Arrow, RW 0xBDDBB0) with the transform of slot 0's drawable (RW 0x48EC4E -> RW 0x6765B9)
	m_anchored = ui.isPlacementAnchored();
	const bool want = m_anchored && m_icons && !m_icons->icons().empty();
	if (!want)
	{
		if (m_arrow && m_arrowInstance >= 0)
		{
			m_arrow->remove_instance(m_arrowInstance);
			m_arrowInstance = -1;
			m_arrow->update_now();
		}
		m_arrowShown = false;
		return;
	}
	if (!m_arrow)
	{
		m_arrow = newInstancer("PlacementArrow");
		m_arrowId = m_arrow->get_instance_id();
	}
	if (m_arrowModel == -2)
	{
		m_arrowModel = m_arrow->add_model("W_Arrow");
		if (m_arrowModel < 0)
		{
			error("the placement arrow W_Arrow does not build: " + std::string(String(Variant(m_arrow->get_errors())).utf8().get_data()));
		}
	}
	if (m_arrowModel < 0)
	{
		m_arrowShown = false;
		return;
	}
	const Drawable &d = *m_icons->icons()[0].drawable;
	const Transform3D xf = toTransform(d.getBasis(), *d.getPosition(), d.getInstanceScale());
	if (m_arrowInstance < 0)
	{
		m_arrowInstance = m_arrow->add_instance(m_arrowModel, xf, String(), 0.0, 1.0);
	}
	else
	{
		m_arrow->set_instance_transform(m_arrowInstance, xf);
	}
	m_arrow->update_now();
	m_arrowShown = m_arrowInstance >= 0;
}

void PlacementGhostRenderer::sync(LiveGame &game, const InGameUI &ui, double deltaSeconds, unsigned ghostOptions, bool judge)
{
	const auto started = std::chrono::steady_clock::now();
	const unsigned loadsBefore = m_modelLoads;
	if ((&game != m_game || !m_icons) && !judge)
	{
		return; // the drawables are made on an idle frame
	}
	if (&game != m_game || !m_icons)
	{
		clear();
		m_game = &game;
		m_icons.reset(new PlacementIcons(game.drawables().services(), game.logic()));
		m_icons->setCastleStore(m_castles.get());
	}
	m_icons->update(ui, deltaSeconds * 1000.0, ghostOptions, judge);
	for (const std::string &e : m_icons->errors())
	{
		error(e);
	}
	// drawIcon replaces instances when their template changes, while retaining
	// model/material caches. Keep them when placement closes too: reopening a
	// building must not reread its textures and recreate its GPU resources.
	if (m_icons->icons().size() > m_views.size())
	{
		m_views.resize(m_icons->icons().size());
	}
	if (ui.isPlacing() && ui.placeHasGhost())
	{
		for (size_t i = 0; i < m_icons->icons().size(); ++i)
		{
			drawIcon(i, m_icons->icons()[i], m_views[i]);
		}
	}
	else
	{
		for (IconView &v : m_views)
		{
			if (v.inst)
			{
				v.inst->set_visible(false);
			}
		}
	}
	for (size_t i = 0; i < m_views.size(); ++i)
	{
		IconView &v = m_views[i];
		const bool visible = ui.isPlacing() && ui.placeHasGhost() && i < m_icons->icons().size();
		if (v.inst)
		{
			v.inst->set_visible(visible);
		}
		if (!visible && v.bib)
		{
			v.bib->set_visible(false);
		}
	}
	updateArrow(ui);
	updateExitDirections();
	updateResources(ui, judge);
	updateGrid(game, ui, judge);
	updateFootprints(game, ui, judge);
	updateWallAlignment(game, ui, judge);
	updateSelectionCircle(game, ui, judge);
	updateFormation(game, ui, judge);
	m_formationSlots = ui.formationSlots;
	const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count();
	if (millis >= 100)
		UtilityFunctions::print("PERF placement preview: ", toGodotString(m_icons->templateName()), " ", (int64_t)millis, " ms; new models=", (int64_t)(m_modelLoads - loadsBefore));
}


void PlacementGhostRenderer::updateSelectionCircle(LiveGame &game, const InGameUI &ui, bool idle)
{
	if (ui.controllerAreaRadius <= 0)
	{
		if (m_selectionCircle) m_selectionCircle->hide();
		return;
	}
	if (!idle) return;
	if (!m_selectionCircle)
	{
		m_selectionCircle = memnew(MeshInstance3D);
		m_parent->add_child(m_selectionCircle);
		m_selectionCircleId = m_selectionCircle->get_instance_id();
		m_selectionCircle->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
	}
	Ref<StandardMaterial3D> material;
	material.instantiate();
	material->set_shading_mode(BaseMaterial3D::SHADING_MODE_UNSHADED);
	material->set_albedo(Color(1,.8f,.2f));
	Ref<ImmediateMesh> mesh;
	mesh.instantiate();
	mesh->surface_begin(Mesh::PRIMITIVE_LINE_STRIP,material);
	for (int i=0;i<=128;++i)
	{
		const float angle = i * 6.28318530718f / 128;
		const float x = ui.controllerAreaCenter.x + std::cos(angle)*ui.controllerAreaRadius;
		const float y = ui.controllerAreaCenter.y + std::sin(angle)*ui.controllerAreaRadius;
		mesh->surface_add_vertex(Vector3(x,game.logic().getGroundHeight(x,y)+1,-y));
	}
	mesh->surface_end();
	m_selectionCircle->set_mesh(mesh);
	m_selectionCircle->show();
}

namespace
{
// the outline of a structure template's foundation rectangle (the box around its active collision shapes) laid on the ground, `width` units each side of
// the edge; begins the mesh's surface on its first use. False when the template has no footprint there.
bool outlineFootprint(GameLogic &logic, ImmediateMesh &mesh, const Ref<StandardMaterial3D> &material, bool &begun, const ThingTemplate &tt, const Coord3D &p,
	float angle, float width = 1.0f, std::vector<Vector3> *vertices = nullptr)
{
	std::vector<BuildPlacement::Footprint> shapes;
	if (!BuildPlacement::footprintsOf(logic,tt,p,angle,shapes) || shapes.empty()) return false;
	const float c=std::cos(angle),s=std::sin(angle);
	float loX=std::numeric_limits<float>::infinity(),loY=loX,hiX=-loX,hiY=-loX;
	// A rotated foundation rectangle bounds the active collision shapes, including offsets.
	for (const auto &shape : shapes)
		for (float dx : {-shape.major,shape.major})
			for (float dy : {-(shape.type==0?shape.minor:shape.major),shape.type==0?shape.minor:shape.major})
			{
				const float a=shape.type==0?shape.angle:angle;
				const float x=shape.x+dx*std::cos(a)-dy*std::sin(a)-p.x;
				const float y=shape.y+dx*std::sin(a)+dy*std::cos(a)-p.y;
				const float lx=x*c+y*s,ly=-x*s+y*c;
				loX=std::min(loX,lx);hiX=std::max(hiX,lx);loY=std::min(loY,ly);hiY=std::max(hiY,ly);
			}
	if (!std::isfinite(loX) || hiX<=loX || hiY<=loY) return false;
	if (!begun) { if (!vertices) mesh.surface_begin(Mesh::PRIMITIVE_TRIANGLES,material); begun=true; }
	auto point = [&](float x,float y) {
		const float wx=p.x+x*c-y*s,wy=p.y+x*s+y*c;
		return Vector3(wx,logic.getGroundHeight(wx,wy)+1.5f,-wy);
	};
	const std::array<Vector2,4> corners={Vector2(loX,loY),Vector2(hiX,loY),Vector2(hiX,hiY),Vector2(loX,hiY)};
	for (int edge=0;edge<4;++edge)
	{
		const Vector2 a=corners[edge],b=corners[(edge+1)%4];
		const Vector2 normal=Vector2(-(b-a).y,(b-a).x).normalized()*width;
		for (int j=0;j<16;++j)
		{
			const Vector2 u=a+(b-a)*(j/16.0f),v=a+(b-a)*((j+1)/16.0f);
			const Vector2 uv[6]={u-normal,v-normal,v+normal,u-normal,v+normal,u+normal};
			for (const auto &vertex:uv)
    if (vertices) vertices->push_back(point(vertex.x,vertex.y));
    else mesh.surface_add_vertex(point(vertex.x,vertex.y));
		}
	}
	return true;
}
} // namespace

void PlacementGhostRenderer::updateFootprints(LiveGame &game, const InGameUI &ui, bool idle)
{
	if (!ui.isPlacing() || !ui.placeHasGhost() || !ui.placementSnap)
	{
		if (m_footprint) m_footprint->hide();
		m_structureFootprintCount = m_buildingAlignmentLines = 0;
		return;
	}
	if (!idle) return; // footprint geometry reads the live movement cache
	Ref<ImmediateMesh> mesh;
	mesh.instantiate();
	Ref<StandardMaterial3D> material;
	material.instantiate();
	material->set_shading_mode(BaseMaterial3D::SHADING_MODE_UNSHADED);
	material->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA);
	material->set_cull_mode(BaseMaterial3D::CULL_DISABLED);
	material->set_albedo(Color(1,.8f,.15f,.9f));
	bool begun = false;
	m_footprintCount = 0;
	// the snap square of a structure template at a position: the ghost's entries, and (OpenBFME aid) every built structure near the cursor
	m_structureFootprintCount = 0;
	auto addFootprint = [&](const ThingTemplate &tt, const Coord3D &p, float angle, unsigned &count) {
		if (outlineFootprint(game.logic(), **mesh, material, begun, tt, p, angle)) ++count;
	};
	for (const auto &icon : m_icons->icons())
	{
		if (icon.drawable && icon.tt) addFootprint(*icon.tt,*icon.drawable->getPosition(),icon.angle,m_footprintCount);
	}
	// the built structures within reach of the cursor (a bounded radius keeps the mesh small on a large base)
	const Coord3D &cursor = ui.placeLocation();
	const float reach = 1500.0f;
    m_buildingAlignmentLines = 0;
    std::array<const ::Object *,8> aligned{};
    std::array<float,8> distances;distances.fill(reach);
    const float gc=std::cos(ui.placementGridAngle), gs=std::sin(ui.placementGridAngle);
    const Player *local=game.logic().players().getLocalPlayer();
	for (::Object *o = game.logic().getFirstObject(); o; o = o->getNextObject())
	{
		if (!o->isInWorld() || o->isEffectivelyDead() || o->getContainedBy() || !o->getTemplate() || !o->isKindOfName("STRUCTURE")) continue;
		// a player's buildings only: the map's neutral and creep pieces (rocks, lairs) are STRUCTUREs owned by players of no playable side
		const Player *owner = o->getControllingPlayer();
		if (!owner || !owner->getPlayerTemplate() || !owner->getPlayerTemplate()->m_playableSide) continue;
		const Coord3D &p = *o->getPosition();
		if (std::abs(p.x-cursor.x) > reach || std::abs(p.y-cursor.y) > reach) continue;
		addFootprint(*o->getTemplate(),p,o->getOrientation(),m_structureFootprintCount);
        if (owner != local || o->getTemplate()->getFinalOverride()->getName()!=ui.placeBuildTemplate()) continue;
        const float dx=p.x-cursor.x, dy=p.y-cursor.y;
        const float u=dx*gc+dy*gs, v=-dx*gs+dy*gc;
        const int direction=std::abs(v)<=1.0f && std::abs(u)>1.0f ? (u>0?0:1) :
            (std::abs(u)<=1.0f && std::abs(v)>1.0f ? (v>0?2:3) :
            (std::abs(u)>1.0f && std::abs(u-v)<=1.41421356f ? (u>0?4:5) :
            (std::abs(u)>1.0f && std::abs(u+v)<=1.41421356f ? (u>0?6:7) : -1)));
        const float distance=std::sqrt(dx*dx+dy*dy);
        if (direction>=0 && distance<distances[direction]) {aligned[direction]=o;distances[direction]=distance;}

	}
	if (begun) mesh->surface_end();
    Ref<StandardMaterial3D> green;green.instantiate();
    green->set_shading_mode(BaseMaterial3D::SHADING_MODE_UNSHADED);
    green->set_cull_mode(BaseMaterial3D::CULL_DISABLED);
    green->set_albedo(Color(.1f,1.0f,.2f));
    bool lines=false;
    for (const auto *o:aligned) if(o) {
        if (!lines) {mesh->surface_begin(Mesh::PRIMITIVE_TRIANGLES,green);lines=true;}
        ++m_buildingAlignmentLines;
        const auto &end=*o->getPosition();
        const float dx=end.x-cursor.x,dy=end.y-cursor.y,length=std::sqrt(dx*dx+dy*dy);
        const float nx=-dy/length*1.5f,ny=dx/length*1.5f;
        const int steps=std::max(1,int(std::ceil(length/20.0f)));
        auto at=[&](int step,float side) {
            const float t=float(step)/steps,x=cursor.x+dx*t+nx*side,y=cursor.y+dy*t+ny*side;
            return Vector3(x,game.logic().getGroundHeight(x,y)+2.0f,-y);
        };
        for(int i=0;i<steps;++i) for(const auto &v: {at(i,-1),at(i,1),at(i+1,1),at(i,-1),at(i+1,1),at(i+1,-1)}) mesh->surface_add_vertex(v);
    }
    if(lines) {mesh->surface_end();begun=true;}
	if (!m_footprint && begun)
	{
		m_footprint=memnew(MeshInstance3D);
		m_parent->add_child(m_footprint);
		m_footprintId=m_footprint->get_instance_id();
		m_footprint->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
	}
	if (m_footprint) { m_footprint->set_mesh(mesh); m_footprint->set_visible(begun); }
}

// OpenBFME placement aid: while a wall span is dragged from a hub, the hub's existing spans that run exactly square to it (90 degrees either way) or exactly
// opposite it are outlined. A span is the hub's own segments in one direction (their producer is the hub, the direction from the hub to the segment), or a
// span another hub built that ends at this one (the direction from this hub to that hub). "Exactly" is within half a degree.
void PlacementGhostRenderer::updateWallAlignment(LiveGame &game, const InGameUI &ui, bool idle)
{
	auto hide = [&] {
		if (m_wallAlign) m_wallAlign->hide();
		m_wallAlignSpans = m_wallAlignSegments = 0;
	};
	if (!ui.isPlacing() || !ui.placeHasGhost() || !m_icons->wall())
	{
		hide();
		return;
	}
 constexpr float kPi=3.14159265358979f,kTolerance=0.5f*kPi/180.0f;
 // Snapshot geometry only in a logic client window. Pointer-angle matching
 // uses that snapshot on every rendered frame, including threaded live games.
 if (idle && (m_alignmentHub != ui.placeBuildSource() || m_alignmentFrame != game.logic().getFrame()))
 {
	GameLogic &logic = game.logic();
	const ::Object *hub = logic.findObjectByID(ui.placeBuildSource());
	const Coord3D &cursor = ui.placeLocation();
	if (!hub || !std::isfinite(cursor.x) || !std::isfinite(cursor.y))
	{
		hide();
		return;
	}
	const Coord3D &h = *hub->getPosition();
	// the hub's spans, keyed by their direction rounded to a tenth of a degree: the segments of each, and its exact direction (the mean of its members')
	struct Span { float sx = 0, sy = 0; std::vector<const ::Object *> segments; };
	std::map<int, Span> spans;
 const auto *linked=dynamic_cast<const GettingBuiltBehavior *>(hub->findModule("GettingBuiltBehavior"));
	for (const ::Object *o = logic.getFirstObject(); o; o = o->getNextObject())
	{
		if (!o->isInWorld() || o->isEffectivelyDead() || !o->getTemplate() || o->getControllingPlayer() != hub->getControllingPlayer()) continue;
		if (!(o->isKindOfName("WALL_SEGMENT") || (o->isKindOfName("DEFENSIVE_WALL") && !o->isKindOfName("WALL_HUB")))) continue;
		const Coord3D &p = *o->getPosition();
		float dx = 0, dy = 0;
		// the span's hub is the segment's producer (Construction::buildObjectNow; the builder link is released when it completes)
		if (o->getProducerID() == hub->getID() || (linked && std::any_of(linked->linkedPieces().begin(), linked->linkedPieces().end(), [&](const auto &piece) { return piece.id == o->getID(); })))
		{
			dx = p.x - h.x;
			dy = p.y - h.y;
		}
		else if (const ::Object *builder = logic.findObjectByID(o->getProducerID()))
		{
			// another hub's span that ends here: the segment lies on the line from its builder to this hub, between the two
			const Coord3D &b = *builder->getPosition();
			const float lx = h.x - b.x, ly = h.y - b.y, len = std::sqrt(lx * lx + ly * ly);
			if (len < 1.0f) continue;
			const float along = ((p.x - b.x) * lx + (p.y - b.y) * ly) / len, off = std::abs((p.x - b.x) * ly - (p.y - b.y) * lx) / len;
			if (off > 10.0f || along < 0.0f || along > len) continue;
			dx = b.x - h.x;
			dy = b.y - h.y;
		}
		else
		{
			continue;
		}
		const float d = std::sqrt(dx * dx + dy * dy);
		if (d < 1.0f) continue;
		Span &span = spans[(int)std::lround(std::atan2(dy, dx) * 1800.0f / kPi)];
		span.sx += dx / d;
		span.sy += dy / d;
		span.segments.push_back(o);
	}
  m_alignmentCache.clear();
  m_alignmentHub=hub->getID();m_alignmentOrigin=h;m_alignmentFrame=logic.getFrame();
  Ref<ImmediateMesh> scratch;scratch.instantiate();
  Ref<StandardMaterial3D> unused;
  for (const auto &entry:spans)
  {
   AlignedWallSpan cached;cached.angle=std::atan2(entry.second.sy,entry.second.sx);
   bool begun=false;
   for (const auto *o:entry.second.segments)
    if (outlineFootprint(logic,**scratch,unused,begun,*o->getTemplate(),*o->getPosition(),o->getOrientation(),2.0f,&cached.vertices)) ++cached.segments;
   m_alignmentCache.push_back(std::move(cached));
  }
 }
 if (m_alignmentHub != ui.placeBuildSource()) { hide();return; }
 const auto &cursor=ui.placeLocation();
 const float cx=cursor.x-m_alignmentOrigin.x,cy=cursor.y-m_alignmentOrigin.y;
 if (!std::isfinite(cx) || !std::isfinite(cy) || cx*cx+cy*cy<1.0f) { hide();return; }
 const float current=std::atan2(cy,cx);
	Ref<ImmediateMesh> mesh;
	mesh.instantiate();
	Ref<StandardMaterial3D> material;
	material.instantiate();
	material->set_shading_mode(BaseMaterial3D::SHADING_MODE_UNSHADED);
	material->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA);
	material->set_cull_mode(BaseMaterial3D::CULL_DISABLED);
 material->set_flag(BaseMaterial3D::FLAG_DISABLE_DEPTH_TEST,true);
	material->set_albedo(Color(.25f, 1, .55f, .95f));
	bool begun = false;
	m_wallAlignSpans = m_wallAlignSegments = 0;
 for (const auto &span:m_alignmentCache)
 {
  const float diff=std::abs(std::remainder(span.angle-current,2.0f*kPi));
  const bool parallel=diff<=kTolerance || std::abs(diff-kPi)<=kTolerance;
  const bool square=std::abs(diff-kPi*.5f)<=kTolerance;
  if ((!parallel && !square) || span.vertices.empty()) continue;
  ++m_wallAlignSpans;m_wallAlignSegments+=span.segments;
  if (!begun) { mesh->surface_begin(Mesh::PRIMITIVE_TRIANGLES,material);begun=true; }
  for (const auto &vertex:span.vertices) mesh->surface_add_vertex(vertex);
 }
	if (begun) mesh->surface_end();
	if (!m_wallAlign && begun)
	{
		m_wallAlign = memnew(MeshInstance3D);
		m_parent->add_child(m_wallAlign);
		m_wallAlignId = m_wallAlign->get_instance_id();
		m_wallAlign->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
	}
	if (m_wallAlign) { m_wallAlign->set_mesh(mesh); m_wallAlign->set_visible(begun); }
}

void PlacementGhostRenderer::updateGrid(LiveGame &game, const InGameUI &ui, bool idle)
{
	const bool show = ui.isPlacing() && ui.placeHasGhost() && ui.placementSnap;
	if (!show)
	{
		if (m_grid) m_grid->hide();
		return;
	}
	const Coord3D &p = ui.placeLocation();
	if (!std::isfinite(p.x) || !std::isfinite(p.y)) return;
	const float spacing = ui.placementGrid;
	const float rot = ui.placementGridAngle;
	const float gc = std::cos(rot), gs = std::sin(rot);
	// the cursor in the grid's own axes
	const int cx = (int)std::round((p.x * gc + p.y * gs) / spacing), cy = (int)std::round((-p.x * gs + p.y * gc) / spacing);
	if (idle && (!m_grid || cx != m_gridX || cy != m_gridY || spacing != m_gridSpacing || rot != m_gridAngle))
	{
		if (!m_grid)
		{
			m_grid = memnew(MeshInstance3D);
			m_parent->add_child(m_grid);
			m_gridId = m_grid->get_instance_id();
			m_grid->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
		}
		Ref<StandardMaterial3D> material;
		material.instantiate();
		material->set_shading_mode(BaseMaterial3D::SHADING_MODE_UNSHADED);
		material->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA);
		material->set_albedo(Color(.8f, .85f, 1, .55f));
		Ref<ImmediateMesh> mesh;
		mesh.instantiate();
		mesh->surface_begin(Mesh::PRIMITIVE_LINES, material);
		auto vertex = [&](float u, float v) {
			const float x = u * gc - v * gs, y = u * gs + v * gc;
			mesh->surface_add_vertex(Vector3(x, game.logic().getGroundHeight(x,y) + 1.0f, -y));
		};
		// Bounded local portion of a global map grid. Subdivide lines to follow terrain.
		for (int i = -10; i <= 10; ++i)
			for (int j = -40; j < 40; ++j)
			{
				vertex((cx+i)*spacing, (cy+j*.25f)*spacing);
				vertex((cx+i)*spacing, (cy+(j+1)*.25f)*spacing);
				vertex((cx+j*.25f)*spacing, (cy+i)*spacing);
				vertex((cx+(j+1)*.25f)*spacing, (cy+i)*spacing);
			}
		mesh->surface_end();
		m_grid->set_mesh(mesh);
		m_gridX = cx; m_gridY = cy; m_gridSpacing = spacing; m_gridAngle = rot;
	}
	if (m_grid) m_grid->show();
}

void PlacementGhostRenderer::updateResources(const InGameUI &ui, bool idle)
{
	const bool show = ui.isPlacing() && ui.placeHasGhost() && m_icons->resourceRadius() > 0;
	if (!show)
	{
		for (auto &ring : m_existingResourceRings) ring.decal->hide();
		m_existingResourceState.clear();
		if (m_resourceDecal) m_resourceDecal->hide();
		if (m_resourceLabel) m_resourceLabel->hide();
		return;
	}
 // Read authoritative claimant data only while the logic worker is idle.
 // Reuse decal nodes between frames, just as the placement reticle is reused.
 if (idle && m_game)
 {
  auto &logic = m_game->logic();
  const auto *source = logic.findObjectByID(ui.placeBuildSource());
  const Player *owner = source ? source->getControllingPlayer() : logic.players().getLocalPlayer();
  size_t count = 0;
  m_existingResourceState.clear();
  for (const auto &claim : logic.economy().resources().claimants())
  {
   const auto *object = logic.findObjectByID(claim.id);
   if (!claim.visible || claim.radius <= 0 || !object || object->isEffectivelyDead() ||
       !object->isInWorld() || object->getControllingPlayer() != owner) continue;
   const auto *resource = dynamic_cast<const TerrainResourceBehavior *>(object->findModule("TerrainResourceBehavior"));
   if (!resource || resource->data()->m_maxIncome <= 0) continue;
   if (count == m_existingResourceRings.size())
   {
    ResourceRing ring;
    ring.decal = memnew(Decal);
    m_parent->add_child(ring.decal);
    ring.nodeId = ring.decal->get_instance_id();
    ring.decal->set_texture(Decal::TEXTURE_ALBEDO, texture("BuildingRadialButtons_003.tga"));
    ring.decal->set_cull_mask(1u << 19);
    ring.decal->set_albedo_mix(1);
    ring.decal->set_upper_fade(0);
    ring.decal->set_lower_fade(0);
    m_existingResourceRings.push_back(ring);
   }
   auto *decal = m_existingResourceRings[count++].decal;
   const float share = resource->claimShare();
   const Color color = share > .75f ? Color(0,1,0) : share >= .5f ? Color(1,1,0) : share >= .25f ? Color(1,0,0) : Color(0,0,0);
   const auto &position = *object->getPosition();
   decal->set_position(Vector3(position.x,position.z,-position.y));
   decal->set_size(Vector3(2*claim.radius,200,2*claim.radius));
   decal->set_modulate(Color(color.r,color.g,color.b,128.0f/255));
   decal->show();
   Dictionary state;
   state["object"] = (int64_t)claim.id;
   state["radius"] = claim.radius;
   state["share"] = share;
   state["color"] = color;
   m_existingResourceState.push_back(state);
  }
  for (size_t i=count;i<m_existingResourceRings.size();++i) m_existingResourceRings[i].decal->hide();
 }
	// The active placement decal and text come from the HUD TerrainResourceFeedback.

}

Dictionary PlacementGhostRenderer::state() const
{
	Dictionary s;
	int cachedModels = 0;
	for (const IconView &view : m_views) cachedModels += (int)view.models.size();
	s["cached_models"] = cachedModels;
	s["model_loads"] = m_modelLoads;
	Array formationTextures;for(const auto &name:m_formationTextures)formationTextures.push_back(toGodotString(name));
	s["formation_textures"]=formationTextures;
	s["formation_visible"] = m_formation && m_formation->is_visible();
	Array formation;
	for(const auto &slot:m_formationSlots) {
		Dictionary item;item["id"]=(int64_t)slot.object;item["x"]=slot.position.x;item["y"]=slot.position.y;item["angle"]=slot.angle;
		formation.push_back(item);
	}
	s["formation_slots"]=formation;
	s["template"] = m_icons ? toGodotString(m_icons->templateName()) : String();
	s["selection_circle_visible"] = m_selectionCircle && m_selectionCircle->is_visible();
	s["footprint_visible"] = m_footprint && m_footprint->is_visible();
	s["footprint_count"] = m_footprintCount;
	s["structure_footprint_count"] = m_structureFootprintCount;
	s["building_alignment_lines"] = m_buildingAlignmentLines;
	s["wall_aligned_spans"] = m_wallAlignSpans;
	s["wall_aligned_segments"] = m_wallAlignSegments;
	s["grid_visible"] = m_grid && m_grid->is_visible();
	s["grid_spacing"] = m_gridSpacing;
	s["grid_angle"] = m_gridAngle;
	s["existing_resource_rings"] = m_existingResourceState;
	s["resource_radius"] = m_icons ? m_icons->resourceRadius() : 0;
	s["resource_share"] = m_icons ? m_icons->resourceShare() : 0;
	s["resource_visible"] = m_resourceLabel && m_resourceLabel->is_visible();
	s["castle"] = m_icons && m_icons->castle();
	s["anchored"] = m_anchored;
	s["arrow"] = m_arrowShown;
	s["unit_exit_arrows"] = m_exitDirectionCount;
	Array icons;
	if (m_icons)
	{
		for (size_t i = 0; i < m_icons->icons().size(); ++i)
		{
			const PlacementIcons::Icon &icon = m_icons->icons()[i];
			Dictionary d;
			d["template"] = icon.tt ? toGodotString(icon.tt->getName()) : String();
			const Coord3D p = icon.drawable ? *icon.drawable->getPosition() : Coord3D{};
			d["x"] = p.x;
			d["y"] = p.y;
			d["z"] = p.z;
			d["angle"] = icon.angle;
			d["code"] = icon.code;
			Color tint(0, 0, 0, 0);
			if (icon.tint == PlacementIcons::TINT_ILLEGAL)
			{
				tint = Color(1, 0, 0, 1);
			}
			else if (icon.tint == PlacementIcons::TINT_CODE9)
			{
				tint = Color(0, 0, icon.tintBlue, 1);
			}
			else if (icon.tint == PlacementIcons::TINT_ALTERNATE)
			{
				tint = Color(.45f, .35f, 0, 1);
			}
			d["tint"] = tint;
			d["bib"] = icon.bib == PlacementIcons::BIB_NONE ? String() : (icon.bib == PlacementIcons::BIB_NORMAL ? String("TBBib.tga") : String("TBRedBib.tga"));
			d["piece"] = icon.piece;
			d["base_site"] = icon.baseSite;
			d["house_color"] = icon.drawable ? (int64_t)icon.drawable->getHouseColor() : (int64_t)0;
			d["opacity"] = icon.drawable ? icon.drawable->drawOpacity() : 0.0f;
			d["instances"] = i < m_views.size() ? m_views[i].instances : 0;
			d["bib_template"] = i < m_views.size() && m_views[i].bib && m_views[i].bibTemplate ? toGodotString(m_views[i].bibTemplate->getName()) : String();
			d["shadow_decal"] = i < m_views.size() ? toGodotString(m_views[i].shadowTexture) : String(); // lane OWNERFIX-2
			d["shadow_decal_size"] = i < m_views.size() ? m_views[i].shadowSizeX : 0.0f;
			icons.push_back(d);
		}
	}
	s["icons"] = icons;
	Array errors;
	for (const std::string &e : m_errors)
	{
		errors.push_back(toGodotString(e));
	}
	s["errors"] = errors;
	Array notes;
	if (m_icons)
	{
		for (const std::string &n : m_icons->notes())
		{
			notes.push_back(toGodotString(n));
		}
	}
	s["notes"] = notes; // lane BUILD-5 r3: the counted draw problems (hide misses, retail data defects)
	return s;
}
} // namespace godot

namespace godot {
void PlacementGhostRenderer::updateFormation(LiveGame &game, const InGameUI &ui, bool idle) {
 const float opacity=ui.formationActive || ui.clientFrame()<=ui.formationExpires?1.f:std::max(0.f,1.f-(ui.clientFrame()-ui.formationExpires)/15.f);
 if(opacity<=0 || ui.formationSlots.empty()) { if(m_formation)m_formation->hide();return; }
 if(!idle)return;
 if(!m_formation) {
  m_formation=memnew(MeshInstance3D);m_parent->add_child(m_formation);
  m_formationId=m_formation->get_instance_id();
  m_formation->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
 }
 Ref<ImmediateMesh> mesh;mesh.instantiate();
 const auto *fa=ui.formationData;if(!fa)return;
 struct Sprite {std::string texture;float x,y,width,height,angle;};
 std::vector<Sprite> sprites;
 auto add=[&](const std::string &name,float x,float y,float angle,float width=-1.f){
  const auto *decal=fa->decal(name,true);if(!decal || decal->texture.empty()){if(!name.empty())error("formation preview decal missing: "+name);return;}
  sprites.push_back({decal->texture,x,y,width<0?decal->width:width,decal->height,angle});
 };
 for(const auto &slot:ui.formationSlots) {
  auto *object=game.logic().findObjectByID(slot.object);
  auto *horde=object?dynamic_cast<HordeContain *>(object->getContain()):nullptr;
  if(horde) {
   const auto positions=FormationAssistant::rankOffsets(*object);
   const auto center=FormationAssistant::rankCenter(*object);
   const float c=std::cos(slot.angle),s=std::sin(slot.angle);
   for(const auto &position:positions) {
    const float x=position.x-center.x,y=position.y-center.y;
    add(slot.preview,slot.position.x+x*c-y*s,slot.position.y+x*s+y*c,slot.angle+1.5707964f);
   }
  } else add(slot.preview,slot.position.x,slot.position.y,slot.angle+1.5707964f);
 }
 const float angle=ui.formationAngle,c=std::cos(angle),s=std::sin(angle);
 const auto *head=fa->decal(fa->head),*body=fa->decal(fa->body),*base=fa->decal(fa->base);
 const float baseWidth=base?base->width:0,bodyWidth=body?body->width:0;
 const float side=std::max(ui.formationDragLength,baseWidth*.5f+bodyWidth*.5f);
 const float offset=(body?body->height*.5f:0)+30,front=(body?body->height:0)+30+(head?head->height*.5f:0);
 const auto anchor=ui.formationAnchor;
 add(fa->body,anchor.x+offset*c,anchor.y+offset*s,angle-1.5707964f,side*2-baseWidth);
 add(fa->base,anchor.x+offset*c-(side-10)*s,anchor.y+offset*s+(side-10)*c,angle+1.5707964f);
 add(fa->base,anchor.x+offset*c+(side-10)*s,anchor.y+offset*s-(side-10)*c,angle-1.5707964f);
 // The texture quad axes differ from the native decal: flip directional art.
 add(fa->head,anchor.x+front*c,anchor.y+front*s,angle+1.5707964f);
 std::map<std::string,std::vector<Sprite>> batches;for(const auto &sprite:sprites)batches[sprite.texture].push_back(sprite);
 const Player *player=game.logic().players().getLocalPlayer();
 const auto rgb=player?player->getPlayerColor():0xffffff;
 const Color tint(((rgb>>16)&255)/255.f,((rgb>>8)&255)/255.f,(rgb&255)/255.f,opacity);
 m_formationTextures.clear();
 for(const auto &batch:batches) {
  const Ref<Texture2D> tex=texture(batch.first+".tga");if(tex.is_null())continue;
  m_formationTextures.push_back(batch.first+".tga");
  Ref<StandardMaterial3D> material;material.instantiate();
  material->set_shading_mode(BaseMaterial3D::SHADING_MODE_UNSHADED);material->set_cull_mode(BaseMaterial3D::CULL_DISABLED);
  material->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA);
  const bool arrow=(head && batch.first==head->texture)||(body && batch.first==body->texture)||(base && batch.first==base->texture);
  material->set_albedo(arrow?Color(1,1,1,opacity):tint);
  material->set_texture(BaseMaterial3D::TEXTURE_ALBEDO,tex);
  mesh->surface_begin(Mesh::PRIMITIVE_TRIANGLES,material);
  for(const auto &sprite:batch.second) {
   const float c=std::cos(sprite.angle),s=std::sin(sprite.angle);
   const Vector2 uv[4]={{0,0},{1,0},{1,1},{0,1}};
   for(int index:{0,1,2,0,2,3}) {
    const float lx=(uv[index].x-.5f)*sprite.width,ly=(uv[index].y-.5f)*sprite.height;
    const float x=sprite.x+lx*c-ly*s,y=sprite.y+lx*s+ly*c;
    mesh->surface_set_uv(uv[index]);mesh->surface_add_vertex(Vector3(x,game.logic().getGroundHeight(x,y)+2,-y));
   }
  }
  mesh->surface_end();
 }
 m_formation->set_mesh(mesh);m_formation->show();
}

} // namespace godot
