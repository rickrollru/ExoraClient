layout (location = 0) in vec2 inVertex;
layout (location = 1) in vec2 inVertexTexCoord;
layout (location = 2) in vec4 inVertexColor;

uniform mat4 gPos; // XClient: a full matrix for the 3D view
uniform float gTextureSize;

out vec2 texCoord;
out vec4 outVertColor;

void main()
{
	gl_Position = gPos * vec4(inVertex, 0.0, 1.0);

	texCoord = vec2(inVertexTexCoord.x / gTextureSize, inVertexTexCoord.y / gTextureSize);
	outVertColor = inVertexColor;
}
