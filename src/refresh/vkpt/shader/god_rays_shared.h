
layout(set=GOD_RAYS_DESC_SET_IDX, binding=0) uniform sampler2DArray TEX_SHADOW_MAP;

layout(push_constant, std140) uniform PushConstants {
	uint pass_index;
} push;

ivec2 GetRotatedGridOffset(ivec2 pixelPos)
{
	return ivec2(pixelPos.y & 0x1, 1 - (pixelPos.x & 0x1));
}

#define IMG_GODRAYS_INTERMEDIATE IMG_ASVGF_COLOR
#define TEX_GODRAYS_INTERMEDIATE TEX_ASVGF_COLOR
