// ======================================================================== //
// Copyright 2009-2014 Intel Corporation                                    //
//                                                                          //
// Licensed under the Apache License, Version 2.0 (the "License");          //
// you may not use this file except in compliance with the License.         //
// You may obtain a copy of the License at                                  //
//                                                                          //
//     http://www.apache.org/licenses/LICENSE-2.0                           //
//                                                                          //
// Unless required by applicable law or agreed to in writing, software      //
// distributed under the License is distributed on an "AS IS" BASIS,        //
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. //
// See the License for the specific language governing permissions and      //
// limitations under the License.                                           //
// ======================================================================== //

#pragma once

#include "common/geometry.h"

// lots of code shared between xeon and xeon phi right now

namespace embree
{

#define MAX_VALENCE 16

  struct __aligned(64) FinalQuad
  {
    Vec3fa vtx[4];
    Vec2f uv[2];
    unsigned int geomID;
    unsigned int primID;
  };

  struct __aligned(64) CatmullClark1Ring
  {
    Vec3fa vtx;
    Vec3fa ring[2*MAX_VALENCE]; // two vertices per face
    unsigned int valence;
    unsigned int num_vtx;

    CatmullClark1Ring() {}
    
    __forceinline void init(const SubdivMesh::HalfEdge *const h,
			    const Vec3fa *const vertices)
    {
      size_t i=0;
      vtx = vertices[ h->getStartVertexIndex() ];
      SubdivMesh::HalfEdge *p = (SubdivMesh::HalfEdge*)h;
      do {
	p = p->opposite();
	ring[i++] = vertices[ p->getStartVertexIndex() ];
	ring[i++] = vertices[ p->prev()->getStartVertexIndex() ];

	/*! continue with next adjacent edge. */
	p = p->next();
      } while( p != h);
      num_vtx = i;
      valence = i >> 1;
      assert( i+1 < MAX_VALENCE );
    }


    __forceinline void update(CatmullClark1Ring &dest) const
    {
      dest.valence = valence;
      dest.num_vtx = num_vtx;
      // new face vtx
      Vec3fa avg_faces(0.0f,0.0f,0.0f);
      Vec3fa avg_edges(0.0f,0.0f,0.0f);

      for (size_t i=0;i<valence-1;i++)
	{
	  const Vec3fa new_face = (vtx + ring[2*i] + ring[2*i+1] + ring[2*i+2]) * 0.25f;
	  avg_faces += new_face;
	  avg_edges += (vtx + ring[2*i]) * 0.5f;
	  dest.ring[2*i + 1] = new_face;
	}
      {
        const Vec3fa new_face = (vtx + ring[num_vtx-2] + ring[num_vtx-1] + ring[0]) * 0.25f;
        avg_faces += new_face;
        avg_edges += (vtx + ring[num_vtx-2]) * 0.5f;
        dest.ring[num_vtx-1] = new_face;
      }
      // new edge vertices
      for (size_t i=1;i<valence;i++)
	{
	  const Vec3fa new_edge = (vtx + ring[2*i] + dest.ring[2*i-1] + dest.ring[2*i+1]) * 0.25f;
	  dest.ring[2*i + 0] = new_edge;
	}
      dest.ring[0] = (vtx + ring[0] + dest.ring[num_vtx-1] + dest.ring[1]) * 0.25f;

      // new vtx
      const float inv_valence = 1.0f / (float)valence;

      avg_faces *= inv_valence;
      avg_edges *= inv_valence; 
      
      dest.vtx = (avg_faces + 2.0f * avg_edges + (float)(valence-3)*vtx) * inv_valence;
    }

  };

  __forceinline std::ostream &operator<<(std::ostream &o, const CatmullClark1Ring &c)
    {
      o << "vtx " << c.vtx << " valence " << c.valence << " num_vtx " << c.num_vtx << " ring: " << std::endl;
      for (size_t i=0;i<c.num_vtx;i++)
	o << i << " -> " << c.ring[i] << std::endl;
      return o;
    } 



  class __aligned(64) IrregularCatmullClarkPatch
  {
  public:
    CatmullClark1Ring ring[4];

    unsigned int geomID;
    unsigned int primID;

    static __forceinline void init_regular(const CatmullClark1Ring &p0,
					   const CatmullClark1Ring &p1,
					   CatmullClark1Ring &dest0,
					   CatmullClark1Ring &dest1) 
    {
      dest0.valence = 4;
      dest0.num_vtx = 8;
      dest0.vtx     = p0.ring[0];
      dest1.valence = 4;
      dest1.num_vtx = 8;
      dest1.vtx     = p0.ring[0];

      // 1-ring for patch0
      dest0.ring[ 0] = p0.ring[p0.num_vtx-1];
      dest0.ring[ 1] = p1.ring[0];
      dest0.ring[ 2] = p1.vtx;
      dest0.ring[ 3] = p1.ring[p1.num_vtx-4];
      dest0.ring[ 4] = p0.ring[1];
      dest0.ring[ 5] = p0.ring[2];
      dest0.ring[ 6] = p0.vtx;
      dest0.ring[ 7] = p0.ring[p0.num_vtx-2];
      // 1-ring for patch1
      dest1.ring[ 0] = p1.vtx;
      dest1.ring[ 1] = p1.ring[p1.num_vtx-4];
      dest1.ring[ 2] = p0.ring[1];
      dest1.ring[ 3] = p0.ring[2];
      dest1.ring[ 4] = p0.vtx;
      dest1.ring[ 5] = p0.ring[p0.num_vtx-2];
      dest1.ring[ 6] = p0.ring[p0.num_vtx-1];
      dest1.ring[ 7] = p1.ring[0];
    }

    static __forceinline void init_regular(const Vec3fa &center, const Vec3fa center_ring[8], const size_t offset, CatmullClark1Ring &dest)
    {
      dest.valence = 4;
      dest.num_vtx = 8;
      dest.vtx     = center;
      for (size_t i=0;i<8;i++)
	dest.ring[i] = center_ring[(offset+i)%8];
    }
 

    __forceinline void subdivide(IrregularCatmullClarkPatch patch[4]) const
    {
      ring[0].update(patch[0].ring[0]);
      ring[1].update(patch[1].ring[1]);
      ring[2].update(patch[2].ring[2]);
      ring[3].update(patch[3].ring[3]);




      init_regular(patch[0].ring[0],patch[1].ring[1],patch[0].ring[1],patch[1].ring[0]);
      init_regular(patch[1].ring[1],patch[2].ring[2],patch[1].ring[2],patch[2].ring[1]);
      init_regular(patch[2].ring[2],patch[3].ring[3],patch[2].ring[3],patch[3].ring[2]);
      init_regular(patch[3].ring[3],patch[0].ring[0],patch[3].ring[0],patch[0].ring[3]);

      __aligned(64) Vec3fa center = (ring[0].vtx + ring[1].vtx + ring[2].vtx + ring[3].vtx) * 0.25f;
      __aligned(64) Vec3fa center_ring[8];

      // counter-clockwise
      center_ring[0] = patch[3].ring[3].ring[0];
      center_ring[1] = patch[3].ring[3].vtx;
      center_ring[2] = patch[2].ring[2].ring[0];
      center_ring[3] = patch[2].ring[2].vtx;
      center_ring[4] = patch[1].ring[1].ring[0];
      center_ring[5] = patch[1].ring[1].vtx;
      center_ring[6] = patch[0].ring[0].ring[0];
      center_ring[7] = patch[0].ring[0].vtx;

      init_regular(center,center_ring,0,patch[0].ring[2]);
      init_regular(center,center_ring,2,patch[3].ring[1]);
      init_regular(center,center_ring,4,patch[2].ring[0]);
      init_regular(center,center_ring,6,patch[1].ring[3]);
    }

    __forceinline void init( FinalQuad& quad ) const
    {
      quad.vtx[0] = ring[0].vtx;
      quad.vtx[1] = ring[1].vtx;
      quad.vtx[2] = ring[2].vtx;
      quad.vtx[3] = ring[3].vtx;
      // uv[0] = 
      // uv[1] = 
      quad.geomID = geomID;
      quad.primID = primID;
    };

  };


  __forceinline std::ostream &operator<<(std::ostream &o, const IrregularCatmullClarkPatch &p)
    {
      o << "geomID " << p.geomID << " primID " << p.primID << "rings: " << std::endl;
      for (size_t i=0;i<4;i++)
	o << i << " -> " << p.ring[i] << std::endl;
      return o;
    } 


  template<typename T>
    class RegularCatmullClarkPatchT
    {
    public:
      T v[4][4];

      __forceinline T computeFaceVertex(const unsigned int y,const unsigned int x) const
      {
	return (v[y][x] + v[y][x+1] + v[y+1][x+1] + v[y+1][x]) * 0.25f;
      }

      __forceinline T computeQuadVertex(const unsigned int y,
					const unsigned int x,
					const T face[3][3]) const
      {
	const T P = v[y][x]; 
	const T Q = face[y-1][x-1] + face[y-1][x] + face[y][x] + face[y][x-1];
	const T R = v[y-1][x] + v[y+1][x] + v[y][x-1] + v[y][x+1];
	const T res = (Q + R) * 0.0625f + P * 0.5f;
	return res;
      }

      __forceinline T computeLimitVertex(const int y,
				   const int x) const
      {
	const T P = v[y][x];
	const T Q = v[y-1][x-1] + v[y-1][x+1] + v[y+1][x-1] + v[y+1][x+1];
	const T R = v[y-1][x] + v[y+1][x] + v[y][x-1] + v[y][x+1];
	const T res = (P * 16.0f + R * 4.0f + Q) * 1.0f / 36.0f;
	return res;
      }

      __forceinline T computeLimitNormal(const int y,
				   const int x) const
      {
	/* --- tangent X --- */
	const T Qx = v[y-1][x+1] - v[y-1][x-1] + v[y+1][x+1] - v[y+1][x-1];
	const T Rx = v[y][x-1] - v[y][x+1];
	const T tangentX = (Rx * 4.0f + Qx) * 1.0f / 12.0f;

	/* --- tangent Y --- */
	const T Qy = v[y-1][x-1] - v[y+1][x-1] + v[y-1][x+1] - v[y+1][x+1];
	const T Ry = v[y-1][x] - v[y+1][x];
	const T tangentY = (Ry * 4.0f + Qy) * 1.0f / 12.0f;
    
	return lnormal_xyz(tangentX,tangentY);
      }

      __forceinline void initSubPatches(const T edge[12],
				  const T face[3][3],
				  const T newQuadVertex[2][2],
				  RegularCatmullClarkPatchT child[4]) const
      {
	GeneralCatmullClarkRegularPatch &subTL = child[0];
	GeneralCatmullClarkRegularPatch &subTR = child[1];
	GeneralCatmullClarkRegularPatch &subBR = child[2];
	GeneralCatmullClarkRegularPatch &subBL = child[3];

	// top-left
	subTL.v[0][0] = face[0][0];
	subTL.v[0][1] = edge[0];
	subTL.v[0][2] = face[0][1];
	subTL.v[0][3] = edge[1];

	subTL.v[1][0] = edge[2];
	subTL.v[1][1] = newQuadVertex[0][0];
	subTL.v[1][2] = edge[3];
	subTL.v[1][3] = newQuadVertex[0][1];

	subTL.v[2][0] = face[1][0];
	subTL.v[2][1] = edge[5];
	subTL.v[2][2] = face[1][1];
	subTL.v[2][3] = edge[6];

	subTL.v[3][0] = edge[7];
	subTL.v[3][1] = newQuadVertex[1][0];
	subTL.v[3][2] = edge[8];
	subTL.v[3][3] = newQuadVertex[1][1];

	// top-right
	subTR.v[0][0] = edge[0];
	subTR.v[0][1] = face[0][1];
	subTR.v[0][2] = edge[1];
	subTR.v[0][3] = face[0][2];

	subTR.v[1][0] = newQuadVertex[0][0];
	subTR.v[1][1] = edge[3];
	subTR.v[1][2] = newQuadVertex[0][1];
	subTR.v[1][3] = edge[4];

	subTR.v[2][0] = edge[5];
	subTR.v[2][1] = face[1][1];
	subTR.v[2][2] = edge[6];
	subTR.v[2][3] = face[1][2];

	subTR.v[3][0] = newQuadVertex[1][0];
	subTR.v[3][1] = edge[8];
	subTR.v[3][2] = newQuadVertex[1][1];
	subTR.v[3][3] = edge[9];

	// buttom-right
	subBR.v[0][0] = newQuadVertex[0][0];
	subBR.v[0][1] = edge[3];
	subBR.v[0][2] = newQuadVertex[0][1];
	subBR.v[0][3] = edge[4];

	subBR.v[1][0] = edge[5];
	subBR.v[1][1] = face[1][1];
	subBR.v[1][2] = edge[6];
	subBR.v[1][3] = face[1][2];

	subBR.v[2][0] = newQuadVertex[1][0];
	subBR.v[2][1] = edge[8];
	subBR.v[2][2] = newQuadVertex[1][1];
	subBR.v[2][3] = edge[9];

	subBR.v[3][0] = edge[10];
	subBR.v[3][1] = face[2][1];
	subBR.v[3][2] = edge[11];
	subBR.v[3][3] = face[2][2];

	// buttom-left
	subBL.v[0][0] = edge[2];
	subBL.v[0][1] = newQuadVertex[0][0];
	subBL.v[0][2] = edge[3];
	subBL.v[0][3] = newQuadVertex[0][1];

	subBL.v[1][0] = face[1][0];
	subBL.v[1][1] = edge[5];
	subBL.v[1][2] = face[1][1];
	subBL.v[1][3] = edge[6];

	subBL.v[2][0] = edge[7];
	subBL.v[2][1] = newQuadVertex[1][0];
	subBL.v[2][2] = edge[8];
	subBL.v[2][3] = newQuadVertex[1][1];

	subBL.v[3][0] = face[2][0];
	subBL.v[3][1] = edge[10];
	subBL.v[3][2] = face[2][1];
	subBL.v[3][3] = edge[11];
      }

      __forceinline void subdivide(RegularCatmullClarkPatchT child[4]) const
      {
	T face[3][3];
	face[0][0] = computeFaceVertex(0,0);
	face[0][1] = computeFaceVertex(0,1);
	face[0][2] = computeFaceVertex(0,2);
	face[1][0] = computeFaceVertex(1,0);
	face[1][1] = computeFaceVertex(1,1);
	face[1][2] = computeFaceVertex(1,2);
	face[2][0] = computeFaceVertex(2,0);
	face[2][1] = computeFaceVertex(2,1);
	face[2][2] = computeFaceVertex(2,2);

	T edge[12];
	edge[0]  = 0.25f * (v[0][1] + v[1][1] + face[0][0] + face[0][1]);
	edge[1]  = 0.25f * (v[0][2] + v[1][2] + face[0][1] + face[0][2]);
	edge[2]  = 0.25f * (v[1][0] + v[1][1] + face[0][0] + face[1][0]);
	edge[3]  = 0.25f * (v[1][1] + v[1][2] + face[0][1] + face[1][1]);
	edge[4]  = 0.25f * (v[1][2] + v[1][3] + face[0][2] + face[1][2]);
	edge[5]  = 0.25f * (v[1][1] + v[2][1] + face[1][0] + face[1][1]);
	edge[6]  = 0.25f * (v[1][2] + v[2][2] + face[1][1] + face[1][2]);
	edge[7]  = 0.25f * (v[2][0] + v[2][1] + face[1][0] + face[2][0]);
	edge[8]  = 0.25f * (v[2][1] + v[2][2] + face[1][1] + face[2][1]);
	edge[9]  = 0.25f * (v[2][2] + v[2][3] + face[1][2] + face[2][2]);
	edge[10] = 0.25f * (v[2][1] + v[3][1] + face[2][0] + face[2][1]);
	edge[11] = 0.25f * (v[2][2] + v[3][2] + face[2][1] + face[2][2]);

	T newQuadVertex[2][2];
	newQuadVertex[0][0] = computeQuadVertex(1,1,face);
	newQuadVertex[0][1] = computeQuadVertex(1,2,face);
	newQuadVertex[1][1] = computeQuadVertex(2,2,face);
	newQuadVertex[1][0] = computeQuadVertex(2,1,face);

	initSubPatches(edge,face,newQuadVertex,child);
      }
    };

  class RegularCatmullClarkPatch : public RegularCatmullClarkPatchT<Vec3fa> 
  {
  public:
    
  };



};

