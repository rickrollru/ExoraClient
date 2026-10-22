uniform sampler2D gTextureSampler;

in vec2 texCoord;
in vec4 vertColor;

out vec4 FragClr;
void main()
{
#ifdef TW_TEXTURED
	vec4 tex = texture(gTextureSampler, texCoord);
	FragClr = tex * vertColor;
#else
	FragClr = vertColor;
#endif
}
