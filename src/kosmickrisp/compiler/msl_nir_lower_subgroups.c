/*
 * Copyright 2023 Valve Corporation
 * Copyright 2025 LunarG, Inc.
 * Copyright 2025 Google LLC
 * SPDX-License-Identifier: MIT
 */

#include "msl_private.h"
#include "nir_to_msl.h"
#include "nir.h"
#include "nir_builder.h"

static bool
needs_bool_widening(nir_intrinsic_instr *intrin)
{
   switch (intrin->intrinsic) {
   case nir_intrinsic_read_invocation:
   case nir_intrinsic_read_first_invocation:
   case nir_intrinsic_reduce:
   case nir_intrinsic_quad_broadcast:
   case nir_intrinsic_quad_swap_horizontal:
   case nir_intrinsic_quad_swap_vertical:
   case nir_intrinsic_quad_swap_diagonal:
   case nir_intrinsic_rotate:
   case nir_intrinsic_shuffle:
   case nir_intrinsic_shuffle_down:
   case nir_intrinsic_shuffle_up:
   case nir_intrinsic_shuffle_xor:
      return true;
   default:
      return false;
   }
}

static bool
lower_bool_ops(nir_builder *b, nir_intrinsic_instr *intrin, void *_unused)
{
   if (!needs_bool_widening(intrin))
      return false;

   if (intrin->def.bit_size != 1)
      return false;

   b->cursor = nir_before_instr(&intrin->instr);
   nir_def *widen = nir_b2i32(b, intrin->src[0].ssa);
   nir_src_rewrite(&intrin->src[0], widen);
   intrin->def.bit_size = 32;
   b->cursor = nir_after_instr(&intrin->instr);
   nir_def *narrow = nir_b2b1(b, &intrin->def);
   nir_def_rewrite_uses_after(&intrin->def, narrow);

   return true;
}

/* Keep 32-bit integer reductions over the whole subgroup (cluster size 0 or
 * at least the subgroup size) as native simd_* operations instead of lowering
 * them to shuffle trees. nir_to_msl ignores the cluster size of the kept
 * reductions, so smaller clusters must be lowered here.
 *
 * Float reductions stay lowered because the Apple GPU backend miscompiles
 * repeated float/half simd_* reductions across divergent control flow
 * (reusing a dominating unconditional reduction inside a divergent branch,
 * failing dEQP subgroup arithmetic tests). 8, 16 and 64-bit reductions
 * stay lowered because they are untested.
 */
static bool
lower_subgroups_filter(const nir_intrinsic_instr *intrin, const void *data)
{
   const nir_lower_subgroups_options *options = data;

   if (intrin->intrinsic != nir_intrinsic_reduce)
      return true;

   unsigned cluster_size = nir_intrinsic_cluster_size(intrin);
   if (intrin->def.bit_size != 32 ||
       (cluster_size != 0 && cluster_size < options->subgroup_size))
      return true;

   switch (nir_intrinsic_reduction_op(intrin)) {
   case nir_op_iadd:
   case nir_op_imul:
   case nir_op_imin:
   case nir_op_umin:
   case nir_op_imax:
   case nir_op_umax:
   case nir_op_iand:
   case nir_op_ior:
   case nir_op_ixor:
      return false;
   default:
      return true;
   }
}

static bool
lower_vs_subgroup_intrin(nir_builder *b, nir_intrinsic_instr *intrin,
                         void *_unused)
{
   b->cursor = nir_before_instr(&intrin->instr);
   nir_def *replacement = NULL;

   switch (intrin->intrinsic) {
   case nir_intrinsic_load_subgroup_size:
      replacement = nir_imm_int(b, 1);
      break;
   case nir_intrinsic_load_subgroup_invocation:
   case nir_intrinsic_load_subgroup_id:
      replacement = nir_imm_int(b, 0);
      break;
   case nir_intrinsic_load_num_subgroups:
      replacement = nir_imm_int(b, 1);
      break;
   case nir_intrinsic_load_subgroup_eq_mask:
   case nir_intrinsic_load_subgroup_ge_mask:
   case nir_intrinsic_load_subgroup_le_mask: {
      unsigned comps = intrin->def.num_components;
      unsigned bs = intrin->def.bit_size;
      nir_const_value vals[4] = {0};
      vals[0] = nir_const_value_for_uint(1, bs);
      replacement = nir_build_imm(b, comps, bs, vals);
      break;
   }
   case nir_intrinsic_load_subgroup_gt_mask:
   case nir_intrinsic_load_subgroup_lt_mask:
      replacement =
         nir_imm_zero(b, intrin->def.num_components, intrin->def.bit_size);
      break;
   case nir_intrinsic_elect:
      replacement = nir_imm_true(b);
      break;
   case nir_intrinsic_ballot: {
      unsigned comps = intrin->def.num_components;
      unsigned bs = intrin->def.bit_size;
      nir_def *bit0 = nir_b2iN(b, intrin->src[0].ssa, bs);
      if (comps == 1) {
         replacement = bit0;
      } else {
         nir_def *z = nir_imm_intN_t(b, 0, bs);
         nir_def *vec[4] = {bit0, z, z, z};
         replacement = nir_vec(b, vec, comps);
      }
      break;
   }
   case nir_intrinsic_ballot_bit_count_reduce:
   case nir_intrinsic_ballot_bit_count_inclusive: {
      nir_def *x = nir_channel(b, intrin->src[0].ssa, 0);
      replacement = nir_u2u32(b, nir_iand_imm(b, x, 1));
      break;
   }
   case nir_intrinsic_ballot_bit_count_exclusive:
   case nir_intrinsic_ballot_find_lsb:
   case nir_intrinsic_ballot_find_msb:
      replacement = nir_imm_int(b, 0);
      break;
   case nir_intrinsic_ballot_bitfield_extract: {
      nir_def *x = nir_channel(b, intrin->src[0].ssa, 0);
      nir_def *idx = intrin->src[1].ssa;
      nir_def *in_lane = nir_ieq_imm(b, idx, 0);
      nir_def *bit0 = nir_ine_imm(b, nir_iand_imm(b, x, 1), 0);
      replacement = nir_iand(b, in_lane, bit0);
      break;
   }
   case nir_intrinsic_inverse_ballot: {
      nir_def *x = nir_channel(b, intrin->src[0].ssa, 0);
      replacement = nir_ine_imm(b, nir_iand_imm(b, x, 1), 0);
      break;
   }
   case nir_intrinsic_vote_all:
   case nir_intrinsic_vote_any:
   case nir_intrinsic_quad_vote_all:
   case nir_intrinsic_quad_vote_any:
      replacement = intrin->src[0].ssa;
      break;
   case nir_intrinsic_vote_ieq:
   case nir_intrinsic_vote_feq:
      replacement = nir_imm_true(b);
      break;
   case nir_intrinsic_read_first_invocation:
   case nir_intrinsic_read_invocation:
   case nir_intrinsic_shuffle:
   case nir_intrinsic_shuffle_xor:
   case nir_intrinsic_shuffle_up:
   case nir_intrinsic_shuffle_down:
   case nir_intrinsic_rotate:
   case nir_intrinsic_quad_broadcast:
   case nir_intrinsic_quad_swap_horizontal:
   case nir_intrinsic_quad_swap_vertical:
   case nir_intrinsic_quad_swap_diagonal:
   case nir_intrinsic_reduce:
   case nir_intrinsic_inclusive_scan:
      replacement = intrin->src[0].ssa;
      break;
   case nir_intrinsic_exclusive_scan: {
      nir_op op = nir_intrinsic_reduction_op(intrin);
      unsigned bs = intrin->def.bit_size;
      nir_def *id;
      switch (op) {
      case nir_op_imul:
         id = nir_imm_intN_t(b, 1, bs);
         break;
      case nir_op_fmul:
         id = nir_imm_floatN_t(b, 1.0, bs);
         break;
      case nir_op_iand:
         id = nir_imm_intN_t(b, ~0ull, bs);
         break;
      case nir_op_umin:
         id = nir_imm_intN_t(b, ~0ull, bs);
         break;
      case nir_op_imin:
         id = nir_imm_intN_t(b, (1ull << (bs - 1)) - 1, bs);
         break;
      case nir_op_imax:
         id = nir_imm_intN_t(b, 1ull << (bs - 1), bs);
         break;
      case nir_op_fmin:
         id = nir_imm_floatN_t(b, INFINITY, bs);
         break;
      case nir_op_fmax:
         id = nir_imm_floatN_t(b, -INFINITY, bs);
         break;
      default:
         id = nir_imm_intN_t(b, 0, bs);
         break;
      }
      replacement = intrin->def.num_components > 1
                       ? nir_replicate(b, id, intrin->def.num_components)
                       : id;
      break;
   }
   default:
      return false;
   }

   nir_def_replace(&intrin->def, replacement);
   return true;
}

void
msl_nir_lower_vs_subgroups(nir_shader *nir)
{
   NIR_PASS(_, nir, nir_shader_intrinsics_pass, lower_vs_subgroup_intrin,
            nir_metadata_control_flow, NULL);
}

void
msl_nir_lower_subgroups(nir_shader *nir)
{
   const nir_lower_subgroups_options subgroups_options = {
      .filter = lower_subgroups_filter,
      .filter_data = &subgroups_options,
      .subgroup_size = 32,
      .ballot_bit_size = 32,
      .ballot_components = 1,
      .lower_subgroup_masks = true,
      .lower_vote_ieq = true,
      .lower_vote_feq = true,
      .lower_vote_bool_eq = true,
      .lower_inverse_ballot = true,
      /* Metal requires relative shuffle operations to have uniform delta */
      .lower_relative_shuffle = true,
      /* Metal reduce operations do not support certain types or cluster size;
       * lower_subgroups_filter keeps the native ones */
      .lower_reduce = true,
      /* Metal rotate operations do not support cluster size */
      .lower_rotate_clustered_to_shuffle = true,
   };
   NIR_PASS(_, nir, nir_lower_subgroups, &subgroups_options);
   NIR_PASS(_, nir, nir_shader_intrinsics_pass, lower_bool_ops,
            nir_metadata_control_flow, NULL);
}
