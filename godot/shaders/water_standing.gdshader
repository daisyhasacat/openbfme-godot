// OpenBFME. GPL-3.0.
//
// Retail watershader.fxo no-reflection bump/specular arithmetic (ps blob 8,
// vs blob 9, tools/render/d3d9_disasm.py). Material parameters come from the
// map's W3D effect. S-035: TIME bridge, bilinear depth reconstruction and
// effect-technique selection are inferred; reflection/PCA/standing waves
// and the depth-colour LUT are not implemented.
shader_type spatial;
render_mode unshaded, blend_mix, depth_draw_never, cull_disabled;

uniform sampler2D bump_tex : filter_linear_mipmap, repeat_enable, hint_default_white;
uniform sampler2D sky_tex : filter_linear_mipmap, repeat_enable, hint_default_white;
uniform sampler2D wave_tex : filter_linear_mipmap, repeat_enable, hint_default_black;
uniform sampler2D terrain_height_tex : filter_linear, repeat_disable;
uniform vec4 material_diffuse = vec4(0.15, 0.3, 0.35, 0.5);
uniform float dimming = 0.8;
uniform float bump_scale = 0.2;
uniform float specular_exponent = 50.0;
uniform vec2 bump_uv_scale = vec2(7.0);
uniform vec2 env_uv_scale = vec2(0.2);
uniform vec2 wave_uv_scale = vec2(7.0);
uniform float wind_angle = 0.0;
uniform float wind_speed = 0.02;
uniform float wave_angle = 0.2;
uniform float wave_speed = 0.0;
uniform vec3 light_direction = vec3(0.0, 0.0, 1.0);
uniform vec3 light_color = vec3(1.0);
uniform vec2 terrain_origin = vec2(0.0);
uniform vec2 terrain_size = vec2(1.0);
uniform float water_height = 0.0;
uniform float max_alpha_depth = 0.0;
uniform float deep_water_alpha = 0.0;
uniform bool blend_terrain_alpha = true;
uniform float output_gamma = 2.2;

varying vec2 v_world;
varying vec3 v_world_position;
// lane ZOOM-1: the local player's shroud (GameWorld::refresh_shroud sets these on the terrain and on this layer): ZH WaterRenderObjClass modulates its
// water with the shroud texture in a second pass / stage (W3DWater.cpp, the ST_SHROUD_TEXTURE passes); the border level outside the map is black
uniform sampler2D shroud_tex : filter_linear, repeat_disable, hint_default_white;
uniform vec2 shroud_origin = vec2(0.0);
uniform vec2 shroud_extent = vec2(1.0);
uniform vec3 shroud_color = vec3(1.0);
uniform bool shroud_enabled = false;
// map browser: pixels outside this SAGE rect (min x, min y, max x, max y) are discarded; the default keeps everything
uniform vec4 clip_rect = vec4(-1e20, -1e20, 1e20, 1e20);
varying vec2 v_clip_xy;
varying vec2 v_shroud_uv;

void vertex() {
	vec3 world = (MODEL_MATRIX * vec4(VERTEX, 1.0)).xyz;
	v_world = vec2(world.x, -world.z);
	v_world_position = world;
	vec3 shroud_world = (MODEL_MATRIX * vec4(VERTEX, 1.0)).xyz;
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
	// VS 9 uses world.xy / 600, not a fixed 160-unit repeating tile.
	vec2 base = v_world / 600.0;
	float a0 = wind_angle - wave_angle * 0.5;
	float a1 = wind_angle + wave_angle * 0.5;
	vec2 d0 = vec2(cos(a0), sin(a0));
	vec2 d1 = vec2(cos(a1), sin(a1));
	vec2 b0 = base + TIME * (wind_speed + wave_speed) * d0;
	vec2 b1 = base + TIME * (wind_speed + wave_speed) * d1;
	vec2 w0 = base + TIME * (wind_speed + wave_speed) * d0;
	vec2 w1 = base + TIME * (wind_speed + wave_speed) * d1;
	// D3D rotation rows are (cos, sin), (-sin, cos).
	b0 = vec2(dot(b0, d0), dot(b0, vec2(-d0.y, d0.x))) * bump_uv_scale;
	b1 = vec2(dot(b1, d1), dot(b1, vec2(-d1.y, d1.x))) * bump_uv_scale;
	w0 = vec2(dot(w0, d0), dot(w0, vec2(-d0.y, d0.x))) * wave_uv_scale;
	w1 = vec2(dot(w1, d1), dot(w1, vec2(-d1.y, d1.x))) * wave_uv_scale;
	vec3 n = 2.0 * (texture(bump_tex, b0).rgb + texture(bump_tex, b1).rgb) - vec3(2.0, 2.0, 1.0);
	n.xy *= bump_scale;
	n = normalize(n);
	vec3 eye_g = normalize(CAMERA_POSITION_WORLD - v_world_position);
	vec3 eye = vec3(eye_g.x, -eye_g.z, eye_g.y);
	vec3 half_vector = normalize(eye + light_direction);
	float lighting = max(dot(n, light_direction), 0.0) + pow(max(dot(n, half_vector), 0.0), specular_exponent);
	vec3 env = texture(sky_tex, base * env_uv_scale).rgb;
	vec3 waves = texture(wave_tex, w0).rgb * texture(wave_tex, w1).rgb;
	vec3 col = env * material_diffuse.rgb * dimming * (2.0 * light_color) * lighting + waves;
	float alpha = material_diffuse.a;
	if (blend_terrain_alpha && max_alpha_depth > 0.0 && deep_water_alpha > 0.0) {
		vec2 grid = (v_world - terrain_origin) / 10.0;
		float ground = texture(terrain_height_tex, (grid + 0.5) / terrain_size).r;
		float depth = max(water_height - ground, 0.0);
		// VS 9: saturate(depth / (MaxAlphaDepth * DeepWaterAlpha^2)) * DeepWaterAlpha.
		alpha = clamp(depth / (max_alpha_depth * deep_water_alpha * deep_water_alpha), 0.0, 1.0) * deep_water_alpha;
	}

	if (shroud_enabled) {
		col = col * shroud_color * texture(shroud_tex, v_shroud_uv).r; // lane ZOOM-1
	}
	ALBEDO = retail_output(col);
	ALPHA = clamp(alpha, 0.0, 1.0);
}
