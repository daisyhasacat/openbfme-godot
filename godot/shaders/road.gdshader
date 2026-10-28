// OpenBFME. GPL-3.0.
//
// Road segment ribbon. Retail draws roads with ST_ROAD_BASE (road texture x static terrain diffuse, optional
// cloud/noise passes, W3DShaderManager); the road geometry comes from W3DRoadBuffer. Straight
// segments and degree-two corners are generated (Y/H junctions and cross-type stacking remain S-036). Colour: texture * vertex colour where
// the vertex colour is the static terrain light at the segment's position (ZH RoadSegment::updateSegLighting).
shader_type spatial;
render_mode unshaded, blend_mix, depth_draw_never, cull_disabled;

#include "res://shaders/snow_cover.gdshaderinc"

uniform sampler2D road_tex : filter_linear_mipmap, repeat_enable, hint_default_white;
uniform float output_gamma = 2.2;
uniform float overbright = 1.0;
// lane ZOOM-1: the local player's shroud (GameWorld::refresh_shroud sets these on the terrain and on this layer): INFERENCE (ZH draws its terrain's
// shroud pass after the roads, W3DRoadBuffer has none; whether it darkens them was not read): the road darkens with the ground under it, as the water does
uniform sampler2D shroud_tex : filter_linear, repeat_disable, hint_default_white;
uniform vec2 shroud_origin = vec2(0.0);
uniform vec2 shroud_extent = vec2(1.0);
uniform vec3 shroud_color = vec3(1.0);
uniform bool shroud_enabled = false;
// map browser: pixels outside this SAGE rect (min x, min y, max x, max y) are discarded; the default keeps everything
uniform vec4 clip_rect = vec4(-1e20, -1e20, 1e20, 1e20);
varying vec2 v_clip_xy;
varying vec3 v_snow_world;
varying vec2 v_shroud_uv;

void vertex() {
	vec3 shroud_world = (MODEL_MATRIX * vec4(VERTEX, 1.0)).xyz;
	v_snow_world = shroud_world;
	v_shroud_uv = (vec2(shroud_world.x, -shroud_world.z) - shroud_origin) / shroud_extent;
	v_clip_xy = vec2(shroud_world.x, -shroud_world.z);
}

// lane RENDER-4: the hand-over to Godot. output_gamma 1.0: the gamma-space value itself (the transparent pass composites in gamma space,
// GodotGammaComposite.h). Any other value: the exact sRGB decode, the inverse of the output encode Godot applies, so the display shows the
// retail byte. The former pow(x, 2.2) did not invert that curve and darkened the dark end of every lit pixel (retail 0.1 displayed 0.073).
vec3 retail_output(vec3 c) {
	c = max(c, vec3(0.0));
	if (output_gamma == 1.0) {
		return c;
	}
	return mix(c / 12.92, pow((c + 0.055) / 1.055, vec3(2.4)), step(vec3(0.04045), c));
}

void fragment() {
	if (v_clip_xy.x < clip_rect.x || v_clip_xy.y < clip_rect.y || v_clip_xy.x > clip_rect.z || v_clip_xy.y > clip_rect.w) {
		discard;
	}
	vec4 t = texture(road_tex, UV);
	// Roads have no vertex normals; derive the ribbon slope from world position.
	vec3 world = v_snow_world;
	vec3 normal = normalize(cross(dFdx(world), dFdy(world)));
	if (normal.y < 0.0) normal = -normal;
	vec3 albedo = snow_ground_cover(t.rgb, vec3(v_clip_xy.x,0.0,-v_clip_xy.y), normal, vec4(0.0));
	vec3 col = albedo * COLOR.rgb * overbright;
	if (shroud_enabled) {
		col = col * shroud_color * texture(shroud_tex, v_shroud_uv).r; // lane ZOOM-1
	}
	ALBEDO = retail_output(col);
	ALPHA = t.a;
}
