
#include "normals.h"
#include "tests.h"
#ifdef WIN32
#define _X86_
#else
#define _AMD64_
#endif
#include <minwindef.h>

int run_visualiser(drawbuff_s* mesh[10]);

static struct drawbuff_s g_drawBuff;


static int vcomp = 0;

static inline int vertexes_compare2(const struct vertexData_s* a, const struct vertexData_s* b)
{
	const float epsilon = 1e-9f;

	vcomp++;

	const float dx = a->xyz[0] - b->xyz[0];
	if (fabsf(dx) < epsilon)
	{
		const float dy = a->xyz[1] - b->xyz[1];
		if (fabsf(dy) < epsilon)
		{
			const float dz = a->xyz[2] - b->xyz[2];
			if (fabsf(dz) < epsilon)
			{
				return 0;
			}
			else return (signbit(dz) ? -1 : 1);
		}
		else return (signbit(dy) ? -1 : 1);
	}
	else return (signbit(dx) ? -1 : 1);
}

static int vertexes_compare(void const* x0, void const* x1)
{
	const struct vertexData_s* a = &g_drawBuff.vertexes[*((uint16_t*)x0)];
	const struct vertexData_s* b = &g_drawBuff.vertexes[*((uint16_t*)x1)];
	int res = vertexes_compare2(a, b);

	if (res == 0)
	{
		if (a < b)
			return -1;
		return 1;
	}
	return res;
}

static inline int IsVectorZero(const vec3_t v)
{
	return (v[0] == 0.f) && (v[1] == 0.f) && (v[2] == 0.f);
}

static inline int IsNormalClose(const struct vertexData_s* a, const struct vertexData_s* b) {
	const float epsilon = 1e-5f;
	float dx = a->normal[0] - b->normal[0];
	float dy = a->normal[1] - b->normal[1];
	float dz = a->normal[2] - b->normal[2];
	return (dx * dx + dy * dy + dz * dz) < (epsilon * epsilon);
}

struct normals_func_s
{
	calculate_normals calc;
	getdrawbuff getdraw;
	struct drawbuff_s* dbuf;
} fn_nor[] =
{
	{ calculate_normals_original, getdrawbuff_original },
	{ calculate_normals_fixed, getdrawbuff_fixed },
	{ calculate_normals_remix, getdrawbuff_remix },
	{ 0 , 0 }
};

void do_normals_test()
{
	memset(&g_drawBuff, 0, sizeof(g_drawBuff));


	for (int i = 0; i < ARRAYSIZE(fn_nor); i++)
	{
		struct normals_func_s* fn = &fn_nor[i];
		if (fn->getdraw)
		{
			fn->dbuf = fn->getdraw();
		}
	}

	std::fstream vfile;
	int count = 0;
	int vcount = 0;
	int maxvindex = -1;


	//vfile.open(PROJDIR "./data/mesh_concrete_pillars.csv", std::ios::in);
	vfile.open(PROJDIR "./data/mesh_wall2.csv", std::ios::in);
	if (vfile.is_open()) {
		std::string sa;
		std::getline(vfile, sa);

		while (std::getline(vfile, sa) && count < MAX_INDEXES && vcount < MAX_VERTEXES) {
			struct vertexData_s vd;
			int index, ord;
			//sscanf_s(sa.c_str(), "%*d,%d,%f,%f,%f,%*f,\"%*[^\"]\",%f,%f", &index, &vd.xyz[0], &vd.xyz[1], &vd.xyz[2], &vd.uv[0], &vd.uv[1]);
			sscanf_s(sa.c_str(), "%d,%d,%f,%f,%f,%*f,", &ord, &index, &vd.xyz[0], &vd.xyz[1], &vd.xyz[2]);
			if (ord == 85 || ord == 164)
			{
				//__debugbreak();
			}
			if (IsVectorZero(g_drawBuff.vertexes[index].xyz))
			{
				memcpy(g_drawBuff.vertexes[index].xyz, vd.xyz, sizeof(vd.xyz));
				vcount++;
				if (index > maxvindex)
					maxvindex = index;
			}
			else if (0 != vertexes_compare2(&g_drawBuff.vertexes[index], &vd))
			{
				assertloop(false && "vertexes_compare2", count);
			}
			g_drawBuff.indexes[count] = index;
			count++;
		}

		g_drawBuff.numIndexes = count;
		g_drawBuff.numVertexes = vcount;

		assert(maxvindex + 1 == vcount);

		// Close the file object.
		vfile.close();
	}
	else {
		assert(false && "failed to open file");
	}

	for (int i = 0; i < g_drawBuff.numVertexes; i++)
	{
		assert(!IsVectorZero(g_drawBuff.vertexes[i].xyz));
	}


	for (int i = 0; i < ARRAYSIZE(fn_nor); i++)
	{
		struct normals_func_s* fn = &fn_nor[i];
		if (fn->calc)
		{
			memcpy(fn->dbuf, &g_drawBuff, sizeof(g_drawBuff));

			fn->calc();
		}
	}

	for (int i = 1; i < ARRAYSIZE(fn_nor); i++)
	{
		struct normals_func_s* fo = &fn_nor[0];
		struct normals_func_s* fn = &fn_nor[i];
		if (fn->dbuf)
		{
			printf("--------------------------------------------\n");
			for (int j = 0; j < min(MAX_VERTEXES, g_drawBuff.numVertexes); j++)
			{
				if (!IsNormalClose(&fo->dbuf->vertexes[j], &fn->dbuf->vertexes[j]))
				{
					printf("k:%d: step:%d %.6f %.6f %.6f\n", 0, j, fo->dbuf->vertexes[j].normal[0], fo->dbuf->vertexes[j].normal[1], fo->dbuf->vertexes[j].normal[2]);
					printf("k:%d: step:%d %.6f %.6f %.6f\n", i, j, fn->dbuf->vertexes[j].normal[0], fn->dbuf->vertexes[j].normal[1], fn->dbuf->vertexes[j].normal[2]);
					printf("--------------------------------------------\n");
				}
			}
		}
	}

	drawbuff_s* mesh[10] = { 0 };
	for (int i = 0; i < ARRAYSIZE(fn_nor); i++)
	{
		struct normals_func_s* fn = &fn_nor[i];
		if (fn->dbuf == 0) break;
		mesh[i] = fn->dbuf;
	}

#if 0
	run_visualiser(mesh);
#endif

	printf("\n");
}
