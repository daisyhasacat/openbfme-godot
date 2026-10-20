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
