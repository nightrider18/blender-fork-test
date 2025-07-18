/* SPDX-FileCopyrightText: 2023 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup edmesh
 *
 * Tools to implement face building tool,
 * an experimental tool for quickly constructing/manipulating faces.
 */

#include <algorithm>

#include "DNA_object_types.h"

#include "BKE_context.hh"
#include "BKE_editmesh.hh"
#include "BKE_layer.hh"
#include "BKE_object_types.hh"
#include "BKE_screen.hh"

#include "BLI_listbase.h"
#include "BLI_math_geom.h"
#include "BLI_math_matrix.h"
#include "BLI_math_vector.h"

#include "WM_types.hh"

#include "ED_mesh.hh"
#include "ED_object.hh"
#include "ED_transform.hh"
#include "ED_view3d.hh"

#include "bmesh.hh"

#include "mesh_intern.hh" /* own include */

#include "RNA_access.hh"
#include "RNA_define.hh"

#include "WM_api.hh"

#include "DEG_depsgraph.hh"

using blender::Vector;

/* -------------------------------------------------------------------- */
/** \name Local Utilities
 * \{ */

static void edbm_selectmode_ensure(Scene *scene, BMEditMesh *em, short selectmode)
{
  if ((scene->toolsettings->selectmode & selectmode) == 0) {
    scene->toolsettings->selectmode |= selectmode;
    EDBM_selectmode_set(em, scene->toolsettings->selectmode);
  }
}

/* Could make public, for now just keep here. */
static void edbm_flag_disable_all_multi(const Scene *scene,
                                        ViewLayer *view_layer,
                                        View3D *v3d,
                                        const char hflag)
{
  const Vector<Object *> objects = BKE_view_layer_array_from_objects_in_edit_mode_unique_data(
      scene, view_layer, v3d);
  for (Object *ob_iter : objects) {
    BMEditMesh *em_iter = BKE_editmesh_from_object(ob_iter);
    BMesh *bm_iter = em_iter->bm;
    if (bm_iter->totvertsel) {
      EDBM_flag_disable_all(em_iter, hflag);
      DEG_id_tag_update(static_cast<ID *>(ob_iter->data), ID_RECALC_SELECT);
    }
  }
}

/** When accessed as a tool, get the active edge from the pre-selection gizmo. */
static bool edbm_preselect_or_active(bContext *C, const View3D *v3d, Base **r_base, BMElem **r_ele)
{
  ARegion *region = CTX_wm_region(C);
  const bool show_gizmo = !(v3d->gizmo_flag & (V3D_GIZMO_HIDE | V3D_GIZMO_HIDE_TOOL));

  wmGizmoMap *gzmap = show_gizmo ? region->runtime->gizmo_map : nullptr;
  wmGizmoGroup *gzgroup = gzmap ? WM_gizmomap_group_find(gzmap, "VIEW3D_GGT_mesh_preselect_elem") :
                                  nullptr;

  if (gzgroup != nullptr) {
    /* Check the gizmo can draw, if not the state may be stale
     * or if the gizmo group has never drawn the list may even be empty, see: #141336.
     *
     * NOTE(ideasman42): we could also fail with an error in this case,
     * however that would be quite disruptive, so fallback to the active element. */
    if (!WM_gizmo_context_check_drawstep(C, WM_GIZMOMAP_DRAWSTEP_3D)) {
      /* Typically only reached when attempting to use the tool during animation playback. */
      gzgroup = nullptr;
    }
    else if (BLI_listbase_is_empty(&gzgroup->gizmos)) {
      /* If the gizmo group is drawing it *should* never be empty.
       * Even so, avoid crashing if it is - investigate if this is ever reached. */
      BLI_assert(false);
      gzgroup = nullptr;
    }
  }

  if (gzgroup != nullptr) {
    wmGizmo *gz = static_cast<wmGizmo *>(gzgroup->gizmos.first);
    ED_view3d_gizmo_mesh_preselect_get_active(C, gz, r_base, r_ele);
  }
  else {
    const Scene *scene = CTX_data_scene(C);
    ViewLayer *view_layer = CTX_data_view_layer(C);
    BKE_view_layer_synced_ensure(scene, view_layer);
    Base *base = BKE_view_layer_active_base_get(view_layer);
    Object *obedit = base->object;
    BMEditMesh *em = BKE_editmesh_from_object(obedit);
    BMesh *bm = em->bm;
    *r_base = base;
    *r_ele = BM_mesh_active_elem_get(bm);
  }
  return (*r_ele != nullptr);
}

static ViewContext edbm_preselect_or_active_init_viewcontext(bContext *C,
                                                             Base **r_base,
                                                             BMElem **r_ele)
{
  ViewContext vc = em_setup_viewcontext(C);
  bool ok = edbm_preselect_or_active(C, vc.v3d, r_base, r_ele);
  if (ok) {
    ED_view3d_viewcontext_init_object(&vc, (*r_base)->object);
  }
  return vc;
}

static wmOperatorStatus edbm_polybuild_transform_at_cursor_invoke(bContext *C,
                                                                  wmOperator * /*op*/,
                                                                  const wmEvent * /*event*/)
{
  Base *basact = nullptr;
  BMElem *ele_act = nullptr;
  ViewContext vc = edbm_preselect_or_active_init_viewcontext(C, &basact, &ele_act);
  BMEditMesh *em = vc.em;
  BMesh *bm = em->bm;

  invert_m4_m4(vc.obedit->runtime->world_to_object.ptr(), vc.obedit->object_to_world().ptr());
  ED_view3d_init_mats_rv3d(vc.obedit, vc.rv3d);

  if (!ele_act) {
    return OPERATOR_CANCELLED;
  }

  edbm_selectmode_ensure(vc.scene, vc.em, SCE_SELECT_VERTEX);

  edbm_flag_disable_all_multi(vc.scene, vc.view_layer, vc.v3d, BM_ELEM_SELECT);

  if (ele_act->head.htype == BM_VERT) {
    BM_vert_select_set(bm, (BMVert *)ele_act, true);
  }
  if (ele_act->head.htype == BM_EDGE) {
    BM_edge_select_set(bm, (BMEdge *)ele_act, true);
  }
  if (ele_act->head.htype == BM_FACE) {
    BM_face_select_set(bm, (BMFace *)ele_act, true);
  }

  EDBMUpdate_Params params{};
  params.calc_looptris = true;
  params.calc_normals = true;
  params.is_destructive = true;
  EDBM_update(static_cast<Mesh *>(vc.obedit->data), &params);
  if (basact != nullptr) {
    BKE_view_layer_synced_ensure(vc.scene, vc.view_layer);
    if (BKE_view_layer_active_base_get(vc.view_layer) != basact) {
      blender::ed::object::base_activate(C, basact);
    }
  }
  BM_select_history_store(bm, ele_act);
  WM_event_add_mousemove(vc.win);
  return OPERATOR_FINISHED;
}

void MESH_OT_polybuild_transform_at_cursor(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "Poly Build Transform at Cursor";
  ot->idname = "MESH_OT_polybuild_transform_at_cursor";

  /* API callbacks. */
  ot->invoke = edbm_polybuild_transform_at_cursor_invoke;
  ot->poll = EDBM_view3d_poll;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  /* to give to transform */
  blender::ed::transform::properties_register(ot, P_PROPORTIONAL | P_MIRROR_DUMMY);
}

static wmOperatorStatus edbm_polybuild_delete_at_cursor_invoke(bContext *C,
                                                               wmOperator *op,
                                                               const wmEvent * /*event*/)
{
  bool changed = false;
  Base *basact = nullptr;
  BMElem *ele_act = nullptr;
  ViewContext vc = edbm_preselect_or_active_init_viewcontext(C, &basact, &ele_act);
  BMEditMesh *em = vc.em;
  BMesh *bm = em->bm;

  invert_m4_m4(vc.obedit->runtime->world_to_object.ptr(), vc.obedit->object_to_world().ptr());
  ED_view3d_init_mats_rv3d(vc.obedit, vc.rv3d);

  if (!ele_act) {
    return OPERATOR_CANCELLED;
  }

  edbm_selectmode_ensure(vc.scene, vc.em, SCE_SELECT_VERTEX);

  if (ele_act->head.htype == BM_FACE) {
    BMFace *f_act = (BMFace *)ele_act;
    EDBM_flag_disable_all(em, BM_ELEM_TAG);
    BM_elem_flag_enable(f_act, BM_ELEM_TAG);
    if (!EDBM_op_callf(em, op, "delete geom=%hf context=%i", BM_ELEM_TAG, DEL_FACES)) {
      return OPERATOR_CANCELLED;
    }
    changed = true;
  }
  if (ele_act->head.htype == BM_VERT) {
    BMVert *v_act = (BMVert *)ele_act;
    if (BM_vert_is_edge_pair(v_act) && !BM_vert_is_wire(v_act)) {
      BM_edge_collapse(bm, v_act->e, v_act, true, true);
      changed = true;
    }
    else {
      EDBM_flag_disable_all(em, BM_ELEM_TAG);
      BM_elem_flag_enable(v_act, BM_ELEM_TAG);

      if (!EDBM_op_callf(em,
                         op,
                         "dissolve_verts verts=%hv use_face_split=%b use_boundary_tear=%b",
                         BM_ELEM_TAG,
                         false,
                         false))
      {
        return OPERATOR_CANCELLED;
      }
      changed = true;
    }
  }

  if (changed) {
    EDBMUpdate_Params params{};
    params.calc_looptris = true;
    params.calc_normals = true;
    params.is_destructive = true;
    EDBM_update(static_cast<Mesh *>(vc.obedit->data), &params);
    if (basact != nullptr) {
      BKE_view_layer_synced_ensure(vc.scene, vc.view_layer);
      if (BKE_view_layer_active_base_get(vc.view_layer) != basact) {
        blender::ed::object::base_activate(C, basact);
      }
    }
    WM_event_add_mousemove(vc.win);
    return OPERATOR_FINISHED;
  }
  return OPERATOR_CANCELLED;
}

void MESH_OT_polybuild_delete_at_cursor(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "Poly Build Delete at Cursor";
  ot->idname = "MESH_OT_polybuild_delete_at_cursor";

  /* API callbacks. */
  ot->invoke = edbm_polybuild_delete_at_cursor_invoke;
  ot->poll = EDBM_view3d_poll;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  /* to give to transform */
  blender::ed::transform::properties_register(ot, P_PROPORTIONAL | P_MIRROR_DUMMY);
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Face at Cursor
 * \{ */

static wmOperatorStatus edbm_polybuild_face_at_cursor_invoke(bContext *C,
                                                             wmOperator *op,
                                                             const wmEvent *event)
{
  float center[3];
  bool changed = false;

  Base *basact = nullptr;
  BMElem *ele_act = nullptr;
  ViewContext vc = edbm_preselect_or_active_init_viewcontext(C, &basact, &ele_act);
  BMEditMesh *em = vc.em;
  BMesh *bm = em->bm;

  invert_m4_m4(vc.obedit->runtime->world_to_object.ptr(), vc.obedit->object_to_world().ptr());
  ED_view3d_init_mats_rv3d(vc.obedit, vc.rv3d);

  edbm_selectmode_ensure(vc.scene, vc.em, SCE_SELECT_VERTEX);

  if (ele_act == nullptr || ele_act->head.htype == BM_FACE) {
    /* Just add vert */
    copy_v3_v3(center, vc.scene->cursor.location);
    mul_v3_m4v3(center, vc.obedit->object_to_world().ptr(), center);
    ED_view3d_win_to_3d_int(vc.v3d, vc.region, center, event->mval, center);
    mul_m4_v3(vc.obedit->world_to_object().ptr(), center);

    BMVert *v_new = BM_vert_create(bm, center, nullptr, BM_CREATE_NOP);
    edbm_flag_disable_all_multi(vc.scene, vc.view_layer, vc.v3d, BM_ELEM_SELECT);
    BM_vert_select_set(bm, v_new, true);
    BM_select_history_store(bm, v_new);
    changed = true;
  }
  else if (ele_act->head.htype == BM_EDGE) {
    BMEdge *e_act = (BMEdge *)ele_act;
    BMFace *f_reference = e_act->l ? e_act->l->f : nullptr;

    mid_v3_v3v3(center, e_act->v1->co, e_act->v2->co);
    mul_m4_v3(vc.obedit->object_to_world().ptr(), center);
    ED_view3d_win_to_3d_int(vc.v3d, vc.region, center, event->mval, center);
    mul_m4_v3(vc.obedit->world_to_object().ptr(), center);
    if (f_reference->len == 3 && RNA_boolean_get(op->ptr, "create_quads")) {
      const float fac = line_point_factor_v3(center, e_act->v1->co, e_act->v2->co);
      BMVert *v_new = BM_edge_split(bm, e_act, e_act->v1, nullptr, std::clamp(fac, 0.0f, 1.0f));
      copy_v3_v3(v_new->co, center);
      edbm_flag_disable_all_multi(vc.scene, vc.view_layer, vc.v3d, BM_ELEM_SELECT);
      BM_vert_select_set(bm, v_new, true);
      BM_select_history_store(bm, v_new);
    }
    else {
      BMVert *v_tri[3];
      v_tri[0] = e_act->v1;
      v_tri[1] = e_act->v2;
      v_tri[2] = BM_vert_create(bm, center, nullptr, BM_CREATE_NOP);
      if (e_act->l && e_act->l->v == v_tri[0]) {
        std::swap(v_tri[0], v_tri[1]);
      }
      BM_face_create_verts(bm, v_tri, 3, f_reference, BM_CREATE_NOP, true);
      edbm_flag_disable_all_multi(vc.scene, vc.view_layer, vc.v3d, BM_ELEM_SELECT);
      BM_vert_select_set(bm, v_tri[2], true);
      BM_select_history_store(bm, v_tri[2]);
    }
    changed = true;
  }
  else if (ele_act->head.htype == BM_VERT) {
    BMVert *v_act = (BMVert *)ele_act;
    BMEdge *e_pair[2] = {nullptr};

    if (v_act->e != nullptr) {
      for (uint allow_wire = 0; allow_wire < 2 && (e_pair[1] == nullptr); allow_wire++) {
        int i = 0;
        BMEdge *e_iter = v_act->e;
        do {
          if ((BM_elem_flag_test(e_iter, BM_ELEM_HIDDEN) == false) &&
              (allow_wire ? BM_edge_is_wire(e_iter) : BM_edge_is_boundary(e_iter)))
          {
            if (i == 2) {
              e_pair[0] = e_pair[1] = nullptr;
              break;
            }
            e_pair[i++] = e_iter;
          }
        } while ((e_iter = BM_DISK_EDGE_NEXT(e_iter, v_act)) != v_act->e);
      }
    }

    if (e_pair[1] != nullptr) {
      /* Quad from edge pair. */
      if (BM_edge_calc_length_squared(e_pair[0]) < BM_edge_calc_length_squared(e_pair[1])) {
        std::swap(e_pair[0], e_pair[1]);
      }

      BMFace *f_reference = e_pair[0]->l ? e_pair[0]->l->f : nullptr;

      mul_v3_m4v3(center, vc.obedit->object_to_world().ptr(), v_act->co);
      ED_view3d_win_to_3d_int(vc.v3d, vc.region, center, event->mval, center);
      mul_m4_v3(vc.obedit->world_to_object().ptr(), center);

      BMVert *v_quad[4];
      v_quad[0] = v_act;
      v_quad[1] = BM_edge_other_vert(e_pair[0], v_act);
      v_quad[2] = BM_vert_create(bm, center, nullptr, BM_CREATE_NOP);
      v_quad[3] = BM_edge_other_vert(e_pair[1], v_act);
      if (e_pair[0]->l && e_pair[0]->l->v == v_quad[0]) {
        std::swap(v_quad[1], v_quad[3]);
      }
      // BMFace *f_new =
      BM_face_create_verts(bm, v_quad, 4, f_reference, BM_CREATE_NOP, true);

      edbm_flag_disable_all_multi(vc.scene, vc.view_layer, vc.v3d, BM_ELEM_SELECT);
      BM_vert_select_set(bm, v_quad[2], true);
      BM_select_history_store(bm, v_quad[2]);
      changed = true;
    }
    else {
      /* Just add edge */
      mul_m4_v3(vc.obedit->object_to_world().ptr(), center);
      ED_view3d_win_to_3d_int(vc.v3d, vc.region, v_act->co, event->mval, center);
      mul_m4_v3(vc.obedit->world_to_object().ptr(), center);

      BMVert *v_new = BM_vert_create(bm, center, nullptr, BM_CREATE_NOP);

      BM_edge_create(bm, v_act, v_new, nullptr, BM_CREATE_NOP);

      BM_vert_select_set(bm, v_new, true);
      BM_select_history_store(bm, v_new);
      changed = true;
    }
  }

  if (changed) {
    EDBMUpdate_Params params{};
    params.calc_looptris = true;
    params.calc_normals = true;
    params.is_destructive = true;
    EDBM_update(static_cast<Mesh *>(vc.obedit->data), &params);

    if (basact != nullptr) {
      BKE_view_layer_synced_ensure(vc.scene, vc.view_layer);
      if (BKE_view_layer_active_base_get(vc.view_layer) != basact) {
        blender::ed::object::base_activate(C, basact);
      }
    }

    WM_event_add_mousemove(vc.win);

    return OPERATOR_FINISHED;
  }
  return OPERATOR_CANCELLED;
}

void MESH_OT_polybuild_face_at_cursor(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "Poly Build Face at Cursor";
  ot->idname = "MESH_OT_polybuild_face_at_cursor";

  /* API callbacks. */
  ot->invoke = edbm_polybuild_face_at_cursor_invoke;
  ot->poll = EDBM_view3d_poll;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  RNA_def_boolean(ot->srna,
                  "create_quads",
                  true,
                  "Create Quads",
                  "Automatically split edges in triangles to maintain quad topology");
  /* to give to transform */
  blender::ed::transform::properties_register(ot, P_PROPORTIONAL | P_MIRROR_DUMMY);
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Split at Cursor
 * \{ */

static wmOperatorStatus edbm_polybuild_split_at_cursor_invoke(bContext *C,
                                                              wmOperator * /*op*/,
                                                              const wmEvent *event)
{
  float center[3];
  bool changed = false;

  Base *basact = nullptr;
  BMElem *ele_act = nullptr;
  ViewContext vc = edbm_preselect_or_active_init_viewcontext(C, &basact, &ele_act);
  BMEditMesh *em = vc.em;
  BMesh *bm = em->bm;

  invert_m4_m4(vc.obedit->runtime->world_to_object.ptr(), vc.obedit->object_to_world().ptr());
  ED_view3d_init_mats_rv3d(vc.obedit, vc.rv3d);

  edbm_selectmode_ensure(vc.scene, vc.em, SCE_SELECT_VERTEX);

  if (ele_act == nullptr || ele_act->head.hflag == BM_FACE) {
    return OPERATOR_PASS_THROUGH;
  }
  if (ele_act->head.htype == BM_EDGE) {
    BMEdge *e_act = (BMEdge *)ele_act;
    mid_v3_v3v3(center, e_act->v1->co, e_act->v2->co);
    mul_m4_v3(vc.obedit->object_to_world().ptr(), center);
    ED_view3d_win_to_3d_int(vc.v3d, vc.region, center, event->mval, center);
    mul_m4_v3(vc.obedit->world_to_object().ptr(), center);

    const float fac = line_point_factor_v3(center, e_act->v1->co, e_act->v2->co);
    BMVert *v_new = BM_edge_split(bm, e_act, e_act->v1, nullptr, std::clamp(fac, 0.0f, 1.0f));
    copy_v3_v3(v_new->co, center);

    edbm_flag_disable_all_multi(vc.scene, vc.view_layer, vc.v3d, BM_ELEM_SELECT);
    BM_vert_select_set(bm, v_new, true);
    BM_select_history_store(bm, v_new);
    changed = true;
  }
  else if (ele_act->head.htype == BM_VERT) {
    /* Just do nothing, allow dragging. */
    return OPERATOR_FINISHED;
  }

  if (changed) {
    EDBMUpdate_Params params{};
    params.calc_looptris = true;
    params.calc_normals = true;
    params.is_destructive = true;
    EDBM_update(static_cast<Mesh *>(vc.obedit->data), &params);

    WM_event_add_mousemove(vc.win);

    BKE_view_layer_synced_ensure(vc.scene, vc.view_layer);
    if (BKE_view_layer_active_base_get(vc.view_layer) != basact) {
      blender::ed::object::base_activate(C, basact);
    }

    return OPERATOR_FINISHED;
  }
  return OPERATOR_CANCELLED;
}

void MESH_OT_polybuild_split_at_cursor(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "Poly Build Split at Cursor";
  ot->idname = "MESH_OT_polybuild_split_at_cursor";

  /* API callbacks. */
  ot->invoke = edbm_polybuild_split_at_cursor_invoke;
  ot->poll = EDBM_view3d_poll;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  /* to give to transform */
  blender::ed::transform::properties_register(ot, P_PROPORTIONAL | P_MIRROR_DUMMY);
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Dissolve at Cursor
 * \{ */

static wmOperatorStatus edbm_polybuild_dissolve_at_cursor_invoke(bContext *C,
                                                                 wmOperator *op,
                                                                 const wmEvent * /*event*/)
{
  bool changed = false;

  Base *basact = nullptr;
  BMElem *ele_act = nullptr;
  ViewContext vc = edbm_preselect_or_active_init_viewcontext(C, &basact, &ele_act);
  BMEditMesh *em = vc.em;
  BMesh *bm = em->bm;

  if (ele_act == nullptr) {
    /* pass */
  }
  else if (ele_act->head.htype == BM_EDGE) {
    BMEdge *e_act = (BMEdge *)ele_act;
    BMLoop *l_a, *l_b;
    if (BM_edge_loop_pair(e_act, &l_a, &l_b)) {
      BMFace *f_double;
      BMFace *f_new = BM_faces_join_pair(bm, l_a, l_b, true, &f_double);
      /* See #BM_faces_join note on callers asserting when `r_double` is non-null. */
      BLI_assert_msg(f_double == nullptr,
                     "Doubled face detected at " AT ". Resulting mesh may be corrupt.");

      if (f_new) {
        changed = true;
      }
    }
  }
  else if (ele_act->head.htype == BM_VERT) {
    BMVert *v_act = (BMVert *)ele_act;
    if (BM_vert_is_edge_pair(v_act)) {
      BM_edge_collapse(bm, v_act->e, v_act, true, true);
    }
    else {
      /* too involved to do inline */

      /* Avoid using selection so failure won't leave modified state. */
      EDBM_flag_disable_all(em, BM_ELEM_TAG);
      BM_elem_flag_enable(v_act, BM_ELEM_TAG);

      if (!EDBM_op_callf(em,
                         op,
                         "dissolve_verts verts=%hv use_face_split=%b use_boundary_tear=%b",
                         BM_ELEM_TAG,
                         false,
                         false))
      {
        return OPERATOR_CANCELLED;
      }
    }
    changed = true;
  }

  if (changed) {
    edbm_flag_disable_all_multi(vc.scene, vc.view_layer, vc.v3d, BM_ELEM_SELECT);

    EDBMUpdate_Params params{};
    EDBM_update(static_cast<Mesh *>(vc.obedit->data), &params);

    BKE_view_layer_synced_ensure(vc.scene, vc.view_layer);
    if (BKE_view_layer_active_base_get(vc.view_layer) != basact) {
      blender::ed::object::base_activate(C, basact);
    }

    WM_event_add_mousemove(vc.win);

    return OPERATOR_FINISHED;
  }
  return OPERATOR_CANCELLED;
}

void MESH_OT_polybuild_dissolve_at_cursor(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "Poly Build Dissolve at Cursor";
  ot->idname = "MESH_OT_polybuild_dissolve_at_cursor";

  /* API callbacks. */
  ot->invoke = edbm_polybuild_dissolve_at_cursor_invoke;
  ot->poll = EDBM_view3d_poll;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;
}

/** \} */
