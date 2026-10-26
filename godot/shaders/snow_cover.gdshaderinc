// OpenBFME enhanced weather: world-space snow on exposed upward-facing surfaces.
// Inputs/outputs use retail gamma space, before fog and shroud. No textures or RNG.
global uniform float openbfme_snow_cover;
float snow_hash(vec2 p) {
 return fract(sin(dot(p, vec2(127.1,311.7))) * 43758.5453);
}
float snow_noise(vec2 p) {
 vec2 cell=floor(p), f=fract(p);
 f=f*f*(3.0-2.0*f);
 return mix(mix(snow_hash(cell),snow_hash(cell+vec2(1.0,0.0)),f.x),
            mix(snow_hash(cell+vec2(0.0,1.0)),snow_hash(cell+vec2(1.0)),f.x),f.y);
}
vec3 snow_cover(vec3 color, vec3 world, vec3 normal) {
 float slope=smoothstep(0.18,0.72,normalize(normal).y);
 float drift=snow_noise(world.xz*0.018);
 float grain=snow_noise(world.xz*0.48);
 float mask=clamp(openbfme_snow_cover,0.0,1.0)*slope*mix(0.76,1.0,smoothstep(0.15,0.75,drift));
 // Preserve baked lighting variation and detail rather than flattening everything to white.
 float illumination=clamp(dot(color,vec3(0.2126,0.7152,0.0722))*1.4,0.32,1.0);
 vec3 snow=vec3(0.83,0.89,0.95)*illumination*(0.94+0.06*grain);
 return mix(color,snow,mask);
}
