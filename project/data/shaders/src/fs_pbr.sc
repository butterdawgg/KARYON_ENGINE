$input v_worldPos, v_normal, v_tangent, v_texcoord0, v_shadowCoord0, v_shadowCoord1, v_shadowCoord2, v_shadowCoord3, v_shadowWorldPos

#include <bgfx_shader.sh>
#include "shadow_common.sh"

#ifndef MAX_POINT_LIGHTS
#	define MAX_POINT_LIGHTS 4
#endif // MAX_POINT_LIGHTS

#ifndef SHADOW_MAP_SIZE
#	define SHADOW_MAP_SIZE 2048
#endif // SHADOW_MAP_SIZE

#ifndef SHADOW_BIAS
#	define SHADOW_BIAS 0.0012
#endif // SHADOW_BIAS

#ifndef SHADOW_POINT_BIAS
#	define SHADOW_POINT_BIAS 0.01
#endif // SHADOW_POINT_BIAS

#define PI 3.14159265359

uniform vec4 u_baseColorFactor;
uniform vec4 u_metallicRoughnessFactor; // x = metallic, y = roughness
uniform vec4 u_emissiveFactor;

uniform vec4 u_camPos;
uniform vec4 u_ambientColor;

uniform vec4 u_dirLightDir;   // xyz = direction the light travels
uniform vec4 u_dirLightColor; // rgb = color * intensity

uniform vec4 u_pointLightPos[MAX_POINT_LIGHTS];   // xyz = world position, w = range
uniform vec4 u_pointLightColor[MAX_POINT_LIGHTS]; // rgb = color * intensity

SAMPLER2D(s_albedo, 0);
SAMPLER2D(s_normal, 1);
SAMPLER2D(s_metallicRoughness, 2);
SAMPLER2D(s_emissive, 3);

SAMPLER2D(s_shadowMap0, 4);
SAMPLER2D(s_shadowMap1, 5);
SAMPLER2D(s_shadowMap2, 6);
SAMPLER2D(s_shadowMap3, 7);

SAMPLERCUBE(s_pointShadowMap0, 8);
SAMPLERCUBE(s_pointShadowMap1, 9);
SAMPLERCUBE(s_pointShadowMap2, 10);
SAMPLERCUBE(s_pointShadowMap3, 11);

float D_GGX(float NdotH, float roughness)
{
	float a = roughness * roughness;
	float a2 = a * a;
	float d = (NdotH * a2 - NdotH) * NdotH + 1.0;

	return a2 / (PI * d * d + 1.0e-6);
}

float G_SmithGGX(float NdotV, float NdotL, float roughness)
{
	float r = roughness + 1.0;
	float k = (r * r) / 8.0;

	float gv = NdotV / (NdotV * (1.0 - k) + k);
	float gl = NdotL / (NdotL * (1.0 - k) + k);

	return gv * gl;
}

vec3 F_Schlick(float cosTheta, vec3 F0)
{
	return F0 + (vec3(1.0, 1.0, 1.0) - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// Cook-Torrance direct lighting contribution of a single light
vec3 EvaluateLight(vec3 N, vec3 V, vec3 L, vec3 radiance, vec3 albedo, float metallic, float roughness)
{
	vec3 H = normalize(V + L);

	float NdotL = max(dot(N, L), 0.0);
	float NdotV = max(dot(N, V), 1.0e-4);
	float NdotH = max(dot(N, H), 0.0);
	float VdotH = max(dot(V, H), 0.0);

	vec3 F0 = mix(vec3(0.04, 0.04, 0.04), albedo, metallic);

	float D = D_GGX(NdotH, roughness);
	float G = G_SmithGGX(NdotV, NdotL, roughness);
	vec3 F = F_Schlick(VdotH, F0);

	vec3 specular = (D * G * F) / max(4.0 * NdotV * NdotL, 1.0e-4);

	vec3 kd = (vec3(1.0, 1.0, 1.0) - F) * (1.0 - metallic);
	vec3 diffuse = kd * albedo / PI;

	return (diffuse + specular) * radiance * NdotL;
}

// Picks the nearest cascade whose projected UV still lands within a safe
// [0.01, 0.99] margin (cascades are nested - shadowCoordN for smaller N
// covers a strictly smaller world volume - so this in-range test doubles as
// a nearest-cascade-first selection without needing a separate depth
// compare against per-cascade split distances). Takes the shadow coords as
// parameters rather than reading v_shadowCoordN directly - bgfx's HLSL
// cross-compilation only exposes varyings inside main(), not to helper
// functions (matches why EvaluateLight above takes N/V/L/etc. as parameters
// instead of touching varyings itself).
float ComputeCascadeShadow(vec4 shadowCoord0, vec4 shadowCoord1, vec4 shadowCoord2, vec4 shadowCoord3, vec2 fragCoord)
{
	vec2 tc0 = shadowCoord0.xy / shadowCoord0.w;
	vec2 tc1 = shadowCoord1.xy / shadowCoord1.w;
	vec2 tc2 = shadowCoord2.xy / shadowCoord2.w;

	bool sel0 = all(lessThan(tc0, vec2_splat(0.99) ) ) && all(greaterThan(tc0, vec2_splat(0.01) ) );
	bool sel1 = all(lessThan(tc1, vec2_splat(0.99) ) ) && all(greaterThan(tc1, vec2_splat(0.01) ) );
	bool sel2 = all(lessThan(tc2, vec2_splat(0.99) ) ) && all(greaterThan(tc2, vec2_splat(0.01) ) );

	vec2 texelSize = vec2_splat(1.0 / float(SHADOW_MAP_SIZE) );

	// A single result variable assigned by an exhaustive if/else-if/else
	// (explicit trailing else, not a bare fallthrough statement) rather than
	// an early return per branch - some HLSL compilers (FXC/dxbc) flag the
	// latter as "potentially uninitialized" once the branches' expressions
	// involve a function with its own internal control flow (PCF's sampling
	// loop), even though every path here does return a value.
	float result;

	if (sel0)
	{
		result = PCF(s_shadowMap0, shadowCoord0, SHADOW_BIAS, texelSize, fragCoord);
	}
	else if (sel1)
	{
		result = PCF(s_shadowMap1, shadowCoord1, SHADOW_BIAS, texelSize, fragCoord);
	}
	else if (sel2)
	{
		result = PCF(s_shadowMap2, shadowCoord2, SHADOW_BIAS, texelSize, fragCoord);
	}
	else
	{
		result = PCF(s_shadowMap3, shadowCoord3, SHADOW_BIAS, texelSize, fragCoord);
	}

	return result;
}

// Single-tap hard shadow: lightToFrag is the (unnormalized) vector from the
// light to the receiver, used both as the cubemap sampling direction and,
// via its length, as the receiver distance compared against what the
// caster pass packed.
float ComputePointShadow(samplerCube shadowMap, vec3 lightToFrag, float range)
{
	float dist = length(lightToFrag) / max(range, 1.0e-4);
	float occluder = UnpackRgbaToFloat(textureCube(shadowMap, lightToFrag) );

	return step(dist - SHADOW_POINT_BIAS, occluder);
}

void main()
{
	vec3 albedo = texture2D(s_albedo, v_texcoord0).rgb * u_baseColorFactor.rgb;

	vec4 mrSample = texture2D(s_metallicRoughness, v_texcoord0);
	float metallic = clamp(u_metallicRoughnessFactor.x * mrSample.b, 0.0, 1.0);
	float roughness = clamp(u_metallicRoughnessFactor.y * mrSample.g, 0.045, 1.0);

	vec3 emissive = texture2D(s_emissive, v_texcoord0).rgb * u_emissiveFactor.rgb;

	vec3 geoNormal = normalize(v_normal);
	vec3 tangent = normalize(v_tangent.xyz - geoNormal * dot(geoNormal, v_tangent.xyz));
	vec3 bitangent = cross(geoNormal, tangent) * v_tangent.w;

	vec3 normalSample = texture2D(s_normal, v_texcoord0).xyz * 2.0 - 1.0;

	// Apply the TBN basis via an explicit linear combination rather than
	// building a mat3 and using bgfx's mul(): mul() swaps argument order on
	// some backends to compensate for HLSL/GLSL row- vs column-major
	// differences in bgfx's own uniform matrices, but a locally-constructed
	// mat3 doesn't reliably get the same treatment - which silently
	// transposed the basis for left-handed vertices (mirrored-UV regions,
	// tangent.w < 0), while right-handed ones came out fine regardless. This
	// form is unambiguous on every backend.
	vec3 N = normalize(tangent * normalSample.x + bitangent * normalSample.y + geoNormal * normalSample.z);

	vec3 V = normalize(u_camPos.xyz - v_worldPos);

	vec3 Lo = vec3(0.0, 0.0, 0.0);

	// Directional light
	{
		vec3 L = normalize(-u_dirLightDir.xyz);
		float shadow = ComputeCascadeShadow(v_shadowCoord0, v_shadowCoord1, v_shadowCoord2, v_shadowCoord3, gl_FragCoord.xy);
		Lo += EvaluateLight(N, V, L, u_dirLightColor.rgb, albedo, metallic, roughness) * shadow;
	}

	// Point lights - unrolled rather than a loop over MAX_POINT_LIGHTS: each
	// light's shadow cubemap is a separately-declared samplerCube (bgfx has
	// no portable way to dynamically index 4 distinct sampler objects by a
	// loop variable across every backend profile this engine targets, the
	// same constraint that makes ComputeCascadeShadow an if/else chain
	// rather than an indexed array).
	{
		vec3 toLight = u_pointLightPos[0].xyz - v_worldPos;
		float dist = length(toLight);
		vec3 L = toLight / max(dist, 1.0e-4);
		float range = u_pointLightPos[0].w;
		float falloff = clamp(1.0 - pow(dist / max(range, 1.0e-4), 4.0), 0.0, 1.0);
		float atten = (falloff * falloff) / (dist * dist + 1.0);
		vec3 radiance = u_pointLightColor[0].rgb * atten;
		float shadow = ComputePointShadow(s_pointShadowMap0, u_pointLightPos[0].xyz - v_shadowWorldPos, range);
		Lo += EvaluateLight(N, V, L, radiance, albedo, metallic, roughness) * shadow;
	}
	{
		vec3 toLight = u_pointLightPos[1].xyz - v_worldPos;
		float dist = length(toLight);
		vec3 L = toLight / max(dist, 1.0e-4);
		float range = u_pointLightPos[1].w;
		float falloff = clamp(1.0 - pow(dist / max(range, 1.0e-4), 4.0), 0.0, 1.0);
		float atten = (falloff * falloff) / (dist * dist + 1.0);
		vec3 radiance = u_pointLightColor[1].rgb * atten;
		float shadow = ComputePointShadow(s_pointShadowMap1, u_pointLightPos[1].xyz - v_shadowWorldPos, range);
		Lo += EvaluateLight(N, V, L, radiance, albedo, metallic, roughness) * shadow;
	}
	{
		vec3 toLight = u_pointLightPos[2].xyz - v_worldPos;
		float dist = length(toLight);
		vec3 L = toLight / max(dist, 1.0e-4);
		float range = u_pointLightPos[2].w;
		float falloff = clamp(1.0 - pow(dist / max(range, 1.0e-4), 4.0), 0.0, 1.0);
		float atten = (falloff * falloff) / (dist * dist + 1.0);
		vec3 radiance = u_pointLightColor[2].rgb * atten;
		float shadow = ComputePointShadow(s_pointShadowMap2, u_pointLightPos[2].xyz - v_shadowWorldPos, range);
		Lo += EvaluateLight(N, V, L, radiance, albedo, metallic, roughness) * shadow;
	}
	{
		vec3 toLight = u_pointLightPos[3].xyz - v_worldPos;
		float dist = length(toLight);
		vec3 L = toLight / max(dist, 1.0e-4);
		float range = u_pointLightPos[3].w;
		float falloff = clamp(1.0 - pow(dist / max(range, 1.0e-4), 4.0), 0.0, 1.0);
		float atten = (falloff * falloff) / (dist * dist + 1.0);
		vec3 radiance = u_pointLightColor[3].rgb * atten;
		float shadow = ComputePointShadow(s_pointShadowMap3, u_pointLightPos[3].xyz - v_shadowWorldPos, range);
		Lo += EvaluateLight(N, V, L, radiance, albedo, metallic, roughness) * shadow;
	}

	vec3 ambient = u_ambientColor.rgb * albedo;

	vec3 color = ambient + Lo + emissive;

	gl_FragColor = vec4(color, 1.0);
}
