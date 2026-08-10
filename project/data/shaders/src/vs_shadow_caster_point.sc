$input a_position
$output v_worldPos

#include <bgfx_shader.sh>

void main()
{
	gl_Position = mul(u_modelViewProj, vec4(a_position, 1.0) );
	v_worldPos = mul(u_model[0], vec4(a_position, 1.0) ).xyz;
}
