$input v_worldPos, v_normal, v_tangent, v_texcoord0

#include <bgfx_shader.sh>

#ifndef MAX_POINT_LIGHTS
#	define MAX_POINT_LIGHTS 4
#endif // MAX_POINT_LIGHTS

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
	mat3 TBN = mat3(tangent, bitangent, geoNormal);

	vec3 normalSample = texture2D(s_normal, v_texcoord0).xyz * 2.0 - 1.0;
	vec3 N = normalize(mul(TBN, normalSample) );

	vec3 V = normalize(u_camPos.xyz - v_worldPos);

	vec3 Lo = vec3(0.0, 0.0, 0.0);

	// Directional light
	{
		vec3 L = normalize(-u_dirLightDir.xyz);
		Lo += EvaluateLight(N, V, L, u_dirLightColor.rgb, albedo, metallic, roughness);
	}

	// Point lights
	for (int i = 0; i < MAX_POINT_LIGHTS; ++i)
	{
		vec3 toLight = u_pointLightPos[i].xyz - v_worldPos;
		float dist = length(toLight);
		vec3 L = toLight / max(dist, 1.0e-4);

		float range = u_pointLightPos[i].w;
		float falloff = clamp(1.0 - pow(dist / max(range, 1.0e-4), 4.0), 0.0, 1.0);
		float atten = (falloff * falloff) / (dist * dist + 1.0);

		vec3 radiance = u_pointLightColor[i].rgb * atten;

		Lo += EvaluateLight(N, V, L, radiance, albedo, metallic, roughness);
	}

	vec3 ambient = u_ambientColor.rgb * albedo;

	vec3 color = ambient + Lo + emissive;

	gl_FragColor = vec4(color, 1.0);
}
