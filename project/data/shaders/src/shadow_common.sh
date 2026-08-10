// Shared helpers for the directional (cascaded) and point (cube) shadow
// caster/receiver shaders. Kept local rather than pulling in bgfx's
// examples/common/shaderlib.sh, which is part of bgfx's examples framework
// and isn't on this engine's shader include path.

// Packs a float in [0,1] into an RGBA8 color value, ~24 bits of precision.
// Used to store depth/distance in a plain color render target instead of a
// hardware depth texture, which isn't reliably shader-sampleable across
// every backend profile this engine targets.
vec4 PackFloatToRgba(float value)
{
	vec4 enc = vec4(1.0, 255.0, 65025.0, 16581375.0) * value;
	enc = fract(enc);
	enc -= enc.yzww * vec4(1.0 / 255.0, 1.0 / 255.0, 1.0 / 255.0, 0.0);
	return enc;
}

float UnpackRgbaToFloat(vec4 rgba)
{
	return dot(rgba, vec4(1.0, 1.0 / 255.0, 1.0 / 65025.0, 1.0 / 16581375.0));
}

// Jimenez 2014 interleaved gradient noise - cheap per-pixel pseudo-random
// angle used to rotate the PCF kernel so banding turns into less noticeable
// dithering.
float InterleavedGradientNoise(vec2 screenPos)
{
	vec3 magic = vec3(0.06711056, 0.00583715, 52.9829189);
	return fract(magic.z * fract(dot(screenPos, magic.xy)));
}

// Point index on a Vogel (golden-angle) disk - gives a well-distributed 2D
// sample pattern without needing precomputed offsets.
vec2 SampleVogelDisk(int index, int sampleCount)
{
	const float goldenAngle = 2.39996323;
	float r = sqrt((float(index) + 0.5) / float(sampleCount));
	float theta = float(index) * goldenAngle;
	return vec2(cos(theta), sin(theta)) * r;
}

vec2 RotateSample(vec2 s, vec2 sinCosAngle)
{
	return vec2(
		s.x * sinCosAngle.y - s.y * sinCosAngle.x,
		s.x * sinCosAngle.x + s.y * sinCosAngle.y
	);
}

// shadowCoord is a light-clip-space position already transformed by a
// shadowMapMtx (see shadow_map.h) - xy/w is the shadow map UV, z/w is a
// depth value directly comparable to what the caster pass packed via
// gl_FragCoord.z.
float HardShadow(sampler2D shadowMap, vec4 shadowCoord, float bias)
{
	vec2 texCoord = shadowCoord.xy / shadowCoord.w;

	bool outside = any(greaterThan(texCoord, vec2_splat(1.0))) || any(lessThan(texCoord, vec2_splat(0.0)));

	if (outside)
		return 1.0;

	float receiver = shadowCoord.z / shadowCoord.w - bias;
	float occluder = UnpackRgbaToFloat(texture2DLod(shadowMap, texCoord, 0.0));

	return step(receiver, occluder);
}

#define PCF_NUM_SAMPLES 16

// 16-tap Vogel-disk PCF, rotated per-pixel by interleaved gradient noise.
// texelSize is 1/shadowMapResolution; fragCoord should be gl_FragCoord.xy.
// Uses texture2DLod (explicit LOD 0) rather than texture2D: HLSL rejects
// automatic-gradient texture sampling inside a loop whose per-iteration
// texture coordinate varies at runtime ("gradient instruction used in a
// loop with varying iteration") - matches bgfx's own shadow-map reference,
// which uses the same explicit-LOD trick for exactly this reason.
float PCF(sampler2D shadowMap, vec4 shadowCoord, float bias, vec2 texelSize, vec2 fragCoord)
{
	vec2 texCoord = shadowCoord.xy / shadowCoord.w;

	bool outside = any(greaterThan(texCoord, vec2_splat(1.0))) || any(lessThan(texCoord, vec2_splat(0.0)));

	if (outside)
		return 1.0;

	float angle = InterleavedGradientNoise(fragCoord) * 6.283185307;
	vec2 rotation = vec2(sin(angle), cos(angle));

	float receiver = shadowCoord.z / shadowCoord.w - bias;

	float sum = 0.0;

	for (int i = 0; i < PCF_NUM_SAMPLES; ++i)
	{
		vec2 offset = RotateSample(SampleVogelDisk(i, PCF_NUM_SAMPLES), rotation) * texelSize * 2.0;
		float occluder = UnpackRgbaToFloat(texture2DLod(shadowMap, texCoord + offset, 0.0));
		sum += step(receiver, occluder);
	}

	return sum / float(PCF_NUM_SAMPLES);
}
