layout (location = 0) in vec2 inVertex;
layout (location = 1) in vec2 inVertexTexCoord;
layout (location = 2) in vec4 inVertexColor;

uniform mat4 gPos; // XClient: a full matrix for the 3D view

#ifndef TW_ROTATIONLESS
uniform vec2 gCenter;
uniform float gRotation;
#endif

out vec2 texCoord;
out vec4 vertColor;

void main()
{
	vec2 FinalPos = vec2(inVertex.xy);
#ifndef TW_ROTATIONLESS
	float X = FinalPos.x - gCenter.x;
	float Y = FinalPos.y - gCenter.y;
	
	FinalPos.x = X * cos(gRotation) - Y * sin(gRotation) + gCenter.x;
	FinalPos.y = X * sin(gRotation) + Y * cos(gRotation) + gCenter.y;
#endif

	gl_Position = gPos * vec4(FinalPos, 0.0, 1.0);
	texCoord = inVertexTexCoord;
	vertColor = inVertexColor;
}
