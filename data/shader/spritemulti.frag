uniform sampler2D gTextureSampler;

uniform vec4 gVerticesColor;

in vec2 texCoord;
in vec4 vertColor;

out vec4 FragClr;
void main()
{
	vec4 tex = texture(gTextureSampler, texCoord);
	FragClr = tex * vertColor * gVerticesColor;
}
