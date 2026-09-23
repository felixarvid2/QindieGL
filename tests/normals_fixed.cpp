
#include "normals.h"

static struct drawbuff_s g_drawBuff;
struct drawbuff_s* getdrawbuff_fixed()
{
	return &g_drawBuff;
}

static inline int h2_vertexes_compare2(const struct vertexData_s* a, const struct vertexData_s* b)
{
	const float epsilon = 1e-9f;

	for (int i = 0; i < 3; i++)
	{
		const float d = a->xyz[i] - b->xyz[i];
		if (d > epsilon) return 1;
		if (d < -epsilon) return -1;
	}

	return 0;
}

static inline int h2_normals_compare2(const struct vertexData_s* a, const struct vertexData_s* b)
{
	const float epsilon = 1e-9f;
#ifdef OLD_VERTEX_CMP
	const float dx = a->normal[0] - b->normal[0];
	if (fabs(dx) < epsilon)
	{
		const float dy = a->normal[1] - b->normal[1];
		if (fabs(dy) < epsilon)
		{
			const float dz = a->normal[2] - b->normal[2];
			if (fabs(dz) < epsilon)
			{
				return 0;
			}
			else return (signbit(dz) ? -1 : 1);
		}
		else return (signbit(dy) ? -1 : 1);
	}
	else return (signbit(dx) ? -1 : 1);
#else
	float dotAB = DotProduct(a->normal, b->normal);

	// 1. Check if they point in the same general direction
	if (dotAB <= 0.0f) {
		return 1;
	}

	// 2. Check for strict colinearity using squared values
	float magSqA = DotProduct(a->normal, a->normal);
	float magSqB = DotProduct(b->normal, b->normal);

	// Mathematically: cos(theta)^2 = (a.b)^2 / (|a|^2 * |b|^2)
	// We rearrange to avoid division: (a.b)^2 >= (|a|^2 * |b|^2) * (1 - epsilon)
	return !((dotAB * dotAB) >= (magSqA * magSqB) * (1.0f - epsilon));
#endif
}

static int h2_vertexes_compare(void const* x0, void const* x1)
{
	const struct vertexData_s* a = &g_drawBuff.vertexes[*((uint16_t*)x0)];
	const struct vertexData_s* b = &g_drawBuff.vertexes[*((uint16_t*)x1)];
#ifdef OLD_VERTEX_CMP
	int res = h2_vertexes_compare2(a, b);

	if (res == 0)
	{
		if (a < b)
			return -1;
		return 1;
	}
	return res;
#else
	if (a->xyz[0] != b->xyz[0]) return (a->xyz[0] < b->xyz[0]) ? -1 : 1;
	if (a->xyz[1] != b->xyz[1]) return (a->xyz[1] < b->xyz[1]) ? -1 : 1;
	if (a->xyz[2] != b->xyz[2]) return (a->xyz[2] < b->xyz[2]) ? -1 : 1;
	return 0;
#endif
}

static inline int IsVertexClose(const struct vertexData_s* a, const struct vertexData_s* b) {
	const float epsilon = 1e-5f;
	float dx = a->xyz[0] - b->xyz[0];
	float dy = a->xyz[1] - b->xyz[1];
	float dz = a->xyz[2] - b->xyz[2];
	return (dx * dx + dy * dy + dz * dz) < (epsilon * epsilon);
}

static inline int IsVectorZero(const vec3_t v)
{
	return (v[0] == 0.f) && (v[1] == 0.f) && (v[2] == 0.f);
}

static inline void MergeNormal(const vec3_t normal, uint index, const uint16_t* remapvpos)
{
	float* ndst;
	if (remapvpos)
		index = remapvpos[index];
	ndst = g_drawBuff.vertexes[index].normal;

	VectorAdd(normal, ndst, ndst);
	VectorNormalize(ndst);
}

static float g_normals_angleval = 0.48f;

void calculate_normals_fixed()
{
	static uint16_t sortidx[MAX_VERTEXES];
	static uint16_t remapvpos[MAX_VERTEXES];
	static uint16_t dupvpos[2 * MAX_VERTEXES];

	for (int i = 0; i < g_drawBuff.numVertexes; i++)
	{
		sortidx[i] = i;
		remapvpos[i] = i;
	}

	if (1)
	{
		qsort(sortidx, g_drawBuff.numVertexes, sizeof(uint16_t), h2_vertexes_compare);

		uint16_t* it = dupvpos;
		int count = 0;
		int backi = sortidx[0];
		const struct vertexData_s* backv = &g_drawBuff.vertexes[backi];
		for (int i = 1; i < g_drawBuff.numVertexes; i++)
		{
			int fronti = sortidx[i];
			const struct vertexData_s* frontv = &g_drawBuff.vertexes[fronti];
#ifdef OLD_VERTEX_CMP
			if (0 == h2_vertexes_compare2(backv, frontv))
#else
			if (IsVertexClose(backv, frontv))
#endif
			{
				remapvpos[fronti] = backi;
				if (count == 0)
				{
					count++;
					it[count] = backi;
				}
				count++;
				it[count] = fronti;
			}
			else
			{
				backv = frontv;
				backi = fronti;
				if (count)
				{
					it[0] = count;
					it += count + 1;
					count = 0;
				}
			}
		}
		it[0] = count;
		it[count + 1] = 0;
	}

	struct vertexData_s* v = g_drawBuff.vertexes;
	for (int i = 0; i < g_drawBuff.numVertexes; i++, v++)
	{
		VectorClear(v->normal);
	}

	for (int i = 0; i < g_drawBuff.numIndexes; i += 3)
	{
		uint i0 = g_drawBuff.indexes[i];
		uint i1 = g_drawBuff.indexes[i + 1];
		uint i2 = g_drawBuff.indexes[i + 2];

		const float* v0 = g_drawBuff.vertexes[i0].xyz;
		const float* v1 = g_drawBuff.vertexes[i1].xyz;
		const float* v2 = g_drawBuff.vertexes[i2].xyz;

		vec3_t e1, e2, normal;
		VectorSubtract(v0, v1, e1);
		VectorSubtract(v2, v1, e2);
		CrossProduct(e1, e2, normal);
		VectorNormalize(normal);

		const uint16_t* param = 1 ? NULL : remapvpos;

		MergeNormal(normal, i0, param);
		MergeNormal(normal, i1, param);
		MergeNormal(normal, i2, param);
	}

	uint16_t* it = dupvpos;
	while (it[0])
	{
		int total_count = it[0];
		it++; // 'it' now points to the array of vertex indices

		// Graceful bucketing: process in chunks of max 32
		for (int chunk_start = 0; chunk_start < total_count; chunk_start += 32)
		{
			int count = total_count - chunk_start;
			if (count > 32) {
				if (chunk_start == 0) {
					// Log the error only on the first split
					//PRINT_ONCE("WARNING: Vertex share count %d exceeds 32. Splitting into buckets.\n", total_count);
				}
				count = 32;
			}

			uint16_t* chunk_it = it + chunk_start;

			uint32_t visited = 0;
			uint32_t visitend = (1 << count) - 1;
			uint32_t skipadd = 0;

			//mark duplicated normals
			for (int i = 0; i < count; i++)
			{
				int indexi = chunk_it[i];
				const struct vertexData_s* backv = &g_drawBuff.vertexes[indexi];
				for (int j = i + 1; j < count; j++)
				{
					int indexj = chunk_it[j];
					const struct vertexData_s* frontv = &g_drawBuff.vertexes[indexj];
					if (0 == h2_normals_compare2(backv, frontv))
					{
						skipadd |= 1 << j;
					}
				}
			}

			//go over face combinations, check for angle of surfaces and merge normals
			for (int i = 0; i < count && visited != visitend; i++)
			{
				vec3_t sum;
				uint32_t visitnow = 0;
				int indexi = chunk_it[i];
				visited |= 1 << i;
				float* ni = g_drawBuff.vertexes[indexi].normal;
				//VectorNormalize(ni);
				VectorCopy(ni, sum);
				for (int j = i + 1; j < count; j++)
				{
					uint32_t visitid = 1 << j;
					if (visitid & visited)
						continue;

					int indexj = chunk_it[j];
					float* nj = g_drawBuff.vertexes[indexj].normal;
					//VectorNormalize(nj);
					float dot = DotProduct(nj, ni);
					if (dot > g_normals_angleval)
					{
						if ((skipadd & visitid) == 0)
							VectorAdd(nj, sum, sum);
						//remapvpos[indexj] = indexi;
						visitnow |= visitid;
					}
					else if (remapvpos[indexj] == indexi)
					{
						//remapvpos[indexj] = indexj;
					}
				}

				VectorNormalize(sum);
				VectorCopy(sum, ni);
				for (int j = i + 1; j < count; j++)
				{
					uint32_t visitid = 1 << j;
					if (visitid & visitnow)
					{
						int indexj = chunk_it[j];
						float* nj = g_drawBuff.vertexes[indexj].normal;
						VectorCopy(sum, nj);
					}
				}

				visited |= visitnow;
			}
		}
		it += total_count; // Advance the main pointer by the total cluster size
	}
}