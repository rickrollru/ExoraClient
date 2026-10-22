#ifdef TW_TEXTURED
uniform sampler2D gTextureSampler;
#endif

uniform vec4 gVerticesColor;

in vec2 texCoord;
in vec4 vertColor;

out vec4 FragClr;
void main()
{
#ifdef TW_TEXTURED
	vec4 tex = texture(gTextureSampler, texCoord);
	FragClr = tex * vertColor * gVerticesColor;
#else
	FragClr = vertColor * gVerticesColor;
#endif
}
