// XClient: the tilted camera of the 3D view. The world plane (DDNet coordinates, y down) is seen by a
// perspective camera that orbits the center of the normal 2D view: pitch raises it (the top of the
// screen goes away), yaw moves it sideways. Without tilt the matrix is exactly the usual 2D mapping,
// so everything DDNet draws stays where it was and only gets the perspective.
#ifndef ENGINE_XC_TILT_H
#define ENGINE_XC_TILT_H

#include <algorithm>
#include <cmath>

namespace XcTilt
{
	struct SCamera
	{
		float m_CenterX, m_CenterY; // the center of the 2D view
		float m_Width, m_Height; // the size of the 2D view at the center
		float m_Pitch, m_Yaw; // radians
		float m_Fov; // vertical, radians
		// first person: the camera stands at this point of the world plane instead of orbiting the center
		bool m_Eye = false;
		float m_EyeX = 0.0f, m_EyeY = 0.0f;
		// side view: flat drawing lands this far behind the world plane
		float m_PlaneZ = 0.0f;
	};

	enum
	{
		FIRST_PERSON_RANGE = 1600, // world units around the eye that are drawn
	};
	constexpr float FIRST_PERSON_NEAR = 10.0f; // closer than this is cut away (your own tee)

	// the camera from a 2D screen rectangle
	inline SCamera FromRect(float x0, float y0, float x1, float y1, float Pitch, float Yaw, float Fov)
	{
		SCamera Cam;
		Cam.m_CenterX = (x0 + x1) * 0.5f;
		Cam.m_CenterY = (y0 + y1) * 0.5f;
		Cam.m_Width = x1 - x0;
		Cam.m_Height = y1 - y0;
		Cam.m_Pitch = Pitch;
		Cam.m_Yaw = Yaw;
		Cam.m_Fov = Fov;
		return Cam;
	}

	// the camera's rotation: world (x, y down, depth) to camera (right, down, forward), rows
	inline void Rotation(const SCamera &Cam, float aaRot[3][3])
	{
		const float ca = std::cos(Cam.m_Pitch), sa = std::sin(Cam.m_Pitch);
		const float cb = std::cos(Cam.m_Yaw), sb = std::sin(Cam.m_Yaw);
		const float aaR[3][3] = {{cb, 0.0f, -sb}, {sb * sa, ca, cb * sa}, {sb * ca, -sa, cb * ca}};
		for(int r = 0; r < 3; r++)
			for(int c = 0; c < 3; c++)
				aaRot[r][c] = aaR[r][c];
	}

	// first person: world (x, y, depth) to clip space with a depth range, column-major
	inline void EyeMatrix(const SCamera &Cam, float *pOut)
	{
		float aaR[3][3];
		Rotation(Cam, aaR);
		const float f = 1.0f / std::tan(Cam.m_Fov * 0.5f);
		const float aScale[3] = {f * Cam.m_Height / Cam.m_Width, -f, 1.0f};
		const float aEye[3] = {Cam.m_EyeX, Cam.m_EyeY, 0.0f};
		float aaRow[4][4] = {};
		for(int r = 0; r < 3; r++)
		{
			const int Dst = r == 2 ? 3 : r; // the forward axis is w
			for(int c = 0; c < 3; c++)
			{
				aaRow[Dst][c] = aScale[r] * aaR[r][c];
				aaRow[Dst][3] -= aScale[r] * aaR[r][c] * aEye[c];
			}
		}
		const float n = FIRST_PERSON_NEAR, fa = 20000.0f;
		const float A = (fa + n) / (fa - n), B = -2.0f * fa * n / (fa - n);
		for(int c = 0; c < 4; c++)
			aaRow[2][c] = A * aaRow[3][c];
		aaRow[2][3] += B;
		for(int r = 0; r < 4; r++)
			for(int c = 0; c < 4; c++)
				pOut[c * 4 + r] = aaRow[r][c];
	}

	// first person: a flat sprite at (X, Y) on the world plane turned to the camera, local coordinates scaled
	inline void BillboardMatrix(const SCamera &Cam, float X, float Y, float ScaleX, float ScaleY, float *pOut)
	{
		float aView[16];
		EyeMatrix(Cam, aView);
		float aaR[3][3];
		Rotation(Cam, aaR);
		// columns: the camera's right and down in the world, nothing for z, the position
		float aB[16] = {};
		for(int i = 0; i < 3; i++)
		{
			aB[0 * 4 + i] = aaR[0][i] * ScaleX;
			aB[1 * 4 + i] = aaR[1][i] * ScaleY;
		}
		aB[3 * 4 + 0] = X;
		aB[3 * 4 + 1] = Y;
		aB[3 * 4 + 3] = 1.0f;
		for(int c = 0; c < 4; c++)
			for(int r = 0; r < 4; r++)
			{
				float Sum = 0.0f;
				for(int k = 0; k < 4; k++)
					Sum += aView[k * 4 + r] * aB[c * 4 + k];
				pOut[c * 4 + r] = Sum;
			}
	}

	// draws nothing: every vertex lands outside the clip volume
	inline void DiscardMatrix(float *pOut)
	{
		for(int i = 0; i < 16; i++)
			pOut[i] = 0.0f;
		pOut[15] = -1.0f;
	}

	inline float Distance(const SCamera &Cam)
	{
		return Cam.m_Height * 0.5f / std::tan(Cam.m_Fov * 0.5f);
	}

	// column-major 4x4 matrix for (x, y, z, 1): z is the depth behind the world plane (away from the
	// camera). WithDepth: a real depth range for depth testing, otherwise z is 0 (2D drawing)
	inline void Matrix(const SCamera &Cam, float *pOut, bool WithDepth)
	{
		if(Cam.m_Eye)
		{
			EyeMatrix(Cam, pOut);
			return;
		}
		const float d = Distance(Cam);
		const float ca = std::cos(Cam.m_Pitch), sa = std::sin(Cam.m_Pitch);
		const float cb = std::cos(Cam.m_Yaw), sb = std::sin(Cam.m_Yaw);
		const float k1 = 2.0f * d / Cam.m_Width, k2 = 2.0f * d / Cam.m_Height;
		const float cx = Cam.m_CenterX, cy = Cam.m_CenterY;
		// camera space: q.x = px*cb - pz*sb, q.y = py*ca + (px*sb + pz*cb)*sa, q.z = -py*sa + (px*sb + pz*cb)*ca
		// rows: clip.x = k1*q.x, clip.y = -k2*q.y, clip.w = q.z + d
		float aRow[4][4] = {
			{k1 * cb, 0.0f, -k1 * sb, 0.0f},
			{-k2 * sb * sa, -k2 * ca, -k2 * cb * sa, 0.0f},
			{0.0f, 0.0f, 0.0f, 0.0f},
			{sb * ca, -sa, cb * ca, d},
		};
		// p = world - center
		for(auto &aR : aRow)
			aR[3] -= aR[0] * cx + aR[1] * cy - aR[2] * Cam.m_PlaneZ;
		if(WithDepth)
		{
			const float n = d * 0.05f, f = d * 40.0f;
			const float A = (f + n) / (f - n), B = -2.0f * f * n / (f - n);
			for(int c = 0; c < 4; c++)
				aRow[2][c] = A * aRow[3][c];
			aRow[2][3] += B;
		}
		for(int r = 0; r < 4; r++)
			for(int c = 0; c < 4; c++)
				pOut[c * 4 + r] = aRow[r][c];
	}

	// the part of the world plane the tilted camera sees, limited to Limit times the 2D view around it
	inline void Bounds(const SCamera &Cam, float Limit, float *pX0, float *pY0, float *pX1, float *pY1)
	{
		const float d = Distance(Cam);
		const float ca = std::cos(Cam.m_Pitch), sa = std::sin(Cam.m_Pitch);
		const float cb = std::cos(Cam.m_Yaw), sb = std::sin(Cam.m_Yaw);
		const float k1 = 2.0f * d / Cam.m_Width, k2 = 2.0f * d / Cam.m_Height;
		const float LimX = Cam.m_Width * Limit * 0.5f, LimY = Cam.m_Height * Limit * 0.5f;
		float MinX = 1e9f, MinY = 1e9f, MaxX = -1e9f, MaxY = -1e9f;
		const auto &&Add = [&](float px, float py) {
			px = std::clamp(px, -LimX, LimX);
			py = std::clamp(py, -LimY, LimY);
			MinX = std::min(MinX, px);
			MaxX = std::max(MaxX, px);
			MinY = std::min(MinY, py);
			MaxY = std::max(MaxY, py);
		};
		// the screen edges at a few points: where the camera ray meets the plane
		for(int i = 0; i <= 8; i++)
		{
			for(int j = 0; j <= 8; j++)
			{
				if(i != 0 && i != 8 && j != 0 && j != 8)
					continue;
				const float u = i / 4.0f - 1.0f, v = j / 4.0f - 1.0f;
				// k1*cb*px = u*w, -k2*(sa*sb*px + ca*py) = v*w, w = d + sb*ca*px - sa*py
				const float a11 = k1 * cb - u * sb * ca, a12 = u * sa, b1 = u * d;
				const float a21 = -k2 * sa * sb - v * sb * ca, a22 = -k2 * ca + v * sa, b2 = v * d;
				const float Det = a11 * a22 - a12 * a21;
				float px, py;
				if(std::abs(Det) < 1e-6f)
				{
					px = u * LimX;
					py = -v * LimY;
				}
				else
				{
					px = (b1 * a22 - a12 * b2) / Det;
					py = (a11 * b2 - b1 * a21) / Det;
					// beyond the horizon: as far as allowed in that direction
					if(d + sb * ca * px - sa * py <= 0.0f)
					{
						px = u * LimX;
						py = -v * LimY;
					}
				}
				Add(px, py);
			}
		}
		*pX0 = Cam.m_CenterX + MinX;
		*pY0 = Cam.m_CenterY + MinY;
		*pX1 = Cam.m_CenterX + MaxX;
		*pY1 = Cam.m_CenterY + MaxY;
	}
} // namespace XcTilt

#endif
