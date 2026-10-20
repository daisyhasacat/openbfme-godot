# Shadow reception brightness

The custom terrain and opaque W3D shaders already calculate retail lighting
and decode the result for Godot's linear buffer. Shadow reception splits this
baked colour between EMISSION (the unshadowed fraction) and ALBEDO (the
shadow-receiving fraction).

The custom light function previously added ALBEDO * ATTENUATION to
DIFFUSE_LIGHT. Godot Forward+'s scene_forward_clustered.glsl multiplies the
accumulated diffuse light by albedo after light(), so the receiving fraction
was coloured twice, even with no shadow. With strength s and baked linear
colour C the result became (1-s)*C + s*s*C*C instead of C.

The light function now adds only directional ATTENUATION; Godot supplies the
single albedo multiplication. The same correction applies to terrain and
generated opaque W3D receiver shaders. Retail light values, gamma conversion,
shadow strength and simulation remain unchanged. This corrects the Godot
integration; it does not claim parity for remaining retail shadow limitations.

The Metal GPU regression in godot/tests/shadows_test.gd compares reception
strength zero with 0.65 while no shadow is cast, on terrain and a real W3D
model. Every sampled RGB channel must agree within one 8-bit output byte.
It then enables casting and checks that shadows darken covered terrain while
uncovered pixels remain stable.

## Terrain sun input

A second integration error reduced terrain sunlight. The retail terrain.fxo
MainDirectionalLight parameter binds Sas.DirectionalLight[0]. The colour
callback RW 0x54CFD0 doubles the RGB channels when RW 0xD9B03C is set
(0x54D001..0x54D03B); the terrain effect then halves that input in its
overbright preshader. Godot supplied raw map diffuse before that halving,
leaving the main sun at half strength. The builder now shares the same
version-gated effect colour scale as object lighting. Vertex ambient/accent
colours and the shader's existing half/double operations are unchanged.

Tests pin the retail SAS binding and callback instructions, exercise the
version/multiplier gate, and verify the scaled sun on all Harlindon terrain
materials. A Metal capture was inspected after the correction. Standing water
remains the simplified S-035 implementation, including Harlindon's missing
SkyEnv.tga; this change does not establish water-rendering parity.
