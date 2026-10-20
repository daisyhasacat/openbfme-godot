# River texture mapping

Minhiriath contains three RiverAreas and no StandingWaterAreas in the pure
RotWK corpus. Its rivers use TWSkyEnv5, Noise0001, TWAlphaClouds01 and
WaterSurfaceClouds01. The previous renderer repeated the main texture every
80 world units, stretched its full U range across the river, and sampled the
opacity texture at constant V=0.5. That creates an artificial banded pattern.

Donor evidence: Open-BFME-2 matching
`Code/GameEngineDevice/Source/W3DDevice/GameClient/Water/W3DWaterDrawRiverWater.cpp`,
BFME2 RVA 0x100A0B..0x1011CA. Its boundary walk computes:

- repeatCount = (perimeter / 2 - firstEndWidth) / firstEndWidth;
- vScale = repeatCount / rectangleCount;
- each section uses base V = sectionIndex * vScale;
- main U spans 0..0.5; opacity U spans 0..1;
- both use V = baseV + sin(2*pi*baseV - 3*origin)/22 - origin.

The renderer now uses that layout, including longitudinal opacity sampling
and V wobble. The current mesh already consists of paired cross-sections;
its perimeter is the two banks plus the first and last ends. There is no
fixed world-space tile length. Degenerate zero-width starts are rejected.

Inference, not a target fact: the presentation shader uses TIME times the
map scroll speed for origin. Retail's exact origin update/clock, boundary
resampling, lighting and full RotWK equivalence remain S-035. Standing water
and waves are outside this fix. No simulation state or RNG is affected.

Geometry tests use a hand-derived rectangular perimeter and verify that
uniformly scaling a river leaves its UVs unchanged. Visual checking uses the
mounted Minhiriath map; no retail texture or map bytes are committed.

Validation: native Release build and two water geometry tests (27 assertions)
passed. A Metal Forward+ capture of the mounted Minhiriath map compiled the
shader, rendered all four rivers in the active map variant, and reported no
build errors. Visual comparison shows the old repeated narrow bands replaced
by a broader pattern. The pure corpus has three rivers; mounted map variants
can differ.
