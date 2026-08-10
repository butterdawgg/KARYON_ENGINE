$input v_worldPos

#include <bgfx_shader.sh>
#include "shadow_common.sh"

uniform vec4 u_lightOrigin; // xyz = light world position, w = range

void main()
{
	float dist = length(v_worldPos - u_lightOrigin.xyz) / max(u_lightOrigin.w, 1.0e-4);

	gl_FragColor = PackFloatToRgba(dist);
}
