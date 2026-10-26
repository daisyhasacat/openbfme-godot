# Standing water rendering

Standing water now reads its map-selected W3D's WaterShader.FX properties.
The former stand-in used one bump sample, a fixed blue tint and 0.78 opacity,
repeated every 160 world units, and sampled the depth-colour LUT at one
constant coordinate. Different authored water materials therefore looked alike.

The implemented path uses the shipped RotWK `watershader.fxo` data, decoded
with `tools/render/fx_params.py` and `tools/render/d3d9_disasm.py`:

- Pixel shader blob 8 combines two bump samples as
  `2*(b0+b1) - (2,2,1)`, scales XY by BumpScale, normalizes, and adds
  diffuse/specular lighting to the environment colour plus the product of
  two WaveTexture samples.
- Vertex shader blob 9 derives the two stream directions from WindAngle
  plus/minus WaveAngle/2. Coordinates use world XY / 600 and the authored
  BumpUVScale, EnvUVScale and WaveUVScale; its streams use WindSpeed + WaveSpeed.
- Its preshader supplies colour `2 * DimmingFactor * MaterialColorDiffuse.rgb
  * DirectionalLight.Color`. The material alpha is used when terrain blending
  is disabled; with valid map depth parameters the alpha is
  `saturate(depth/(MaxAlphaDepth*DeepWaterAlpha^2))*DeepWaterAlpha`.

Defaults come from the effect parameter table; authored W3D properties override
them. In the mounted Harlindon map, WaterPlanePCA.W3D specifies diffuse
(0.15, 0.3, 0.35, 0.9) and BumpUVScale (15,15). Each area owns its material so
water height and authored settings cannot leak between areas sharing textures.
No simulation state, update order or random numbers change.

S-035 remains open. The first accepted scene object light supplies the shader
light; the original water caller's light binding has not been traced. Selecting this no-reflection path for every WaterShader.FX
material is an approximation, including PCA-authored water. TIME is a
presentation-clock bridge; the ground texture uses bilinear height sampling
rather than reproducing the original water mesh's per-vertex depth inputs.
Planar reflections, PCA displacement, the depth-colour LUT, standing-wave
geometry and full fog/technique parity are not implemented. Legacy areas
without a WaterShader.FX material use effect defaults with their map textures
and explicitly report that approximation in water.material_stops. Missing
referenced materials and textures reach the builder's errors.

Validation uses exact native material/geometry assertions and a mounted-map
Godot regression. Run `godot/tests/standing_water_test.gd` without `--headless`
for Metal/Forward+ shader compilation; set OPENBFME_WATER_SCREENSHOT to save
a Harlindon screenshot. The headless
run checks material bindings only. These are data and renderer checks, not a
claim of retail pixel parity.
