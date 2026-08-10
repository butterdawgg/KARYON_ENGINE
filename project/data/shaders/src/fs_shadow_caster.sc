#include <bgfx_shader.sh>
#include "shadow_common.sh"

// Packs gl_FragCoord.z (hardware window-space depth, always [0,1] on every
// backend once the light's projection matrix is built with the correct
// bgfx::getCaps()->homogeneousDepth) into the caster's colour attachment -
// see shadow_map.cpp's ComputeCascadeData for why this needs no manual NDC
// range handling, unlike deriving depth from a custom clip-space varying.
void main()
{
	gl_FragColor = PackFloatToRgba(gl_FragCoord.z);
}
