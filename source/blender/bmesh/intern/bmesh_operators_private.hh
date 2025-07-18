/* SPDX-FileCopyrightText: 2023 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#pragma once

/** \file
 * \ingroup bmesh
 */

struct BMOperator;
struct BMesh;

void bmo_average_vert_facedata_exec(BMesh *bm, BMOperator *op);
void bmo_beautify_fill_exec(BMesh *bm, BMOperator *op);
void bmo_bevel_exec(BMesh *bm, BMOperator *op);
void bmo_bisect_edges_exec(BMesh *bm, BMOperator *op);
void bmo_bisect_plane_exec(BMesh *bm, BMOperator *op);
void bmo_bmesh_to_mesh_exec(BMesh *bm, BMOperator *op);
void bmo_bridge_loops_exec(BMesh *bm, BMOperator *op);
void bmo_collapse_exec(BMesh *bm, BMOperator *op);
void bmo_collapse_uvs_exec(BMesh *bm, BMOperator *op);
void bmo_connect_verts_exec(BMesh *bm, BMOperator *op);
void bmo_connect_verts_concave_exec(BMesh *bm, BMOperator *op);
void bmo_connect_verts_nonplanar_exec(BMesh *bm, BMOperator *op);
void bmo_connect_vert_pair_exec(BMesh *bm, BMOperator *op);
void bmo_contextual_create_exec(BMesh *bm, BMOperator *op);
void bmo_convex_hull_exec(BMesh *bm, BMOperator *op);
void bmo_create_circle_exec(BMesh *bm, BMOperator *op);
void bmo_create_cone_exec(BMesh *bm, BMOperator *op);
void bmo_create_cube_exec(BMesh *bm, BMOperator *op);
void bmo_create_grid_exec(BMesh *bm, BMOperator *op);
void bmo_create_icosphere_exec(BMesh *bm, BMOperator *op);
void bmo_create_monkey_exec(BMesh *bm, BMOperator *op);
void bmo_create_uvsphere_exec(BMesh *bm, BMOperator *op);
void bmo_create_vert_exec(BMesh *bm, BMOperator *op);
void bmo_delete_exec(BMesh *bm, BMOperator *op);
void bmo_dissolve_edges_init(BMOperator *op);
void bmo_dissolve_edges_exec(BMesh *bm, BMOperator *op);
void bmo_dissolve_faces_exec(BMesh *bm, BMOperator *op);
void bmo_dissolve_verts_exec(BMesh *bm, BMOperator *op);
void bmo_dissolve_limit_exec(BMesh *bm, BMOperator *op);
void bmo_dissolve_degenerate_exec(BMesh *bm, BMOperator *op);
void bmo_duplicate_exec(BMesh *bm, BMOperator *op);
void bmo_edgeloop_fill_exec(BMesh *bm, BMOperator *op);
void bmo_face_attribute_fill_exec(BMesh *bm, BMOperator *op);
void bmo_holes_fill_exec(BMesh *bm, BMOperator *op);
void bmo_edgenet_fill_exec(BMesh *bm, BMOperator *op);
void bmo_edgenet_prepare_exec(BMesh *bm, BMOperator *op);
void bmo_extrude_discrete_faces_exec(BMesh *bm, BMOperator *op);
void bmo_extrude_edge_only_exec(BMesh *bm, BMOperator *op);
void bmo_extrude_face_region_exec(BMesh *bm, BMOperator *op);
void bmo_extrude_vert_indiv_exec(BMesh *bm, BMOperator *op);
void bmo_find_doubles_exec(BMesh *bm, BMOperator *op);
void bmo_grid_fill_exec(BMesh *bm, BMOperator *op);
void bmo_inset_individual_exec(BMesh *bm, BMOperator *op);
void bmo_inset_region_exec(BMesh *bm, BMOperator *op);
void bmo_join_triangles_exec(BMesh *bm, BMOperator *op);
void bmo_mesh_to_bmesh_exec(BMesh *bm, BMOperator *op);
void bmo_mirror_exec(BMesh *bm, BMOperator *op);
void bmo_object_load_bmesh_exec(BMesh *bm, BMOperator *op);
void bmo_pointmerge_exec(BMesh *bm, BMOperator *op);
void bmo_pointmerge_facedata_exec(BMesh *bm, BMOperator *op);
void bmo_recalc_face_normals_exec(BMesh *bm, BMOperator *op);
void bmo_poke_exec(BMesh *bm, BMOperator *op);
void bmo_offset_edgeloops_exec(BMesh *bm, BMOperator *op);
void bmo_planar_faces_exec(BMesh *bm, BMOperator *op);
void bmo_region_extend_exec(BMesh *bm, BMOperator *op);
void bmo_remove_doubles_exec(BMesh *bm, BMOperator *op);
void bmo_reverse_colors_exec(BMesh *bm, BMOperator *op);
void bmo_reverse_faces_exec(BMesh *bm, BMOperator *op);
void bmo_reverse_uvs_exec(BMesh *bm, BMOperator *op);
void bmo_rotate_colors_exec(BMesh *bm, BMOperator *op);
void bmo_rotate_edges_exec(BMesh *bm, BMOperator *op);
void bmo_rotate_exec(BMesh *bm, BMOperator *op);
void bmo_rotate_uvs_exec(BMesh *bm, BMOperator *op);
void bmo_scale_exec(BMesh *bm, BMOperator *op);
void bmo_smooth_vert_exec(BMesh *bm, BMOperator *op);
void bmo_smooth_laplacian_vert_exec(BMesh *bm, BMOperator *op);
void bmo_solidify_face_region_exec(BMesh *bm, BMOperator *op);
void bmo_spin_exec(BMesh *bm, BMOperator *op);
void bmo_split_edges_exec(BMesh *bm, BMOperator *op);
void bmo_split_exec(BMesh *bm, BMOperator *op);
void bmo_subdivide_edges_exec(BMesh *bm, BMOperator *op);
void bmo_subdivide_edgering_exec(BMesh *bm, BMOperator *op);
void bmo_symmetrize_exec(BMesh *bm, BMOperator *op);
void bmo_transform_exec(BMesh *bm, BMOperator *op);
void bmo_translate_exec(BMesh *bm, BMOperator *op);
void bmo_triangle_fill_exec(BMesh *bm, BMOperator *op);
void bmo_triangulate_exec(BMesh *bm, BMOperator *op);
void bmo_unsubdivide_exec(BMesh *bm, BMOperator *op);
void bmo_weld_verts_exec(BMesh *bm, BMOperator *op);
void bmo_wireframe_exec(BMesh *bm, BMOperator *op);
void bmo_flip_quad_tessellation_exec(BMesh *bm, BMOperator *op);
