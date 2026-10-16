#ifndef QINDIEGL_D3D_NORMALS_H
#define QINDIEGL_D3D_NORMALS_H

#include "d3d_wrapper.hpp"
#include <vector>

// Smooth vertex normal generation for draws that supply no normals (per-game option "smoothnormals").

// True when the current draw should get generated normals: option on, triangle type primitive with
// at least 3 vertices, not an ortho/2D draw and the draw is a model/entity draw (see .cpp for details).
bool D3DNormals_ShouldGenerate( GLenum mode, int vertexCount );

// Converts the sequence of 'count' submitted vertices of primitive 'mode' to a triangle list.
// Output entries are positions in the submitted sequence (0..count-1). Quads are split (0,1,2)(0,2,3).
void D3DNormals_BuildTriangleList( GLenum mode, int count, std::vector<unsigned int> &triangles );

// positions: vertexCount * 3 floats, triangles: triangle list indexing into positions.
// out receives vertexCount * 3 floats (unit length normals). Coincident vertices (welded by position)
// share the accumulated, area weighted normals of faces within the crease angle.
void D3DNormals_Compute( const float *positions, int vertexCount,
	const unsigned int *triangles, int indexCount, std::vector<float> &out );

// statistics; logs a summary roughly once per second when the option is enabled
void D3DNormals_RecordDraw( GLenum mode, int vertexCount );
void D3DNormals_FrameEnded();

#endif //QINDIEGL_D3D_NORMALS_H
