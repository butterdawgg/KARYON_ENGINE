vec3 v_worldPos       : TEXCOORD0 = vec3(0.0, 0.0, 0.0);
vec3 v_normal         : NORMAL    = vec3(0.0, 0.0, 1.0);
vec4 v_tangent        : TANGENT   = vec4(1.0, 0.0, 0.0, 1.0);
vec2 v_texcoord0      : TEXCOORD1 = vec2(0.0, 0.0);
vec4 v_shadowCoord0   : TEXCOORD2 = vec4(0.0, 0.0, 0.0, 0.0);
vec4 v_shadowCoord1   : TEXCOORD3 = vec4(0.0, 0.0, 0.0, 0.0);
vec4 v_shadowCoord2   : TEXCOORD4 = vec4(0.0, 0.0, 0.0, 0.0);
vec4 v_shadowCoord3   : TEXCOORD5 = vec4(0.0, 0.0, 0.0, 0.0);
vec3 v_shadowWorldPos : TEXCOORD6 = vec3(0.0, 0.0, 0.0);

vec3 a_position  : POSITION;
vec3 a_normal    : NORMAL;
vec4 a_tangent   : TANGENT;
vec2 a_texcoord0 : TEXCOORD0;
