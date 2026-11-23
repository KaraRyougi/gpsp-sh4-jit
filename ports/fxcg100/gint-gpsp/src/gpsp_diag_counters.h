#ifndef CGBA_GPSP_DIAG_COUNTERS_H
#define CGBA_GPSP_DIAG_COUNTERS_H

#include <stdint.h>
#include <string.h>

#ifdef CGBA_DYNAREC
extern int dynarec_enable;   /* gpSP: 0 = interpreter, 1 = SH4 recompiler */

#if defined(CGBA_GPSP_HEADLESS_TEST) || defined(CGBA_SH4_PROFILE_COUNTERS)
extern uint32_t cgba_dynarec_rom_flush_count;
extern uint32_t cgba_sh4_bios_fallback_call_count;
extern uint32_t cgba_sh4_bios_fallback_cycle_count;
extern uint32_t cgba_dynarec_cold_interp_count;
extern uint32_t cgba_dynarec_ram_flush_count;
extern uint32_t cgba_dynarec_arm_translate_count;
extern uint32_t cgba_dynarec_thumb_translate_count;
extern uint32_t cgba_dynarec_lookup_arm_count;
extern uint32_t cgba_dynarec_lookup_thumb_count;
extern uint32_t cgba_dynarec_lookup_dual_count;
extern uint32_t cgba_dynarec_icache_sync_count;
extern uint32_t cgba_dynarec_icache_sync_bytes;
extern uint32_t cgba_dynarec_patch_sync_count;
extern uint32_t cgba_dynarec_patch_sync_bytes;
extern uint32_t cgba_dynarec_ibh_arm_hit_count;
extern uint32_t cgba_dynarec_ibh_arm_slow_count;
extern uint32_t cgba_dynarec_ibh_arm_hot_count;
extern uint32_t cgba_dynarec_ibh_thumb_hit_count;
extern uint32_t cgba_dynarec_ibh_thumb_slow_count;
extern uint32_t cgba_dynarec_ibh_thumb_hot_count;
extern uint32_t cgba_dynarec_ibh_dual_arm_hit_count;
extern uint32_t cgba_dynarec_ibh_dual_thumb_hit_count;
extern uint32_t cgba_dynarec_ibh_dual_slow_count;
extern uint32_t cgba_dynarec_ibh_dual_hot_arm_count;
extern uint32_t cgba_dynarec_ibh_dual_hot_thumb_count;
extern void cgba_sh4_prof_reset(void);
extern uint32_t cgba_sh4_helper_thumb_ldst_count;
extern uint32_t cgba_sh4_helper_thumb_block_count;
extern uint32_t cgba_sh4_helper_thumb_shift_count;
extern uint32_t cgba_sh4_helper_thumb_dp_count;
extern uint32_t cgba_sh4_helper_arm_ldst_count;
extern uint32_t cgba_sh4_helper_arm_block_count;
extern uint32_t cgba_sh4_helper_arm_dp_count;
extern uint32_t cgba_sh4_helper_arm_mul_count;
extern uint32_t cgba_sh4_helper_arm_psr_count;
extern uint32_t cgba_sh4_helper_arm_swap_count;
extern uint32_t cgba_sh4_helper_hle_div_count;
extern uint32_t cgba_sh4_helper_arm_ldst_load_count;
extern uint32_t cgba_sh4_helper_arm_ldst_store_count;
extern uint32_t cgba_sh4_helper_arm_ldst_ram_count;
extern uint32_t cgba_sh4_helper_arm_ldst_io_count;
extern uint32_t cgba_sh4_helper_arm_ldst_video_count;
extern uint32_t cgba_sh4_helper_arm_ldst_rom_count;
extern uint32_t cgba_sh4_helper_arm_ldst_other_count;
extern uint32_t cgba_sh4_helper_arm_block_load_count;
extern uint32_t cgba_sh4_helper_arm_block_store_count;
extern uint32_t cgba_sh4_helper_thumb_ldst_load_count;
extern uint32_t cgba_sh4_helper_thumb_ldst_store_count;
extern uint32_t cgba_sh4_helper_thumb_ldst_ram_count;
extern uint32_t cgba_sh4_helper_thumb_ldst_io_count;
extern uint32_t cgba_sh4_helper_thumb_ldst_video_count;
extern uint32_t cgba_sh4_helper_thumb_ldst_video_region_count[3];
extern uint32_t cgba_sh4_helper_thumb_ldst_rom_count;
extern uint32_t cgba_sh4_helper_thumb_ldst_other_count;
extern uint32_t cgba_sh4_helper_thumb_ldst_unmapped_count;
extern uint32_t cgba_sh4_helper_thumb_ldst_guest_unaligned_count;
extern uint32_t cgba_sh4_helper_thumb_ldst_host_unaligned_count;
extern uint32_t cgba_sh4_helper_thumb_ldst_unsafe_region_count;
extern uint32_t cgba_sh4_helper_thumb_ldst_smc_count;
extern uint32_t cgba_sh4_helper_thumb_ldst_native_ready_count;
extern uint32_t cgba_sh4_helper_thumb_ldst_word_count;
extern uint32_t cgba_sh4_helper_thumb_ldst_byte_count;
extern uint32_t cgba_sh4_helper_thumb_ldst_half_count;
extern uint32_t cgba_sh4_helper_thumb_ldst_pc_count;
extern uint32_t cgba_sh4_helper_thumb_ldst_sp_count;
extern uint32_t cgba_sh4_helper_thumb_ldst_reg_count;
extern uint32_t cgba_sh4_helper_thumb_ldst_imm_count;
extern uint32_t cgba_sh4_native_thumb_const_io_count;
extern uint32_t cgba_sh4_thumb_io16_store_count[512];
extern uint32_t cgba_sh4_dma_ctl_count[4];
extern uint32_t cgba_sh4_dma_ctl_enable_count[4];
extern uint32_t cgba_sh4_dma_ctl_value[4][4];
extern uint32_t cgba_sh4_dma_ctl_value_count[4][4];
extern uint32_t cgba_sh4_hle_div_zero_count;
extern uint32_t cgba_sh4_hle_div_one_count;
extern uint32_t cgba_sh4_hle_div_neg_one_count;
extern uint32_t cgba_sh4_hle_div_pow2_count;
extern uint32_t cgba_sh4_hle_div_other_count;
extern uint32_t cgba_sh4_prof_entry_count;
extern uint32_t cgba_sh4_prof_overflow_count;

struct cgba_sh4_prof_row {
	uint32_t key;
	uint32_t count;
};
extern unsigned cgba_sh4_prof_top(struct cgba_sh4_prof_row *out, unsigned max);

#ifdef CGBA_SH4_ARM_MIXER_FASTPATH
extern uint32_t cgba_sh4_arm_mixer_try_count;
extern uint32_t cgba_sh4_arm_mixer_hit_count;
extern uint32_t cgba_sh4_arm_mixer_batch_count;
extern uint32_t cgba_sh4_arm_mixer_guard_fallback_count;
extern uint32_t cgba_sh4_arm_mixer_budget_stop_count;
extern uint32_t cgba_sh4_arm_mixer_first_pc;
extern uint32_t cgba_sh4_arm_mixer_first_reg[10];
#endif

static inline void cgba_diag_counters_reset(int reset_bios_hle)
{
	cgba_dynarec_rom_flush_count = 0;
	cgba_dynarec_ram_flush_count = 0;
	cgba_dynarec_arm_translate_count = 0;
	cgba_dynarec_thumb_translate_count = 0;
	cgba_sh4_bios_fallback_call_count = 0;
	cgba_sh4_bios_fallback_cycle_count = 0;
	cgba_dynarec_cold_interp_count = 0;
	cgba_dynarec_lookup_arm_count = 0;
	cgba_dynarec_lookup_thumb_count = 0;
	cgba_dynarec_lookup_dual_count = 0;
	cgba_dynarec_icache_sync_count = 0;
	cgba_dynarec_icache_sync_bytes = 0;
	cgba_dynarec_patch_sync_count = 0;
	cgba_dynarec_patch_sync_bytes = 0;
	cgba_dynarec_ibh_arm_hit_count = 0;
	cgba_dynarec_ibh_arm_slow_count = 0;
	cgba_dynarec_ibh_arm_hot_count = 0;
	cgba_dynarec_ibh_thumb_hit_count = 0;
	cgba_dynarec_ibh_thumb_slow_count = 0;
	cgba_dynarec_ibh_thumb_hot_count = 0;
	cgba_dynarec_ibh_dual_arm_hit_count = 0;
	cgba_dynarec_ibh_dual_thumb_hit_count = 0;
	cgba_dynarec_ibh_dual_slow_count = 0;
	cgba_dynarec_ibh_dual_hot_arm_count = 0;
	cgba_dynarec_ibh_dual_hot_thumb_count = 0;
#ifdef CGBA_GPSP_HEADLESS_TEST
	if (reset_bios_hle) {
		extern uint32_t cgba_bios_hle_irq_in, cgba_bios_hle_irq_out;
		extern uint32_t cgba_bios_hle_swi_count;
		cgba_bios_hle_irq_in = 0;
		cgba_bios_hle_irq_out = 0;
		cgba_bios_hle_swi_count = 0;
	}
#else
	(void)reset_bios_hle;
#endif
	cgba_sh4_prof_reset();
	cgba_sh4_helper_thumb_ldst_count = 0;
	cgba_sh4_helper_thumb_block_count = 0;
	cgba_sh4_helper_thumb_shift_count = 0;
	cgba_sh4_helper_thumb_dp_count = 0;
	cgba_sh4_helper_arm_ldst_count = 0;
	cgba_sh4_helper_arm_block_count = 0;
	cgba_sh4_helper_arm_dp_count = 0;
	cgba_sh4_helper_arm_mul_count = 0;
	cgba_sh4_helper_arm_psr_count = 0;
	cgba_sh4_helper_arm_swap_count = 0;
	cgba_sh4_helper_hle_div_count = 0;
	cgba_sh4_helper_arm_ldst_load_count = 0;
	cgba_sh4_helper_arm_ldst_store_count = 0;
	cgba_sh4_helper_arm_ldst_ram_count = 0;
	cgba_sh4_helper_arm_ldst_io_count = 0;
	cgba_sh4_helper_arm_ldst_video_count = 0;
	cgba_sh4_helper_arm_ldst_rom_count = 0;
	cgba_sh4_helper_arm_ldst_other_count = 0;
	cgba_sh4_helper_arm_block_load_count = 0;
	cgba_sh4_helper_arm_block_store_count = 0;
	cgba_sh4_helper_thumb_ldst_load_count = 0;
	cgba_sh4_helper_thumb_ldst_store_count = 0;
	cgba_sh4_helper_thumb_ldst_ram_count = 0;
	cgba_sh4_helper_thumb_ldst_io_count = 0;
	cgba_sh4_helper_thumb_ldst_video_count = 0;
	memset(cgba_sh4_helper_thumb_ldst_video_region_count, 0,
		sizeof(uint32_t) * 3u);
	cgba_sh4_helper_thumb_ldst_rom_count = 0;
	cgba_sh4_helper_thumb_ldst_other_count = 0;
	cgba_sh4_helper_thumb_ldst_unmapped_count = 0;
	cgba_sh4_helper_thumb_ldst_guest_unaligned_count = 0;
	cgba_sh4_helper_thumb_ldst_host_unaligned_count = 0;
	cgba_sh4_helper_thumb_ldst_unsafe_region_count = 0;
	cgba_sh4_helper_thumb_ldst_smc_count = 0;
	cgba_sh4_helper_thumb_ldst_native_ready_count = 0;
	cgba_sh4_helper_thumb_ldst_word_count = 0;
	cgba_sh4_helper_thumb_ldst_byte_count = 0;
	cgba_sh4_helper_thumb_ldst_half_count = 0;
	cgba_sh4_helper_thumb_ldst_pc_count = 0;
	cgba_sh4_helper_thumb_ldst_sp_count = 0;
	cgba_sh4_helper_thumb_ldst_reg_count = 0;
	cgba_sh4_helper_thumb_ldst_imm_count = 0;
	cgba_sh4_native_thumb_const_io_count = 0;
	memset(cgba_sh4_thumb_io16_store_count, 0,
		sizeof(uint32_t) * 512u);
	memset(cgba_sh4_dma_ctl_count, 0, sizeof(uint32_t) * 4u);
	memset(cgba_sh4_dma_ctl_enable_count, 0, sizeof(uint32_t) * 4u);
	memset(cgba_sh4_dma_ctl_value, 0, sizeof(uint32_t) * 16u);
	memset(cgba_sh4_dma_ctl_value_count, 0, sizeof(uint32_t) * 16u);
	cgba_sh4_hle_div_zero_count = 0;
	cgba_sh4_hle_div_one_count = 0;
	cgba_sh4_hle_div_neg_one_count = 0;
	cgba_sh4_hle_div_pow2_count = 0;
	cgba_sh4_hle_div_other_count = 0;
}
#endif
#endif

#endif /* CGBA_GPSP_DIAG_COUNTERS_H */
