$input a_position, a_normal, a_tangent, a_texcoord0
$output v_worldPos, v_normal, v_tangent, v_texcoord0, v_shadowCoord0, v_shadowCoord1, v_shadowCoord2, v_shadowCoord3, v_shadowWorldPos

#include <bgfx_shader.sh>

#ifndef NUM_CASCADES
#	define NUM_CASCADES 4
#endif // NUM_CASCADES

#ifndef SHADOW_NORMAL_OFFSET
#	define SHADOW_NORMAL_OFFSET 0.001
#endif // SHADOW_NORMAL_OFFSET

uniform mat4 u_shadowMapMtx[NUM_CASCADES];

void main()
{
	vec4 worldPos = mul(u_model[0], vec4(a_position, 1.0) );

	v_worldPos  = worldPos.xyz;
	v_normal    = mul(u_model[0], vec4(a_normal, 0.0) ).xyz;
	v_tangent   = vec4(mul(u_model[0], vec4(a_tangent.xyz, 0.0) ).xyz, a_tangent.w);
	v_texcoord0 = a_texcoord0;

	// Push the shadow lookup position along the normal before projecting
	// into light space - reduces shadow acne on grazing-angle surfaces
	// without needing a slope-scaled depth bias. Shared by both the
	// cascade (v_shadowCoordN) and point-light (v_shadowWorldPos) paths.
	vec3 shadowPos = worldPos.xyz + v_normal * SHADOW_NORMAL_OFFSET;

	v_shadowWorldPos = shadowPos;
	v_shadowCoord0 = mul(u_shadowMapMtx[0], vec4(shadowPos, 1.0) );
	v_shadowCoord1 = mul(u_shadowMapMtx[1], vec4(shadowPos, 1.0) );
	v_shadowCoord2 = mul(u_shadowMapMtx[2], vec4(shadowPos, 1.0) );
	v_shadowCoord3 = mul(u_shadowMapMtx[3], vec4(shadowPos, 1.0) );

	gl_Position = mul(u_viewProj, worldPos);
}
