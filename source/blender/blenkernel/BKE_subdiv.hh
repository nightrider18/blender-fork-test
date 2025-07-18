/* SPDX-FileCopyrightText: 2018 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup bke
 */

#pragma once

#include "BLI_compiler_compat.h"
#include "BLI_math_vector_types.hh"

struct Mesh;
struct MultiresModifierData;
struct OpenSubdiv_Converter;
struct OpenSubdiv_Evaluator;
namespace blender::opensubdiv {
class TopologyRefinerImpl;
}

namespace blender::bke::subdiv {

enum VtxBoundaryInterpolation {
  /* Do not interpolate boundaries. */
  SUBDIV_VTX_BOUNDARY_NONE,
  /* Sharpen edges. */
  SUBDIV_VTX_BOUNDARY_EDGE_ONLY,
  /* sharpen edges and corners, */
  SUBDIV_VTX_BOUNDARY_EDGE_AND_CORNER,
};

enum FVarLinearInterpolation {
  SUBDIV_FVAR_LINEAR_INTERPOLATION_NONE,
  SUBDIV_FVAR_LINEAR_INTERPOLATION_CORNERS_ONLY,
  SUBDIV_FVAR_LINEAR_INTERPOLATION_CORNERS_AND_JUNCTIONS,
  SUBDIV_FVAR_LINEAR_INTERPOLATION_CORNERS_JUNCTIONS_AND_CONCAVE,
  SUBDIV_FVAR_LINEAR_INTERPOLATION_BOUNDARIES,
  SUBDIV_FVAR_LINEAR_INTERPOLATION_ALL,
};

struct Settings {
  /* Simple subdivision corresponds to "Simple" option in the interface. When it's enabled, the
   * subdivided mesh is not "smoothed": new vertices are added uniformly on the existing surface.
   *
   * On an OpenSubdiv implementation level this translates to a subdivision scheme:
   * when is_simple is true OSD_SCHEME_BILINEAR is used, otherwise OSD_SCHEME_CATMARK. */
  bool is_simple;

  /* This refers to an adaptive isolation when creating patches for the subdivided surface.
   *
   * When is set to false (aka uniform subdivision) fixed depth of isolation is used, which
   * allows to iteratively add more subdivisions (uniform subdivision level 2 = uniform subdivision
   * level 1 + uniform subdivision level 1). Uniform subdivisions will progressively go to a limit
   * surface.
   *
   * Adaptive isolation generates patches at a limit surface (aka as if infinite number of uniform
   * subdivisions have been applied). This setting allows to have matches normal and tangent space
   * the same independent of number of subdivisions set in modifier settings. */
  bool is_adaptive;

  /* Corresponds to Quality option in modifier settings: higher values means the final surface
   * will be more accurately represented by patches.
   *
   * On an OpenSubdiv implementation level this is an isolation level. */
  int level;

  bool use_creases;

  VtxBoundaryInterpolation vtx_boundary_interpolation;
  FVarLinearInterpolation fvar_linear_interpolation;
};

/* NOTE: Order of enumerators MUST match order of values in SubdivStats. */
enum StatsValue {
  SUBDIV_STATS_TOPOLOGY_REFINER_CREATION_TIME = 0,
  SUBDIV_STATS_SUBDIV_TO_MESH,
  SUBDIV_STATS_SUBDIV_TO_MESH_GEOMETRY,
  SUBDIV_STATS_EVALUATOR_CREATE,
  SUBDIV_STATS_EVALUATOR_REFINE,
  SUBDIV_STATS_SUBDIV_TO_CCG,
  SUBDIV_STATS_SUBDIV_TO_CCG_ELEMENTS,
  SUBDIV_STATS_TOPOLOGY_COMPARE,

  NUM_SUBDIV_STATS_VALUES,
};

struct SubdivStats {
  union {
    struct {
      /* Time spent on creating topology refiner, which includes time
       * spend on conversion from Blender data to OpenSubdiv data, and
       * time spent on topology orientation on OpenSubdiv C-API side. */
      double topology_refiner_creation_time;
      /* Total time spent in blender::bke::subdiv::subdiv_to_mesh(). */
      double subdiv_to_mesh_time;
      /* Geometry (mesh vertices) creation time during SUBDIV_TO_MESH. */
      double subdiv_to_mesh_geometry_time;
      /* Time spent on evaluator creation from topology refiner. */
      double evaluator_creation_time;
      /* Time spent on evaluator->refine(). */
      double evaluator_refine_time;
      /* Total time spent on whole CCG creation. */
      double subdiv_to_ccg_time;
      /* Time spent on CCG elements evaluation/initialization. */
      double subdiv_to_ccg_elements_time;
      /* Time spent on CCG elements evaluation/initialization. */
      double topology_compare_time;
    };
    double values_[NUM_SUBDIV_STATS_VALUES];
  };

  /* Per-value timestamp on when corresponding stats_begin() was
   * called. */
  double begin_timestamp_[NUM_SUBDIV_STATS_VALUES];
};

/* Functor which evaluates displacement at a given (u, v) of given ptex face. */
struct Displacement {
  /* Initialize displacement evaluator.
   *
   * Is called right before evaluation is actually needed. This allows to do
   * some lazy initialization, like allocate evaluator from a main thread but
   * then do actual evaluation from background job. */
  void (*initialize)(Displacement *displacement);

  /* Return displacement which is to be added to the original coordinate.
   *
   * NOTE: This function is supposed to return "continuous" displacement for
   * each pf PTex faces created for special (non-quad) face. This means,
   * if displacement is stored on per-corner manner (like MDisps for multires)
   * this is up the displacement implementation to average boundaries of the
   * displacement grids if needed.
   *
   * Averaging of displacement for vertices created for over coarse vertices
   * and edges is done by subdiv code. */
  void (*eval_displacement)(Displacement *displacement,
                            int ptex_face_index,
                            float u,
                            float v,
                            const float3 &dPdu,
                            const float3 &dPdv,
                            float3 &r_D);

  /* Free the data, not the evaluator itself. */
  void (*free)(Displacement *displacement);

  void *user_data;
};

/* This structure contains everything needed to construct subdivided surface.
 * It does not specify storage, memory layout or anything else.
 * It is possible to create different storage's (like, grid based CPU side
 * buffers, GPU subdivision mesh, CPU side fully qualified mesh) from the same
 * Subdiv structure. */
struct Subdiv {
  /* Settings this subdivision surface is created for.
   *
   * It is read-only after assignment in BKE_subdiv_new_from_FOO(). */
  Settings settings;
  /* Topology refiner includes all the glue logic to feed Blender side
   * topology to OpenSubdiv. It can be shared by both evaluator and GL mesh
   * drawer. */
  blender::opensubdiv::TopologyRefinerImpl *topology_refiner;
  /* CPU side evaluator. */
  OpenSubdiv_Evaluator *evaluator;
  /* Optional displacement evaluator. */
  Displacement *displacement_evaluator;
  /* Statistics for debugging. */
  SubdivStats stats;

  /* Cached values, are not supposed to be accessed directly. */
  struct {
    /* Indexed by base face index, element indicates total number of ptex
     * faces created for preceding base faces. This also stores the final
     * ptex offset (the total number of PTex faces) at the end of the array
     * so that algorithms can compute the number of ptex faces for a given
     * face by computing the delta with the offset for the next face without
     * using a separate data structure, e.g.:
     *
     * const int num_face_ptex_faces = face_ptex_offset[i + 1] - face_ptex_offset[i];
     *
     * In total this array has a size of `num base faces + 1`.
     */
    int *face_ptex_offset;
  } cache_;
};

/* --------------------------------------------------------------------
 * Module.
 */

/* (De)initialize the entire subdivision surface module. */
void init();
void exit();

/* --------------------------------------------------------------------
 * Conversion helpers.
 */

/* NOTE: uv_smooth is eSubsurfUVSmooth. */
FVarLinearInterpolation fvar_interpolation_from_uv_smooth(int uv_smooth);

VtxBoundaryInterpolation vtx_boundary_interpolation_from_subsurf(int boundary_smooth);

/* --------------------------------------------------------------------
 * Statistics.
 */

void stats_init(SubdivStats *stats);

void stats_begin(SubdivStats *stats, StatsValue value);
void stats_end(SubdivStats *stats, StatsValue value);

void stats_reset(SubdivStats *stats, StatsValue value);

void stats_print(const SubdivStats *stats);

/* --------------------------------------------------------------------
 * Settings.
 */

bool settings_equal(const Settings *settings_a, const Settings *settings_b);

/* --------------------------------------------------------------------
 * Construction.
 */

/* Construct new subdivision surface descriptor, from scratch, using given
 * settings and topology. */
Subdiv *new_from_converter(const Settings *settings, OpenSubdiv_Converter *converter);
Subdiv *new_from_mesh(const Settings *settings, const Mesh *mesh);

/* Similar to above, but will not re-create descriptor if it was created for the
 * same settings and topology.
 * If settings or topology did change, the existing descriptor is freed and a
 * new one is created from scratch.
 *
 * NOTE: It is allowed to pass NULL as an existing subdivision surface
 * descriptor. This will create a new descriptor without any extra checks.
 */
Subdiv *update_from_converter(Subdiv *subdiv,
                              const Settings *settings,
                              OpenSubdiv_Converter *converter);
Subdiv *update_from_mesh(Subdiv *subdiv, const Settings *settings, const Mesh *mesh);

void free(Subdiv *subdiv);

/* --------------------------------------------------------------------
 * Displacement API.
 */

void displacement_attach_from_multires(Subdiv *subdiv,
                                       Mesh *mesh,
                                       const MultiresModifierData *mmd);

void displacement_detach(Subdiv *subdiv);

/* --------------------------------------------------------------------
 * Topology helpers.
 */

/* For each element in the array, this stores the total number of ptex faces up to that element,
 * with the total number of ptex faces being the last element in the array. The array is of length
 * `base face count + 1`. */
int *face_ptex_offset_get(Subdiv *subdiv);

/* --------------------------------------------------------------------
 * PTex faces and grids.
 */

/* For a given (ptex_u, ptex_v) within a ptex face get corresponding
 * (grid_u, grid_v) within a grid. */
BLI_INLINE void ptex_face_uv_to_grid_uv(float ptex_u,
                                        float ptex_v,
                                        float *r_grid_u,
                                        float *r_grid_v);

/* Inverse of above. */
BLI_INLINE void grid_uv_to_ptex_face_uv(float grid_u,
                                        float grid_v,
                                        float *r_ptex_u,
                                        float *r_ptex_v);

/* For a given subdivision level (which is NOT refinement level) get size of
 * CCG grid (number of grid points on a side).
 */
BLI_INLINE int grid_size_from_level(int level);

/* Simplified version of mdisp_rot_face_to_crn, only handles quad and
 * works in normalized coordinates.
 *
 * NOTE: Output coordinates are in ptex coordinates. */
BLI_INLINE int rotate_quad_to_corner(float quad_u,
                                     float quad_v,
                                     float *r_corner_u,
                                     float *r_corner_v);

/* Converts (u, v) coordinate from within a grid to a quad coordinate in
 * normalized ptex coordinates. */
BLI_INLINE void rotate_grid_to_quad(
    int corner, float grid_u, float grid_v, float *r_quad_u, float *r_quad_v);

/* Convert Blender edge crease value to OpenSubdiv sharpness. */
BLI_INLINE float crease_to_sharpness(float crease);
BLI_INLINE float sharpness_to_crease(float sharpness);

}  // namespace blender::bke::subdiv

#include "intern/subdiv_inline.hh"  // IWYU pragma: export
