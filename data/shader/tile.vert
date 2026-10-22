layout (location = 0) in vec2 inVertex;
#ifdef TW_TILE_TEXTURED
layout (location = 1) in uvec4 inVertexTexCoord;
#endif

uniform mat4 gPos; // XClient: a full matrix for the 3D view

#ifdef TW_TILE_TEXTURED
out vec3 TexCoord;
#endif

void main()
{
	gl_Position = gPos * vec4(inVertex, 0.0, 1.0);

#ifdef TW_TILE_TEXTURED
	TexCoord = vec3(inVertexTexCoord.xyz);
#endif
}
