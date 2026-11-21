/* gameplaySP
 *
 * Copyright (C) 2006 Exophase <exophase@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "common.h"
#include <ctype.h>

timer_type timer[4];
void cgba_recompute_timer_cap_mask(void);

u32 frame_counter = 0;
u32 cpu_ticks = 0;
u32 execute_cycles = 0;
s32 video_count = 0;

u32 last_frame = 0;
u32 flush_ram_count = 0;
u32 gbc_update_count = 0;
u32 oam_update_count = 0;

char main_path[512];

static u32 random_state = 0;

#if defined(CGBA_GPSP_HEADLESS_TEST) && defined(CGBA_DYNAREC)
int cgba_sh4_trace_update_gba;
int cgba_sh4_trace_update_tag;
int cgba_sh4_trace_update_count;
int cgba_sh4_trace_update_limit;

#if defined(CGBA_SH4_UPDATE_TRACE)
static void cgba_trace_putc(char c)
{
  *(volatile unsigned char *)0xb7000000u = (unsigned char)c;
}

static void cgba_trace_puts(const char *s)
{
  while (*s)
    cgba_trace_putc(*s++);
}

static void cgba_trace_hex32(u32 v)
{
  static const char hex[] = "0123456789ABCDEF";
  int i;
  for (i = 7; i >= 0; i--)
    cgba_trace_putc(hex[(v >> (i * 4)) & 0x0F]);
}

static void cgba_trace_update_gba(int remaining_cycles)
{
  if (!cgba_sh4_trace_update_gba)
    return;
  if (cgba_sh4_trace_update_limit > 0 &&
      cgba_sh4_trace_update_count++ >= cgba_sh4_trace_update_limit)
    return;
  cgba_trace_putc('U');
  cgba_trace_putc((char)cgba_sh4_trace_update_tag);
  cgba_trace_puts(" pc");
  cgba_trace_hex32(reg[REG_PC]);
  cgba_trace_puts(" rem");
  cgba_trace_hex32((u32)remaining_cycles);
  cgba_trace_puts(" exec");
  cgba_trace_hex32(execute_cycles);
  cgba_trace_puts(" t0");
  cgba_trace_hex32((u32)timer[0].count);
  cgba_trace_putc('\n');
}
#else
#define cgba_trace_update_gba(x) ((void)0)
#endif
#else
#define cgba_trace_update_gba(x) ((void)0)
#endif

// Generate 16 random bits.
u16 rand_gen() {
  random_state = ((random_state * 1103515245) + 12345) & 0x7fffffff;
  return random_state;
}

// Add some random state to the initial seed.
void rand_seed(u32 data) {
  random_state ^= rand_gen() ^ data;
}


extern u32 cgba_timer_active_mask;

static __attribute__((noinline)) unsigned update_timers_slow(
   irq_type *irq_raised, unsigned completed_cycles)
{
   unsigned i, ret = 0;
   for (i = 0; i < 4; i++)
   {
      unsigned shift;

      if(timer[i].status == TIMER_INACTIVE)
         continue;

      /* Clock-select bits are ignored for count-up timers.  Keeping this
         defensive check here also makes old savestates with a nonzero
         cascade prescale behave correctly after loading. */
      shift = timer[i].status == TIMER_CASCADE ? 0 : timer[i].prescale;

      if(timer[i].status != TIMER_CASCADE)
         timer[i].count -= completed_cycles;

#if defined(CGBA_SILENT_TIMERS) && CGBA_SILENT_TIMERS
      /* The calculator's sound_timer is a no-op. Preserve the number of
       * cascade ticks and the final counter, without calling it for each
       * overflow. The common single-overflow case avoids software division. */
      if (timer[i].count <= 0)
      {
         u32 period = timer[i].reload << shift;
         u32 overflows = 1;
         timer[i].count += period;
         if (timer[i].count <= 0)
         {
            u32 extra = (0u - (u32)timer[i].count) / period + 1;
            timer[i].count += extra * period;
            overflows += extra;
         }
         if (timer[i].irq)
            *irq_raised |= (IRQ_TIMER0 << i);
         if (i != 3 && timer[i + 1].status == TIMER_CASCADE)
            timer[i + 1].count -= overflows;
      }
#else
      /* Process EVERY overflow the elapsed cycles covered: IRQ-less sound
         timers no longer cap the event slice (AW: the ~38kHz sample timer
         forced ~650 update_gba round-trips per frame), so a slice may span
         several periods. Timers that raise IRQs (or feed a cascade) still
         cap the slice and take one overflow per call, keeping IRQ timing. */
      while(timer[i].count <= 0)
      {
         /* irq_raised range: IRQ_TIMER0, IRQ_TIMER1, IRQ_TIMER2, IRQ_TIMER3 */
         if(timer[i].irq)
            *irq_raised |= (IRQ_TIMER0 << i);

         if((i != 3) && (timer[i + 1].status == TIMER_CASCADE))
            timer[i + 1].count--;

         if(i < 2)
         {
            if(timer[i].direct_sound_channels & 0x01)
               ret += sound_timer(timer[i].frequency_step, 0);

            if(timer[i].direct_sound_channels & 0x02)
               ret += sound_timer(timer[i].frequency_step, 1);
         }

         timer[i].count += (timer[i].reload << shift);
      }
#endif

      /* Publish the final counter after applying every reload.  Publishing
         before the loop exposed a transient zero at an exact overflow and
         left it visible after returning to guest code.  SimCity 2000 samples
         cascaded TM1 at that boundary; zero followed by the normal 0xFFxx
         value looked like nearly 65536 elapsed audio samples and made its
         mixer overwrite IWRAM code. */
      write_ioreg(REG_TMXD(i), -(timer[i].count >> shift));
   }
   return ret;
}

#if defined(CGBA_SILENT_TIMERS) && CGBA_SILENT_TIMERS
static __attribute__((noinline)) s32 cgba_timer0_refill_slow(s32 cnt, u32 period)
{
   u32 extra = (0u - (u32)cnt) / period + 1u;
   return cnt + (s32)(extra * period);
}
#endif

extern u32 cgba_timer_active_mask;
static u32 cgba_timer0_fast_only;

static inline unsigned update_timers(irq_type *irq_raised, unsigned completed_cycles)
{
#if defined(CGBA_SILENT_TIMERS) && CGBA_SILENT_TIMERS
   /* Fast path for the common single-sound-timer configuration (Timer 0 only,
    * prescale=1 (shift 0), no IRQ, no cascade). */
   if (__builtin_expect(cgba_timer0_fast_only != 0u, 1))
   {
      s32 cnt = timer[0].count - (s32)completed_cycles;
      if (cnt <= 0)
      {
         u32 period = timer[0].reload;
         cnt += (s32)period;
         if (__builtin_expect(cnt <= 0, 0))
            cnt = cgba_timer0_refill_slow(cnt, period);
      }
      timer[0].count = cnt;
      write_ioreg(REG_TMXD(0), -cnt);
      return 0;
   }
#endif
   return update_timers_slow(irq_raised, completed_cycles);
}

void init_main(void)
{
  u32 i;
  for(i = 0; i < 4; i++)
  {
    timer[i].status = TIMER_INACTIVE;
    timer[i].prescale = 0;
    timer[i].irq = 0;
    timer[i].reload = timer[i].count = 0x10000;
    timer[i].direct_sound_channels = TIMER_DS_CHANNEL_NONE;
    timer[i].frequency_step = 0;
  }

  timer[0].direct_sound_channels = TIMER_DS_CHANNEL_BOTH;
  timer[1].direct_sound_channels = TIMER_DS_CHANNEL_NONE;
  cgba_recompute_timer_cap_mask();

  frame_counter = 0;
  cpu_ticks = 0;
  execute_cycles = 960;
  video_count = 960;

#ifdef HAVE_DYNAREC
  init_dynarec_caches();
  init_emitter(gamepak_must_swap());
#endif
}

#ifdef CGBA_GPSP_HEADLESS_TEST
u32 cgba_update_gba_calls, cgba_update_gba_slices;
u32 cgba_update_gba_halt_calls;   /* entries with the CPU halted */
#endif

/* One-shot request from the dynarec's idle-loop-eliminated exits: process
 * event slices INTERNALLY (like halt) until an IRQ is flagged or taken, or
 * the frame completes. Guest-visible IF changes end the pass, so an
 * IRQ-flag poll re-executes exactly when it could observe progress; games
 * in the idle database whose poll is IRQ-driven (all current entries) are
 * unaffected semantically and save one C round-trip per event slice. */
u32 cgba_idle_wait;

/* Bitmask of timers that must cap the event slice at their next overflow
 * (IRQ-raising or cascade-feeding PRESCALE timers — see update_timers).
 * Recomputed on any timer start/stop and after savestate loads; zero for
 * pure sound-clock configurations (AW), skipping the per-slice scan. */
u32 cgba_timer_cap_mask;
u32 cgba_timer_active_mask;
u32 rfu_comstate;

void cgba_recompute_timer_cap_mask(void)
{
  unsigned i;
  u32 m = 0;
  u32 active = 0;
  for (i = 0; i < 4; i++)
  {
    if (timer[i].status != TIMER_INACTIVE)
      active |= 1u << i;
    if (timer[i].status == TIMER_PRESCALE &&
        (timer[i].irq ||
         (i != 3 && timer[i + 1].status == TIMER_CASCADE)))
      m |= 1u << i;
  }
  cgba_timer_cap_mask = m;
  cgba_timer_active_mask = active;
  cgba_timer0_fast_only = (active == 1u &&
                           timer[0].status == TIMER_PRESCALE &&
                           !timer[0].irq &&
                           timer[0].prescale == 0u);
}

static __attribute__((noinline)) int cgba_poll_serial(u32 completed_cycles,
                                                      irq_type *irq_raised)
{
  extern u32 serial_irq_cycles;
  extern int serial_mode;
  int serial_active = serial_irq_cycles != 0;
  if (!serial_active)
    switch (serial_mode) {
    case SERIAL_MODE_RFU:
      serial_active = (rfu_comstate >= 8u); break;
    case SERIAL_MODE_SERIAL_POKE:
      serial_active = 1; break;
    case SERIAL_MODE_SERIAL_AW1:
    case SERIAL_MODE_SERIAL_AW2:
      serial_active = netplay_client_id != 0; break;
    default:
      break;
    }
  if (serial_active && update_serial(completed_cycles))
    *irq_raised |= IRQ_SERIAL;
  return serial_active;
}

typedef struct {
  u32 vcount;
  u32 dispstat;
  irq_type irq_raised;
  cpu_alert_type dma_alert;
  int dma_cycles;
  u32 frame_complete;
} cgba_ugba_boundary_res;

static cgba_ugba_boundary_res cgba_ugba_bres;

static __attribute__((noinline)) void cgba_ugba_hblank_dma(int cur_dma_cycles)
{
  cpu_alert_type alert = CPU_ALERT_NONE;
  int dcyc = cur_dma_cycles;
  u32 mask = cgba_hblank_dma_mask;
  while (mask)
  {
    u32 i = (u32)__builtin_ctz(mask);
    mask &= mask - 1u;
    if (__builtin_expect(dma[i].start_type == DMA_START_HBLANK, 1))
      alert |= dma_transfer(i, &dcyc);
  }
  cgba_ugba_bres.dma_alert = alert;
  cgba_ugba_bres.dma_cycles = dcyc;
}

static __attribute__((noinline)) void cgba_ugba_vcount_boundary(
  u32 vcount, u32 dispstat, int cur_dma_cycles)
{
  irq_type irq_out = IRQ_NONE;
  cpu_alert_type alert_out = CPU_ALERT_NONE;
  int dcyc = cur_dma_cycles;
  u32 fc = 0;
  cgba_video_flush_before_write();
  if (vcount == 160)
  {
    u32 i;
    dispstat |= 0x01;
    video_reload_counters();
    if (dispstat & 0x8)
      irq_out |= IRQ_VBLANK;
    for (i = 0; i < 4; i++)
    {
      if (dma[i].start_type == DMA_START_VBLANK)
        alert_out |= dma_transfer(i, &dcyc);
    }
  }
  else
  {
    /* vcount == 228 */
    vcount = 0;
    dispstat &= ~0x01u;
    if (cheat_master_hook == ~0U)
      process_cheats();
    gbc_update_count = 0;
    oam_update_count = 0;
    flush_ram_count = 0;
    render_gbc_sound();
    frame_counter++;
    fc = 0x80000000u;
  }
  cgba_ugba_bres.vcount = vcount;
  cgba_ugba_bres.dispstat = dispstat;
  cgba_ugba_bres.irq_raised = irq_out;
  cgba_ugba_bres.dma_alert = alert_out;
  cgba_ugba_bres.dma_cycles = dcyc;
  cgba_ugba_bres.frame_complete = fc;
}

static __attribute__((noinline)) void cgba_ugba_timer_cap(void)
{
  unsigned i;
  for (i = 0; i < 4; i++)
  {
    if ((cgba_timer_cap_mask & (1u << i)) &&
        timer[i].count < (s32)execute_cycles) {
      execute_cycles = timer[i].count;
#if defined(CGBA_GPSP_HEADLESS_TEST) && defined(CGBA_SH4_DIAG_COUNTERS)
      { extern u32 cgba_cap_src[8]; cgba_cap_src[2 + i]++; }
#endif
    }
  }
}

#if defined(CGBA_SILENT_TIMERS) && CGBA_SILENT_TIMERS
static __attribute__((noinline)) unsigned cgba_ugba_idle_fastforward(
  u32 frame_complete, int serial_active,
  u32 idle_pass, u32 changed_pc, u32 idle_if_entry)
{
  cgba_ugba_bres.irq_raised = IRQ_NONE;
  if (!frame_complete && !cgba_timer_cap_mask && !serial_active) {
    int is_halt = (reg[CPU_HALT_STATE] == CPU_HALT);
    if (is_halt ||
        (reg[CPU_HALT_STATE] == CPU_ACTIVE && idle_pass && !changed_pc &&
         read_ioreg(REG_IF) == idle_if_entry)) {
      u32 dispstat = read_ioreg(REG_DISPSTAT);
      u32 hblank_irq_en = dispstat & 0x10u;
      int hblank_safe = (hblank_irq_en == 0u) ||
                        (((read_ioreg(REG_IE) & IRQ_HBLANK) == 0u) &&
                         (is_halt || (idle_if_entry & IRQ_HBLANK) != 0u));
      if (hblank_safe && video_count > 0) {
        u32 vcount = read_ioreg(REG_VCOUNT);
        u32 lyc = dispstat >> 8;
        if ((dispstat & 0x01u) && vcount >= 160u && vcount < 228u &&
            (!(dispstat & 0x20u) || lyc <= vcount || lyc >= 228u)) {
          u32 total_to_end = (u32)video_count +
                             ((dispstat & 0x02u) ? 0u : 272u) +
                             (227u - vcount) * 1232u;
          if (total_to_end > 272u) {
            if (hblank_irq_en)
              io_registers[REG_IF] |= eswap16(IRQ_HBLANK);
            video_count = 272;
            execute_cycles = 272;
            write_ioreg(REG_VCOUNT, 227);
            dispstat |= 0x02u;
            if (227u == lyc)
              dispstat |= 0x04u;
            else
              dispstat &= ~0x04u;
            write_ioreg(REG_DISPSTAT, dispstat);
            return total_to_end - 272u;
          }
        } else if ((dispstat & 0x03u) == 0x02u && vcount < 159u &&
                   (!(dispstat & 0x20u) || (vcount + 1u) != lyc)) {
          int ok = 0;
          u32 stop_y = 159u;
          if ((dispstat & 0x20u) && lyc > vcount && lyc <= 159u)
            stop_y = lyc - 1u;
          u32 lines = stop_y - vcount;
          if (!cgba_hblank_dma_mask) {
#ifdef CGBA_VIDEO_DEFERRED_ACTIVE
            cgba_queue_scanlines(vcount + 1u, lines);
            write_ioreg(REG_VCOUNT, stop_y);
#else
            for (u32 y = vcount + 1u; y <= stop_y; y++) {
              write_ioreg(REG_VCOUNT, y);
              update_scanline();
            }
#endif
            ok = 1;
          }
#if defined(CGBA_BULK_DMA) && CGBA_BULK_DMA && defined(CGBA_VIDEO_DEFERRED_ACTIVE)
          else if (is_halt && cgba_hblank_dma_mask == 1u &&
                   cgba_dma0_bg2_fast != 0u &&
                   cgba_try_batch_hblank_bg2_dma0_fast(vcount + 1u, lines)) {
            write_ioreg(REG_VCOUNT, stop_y);
            ok = 1;
          }
#endif
          if (ok) {
            u32 batch_cycles = (u32)video_count + lines * 1232u - 272u;
            if (reg[OAM_UPDATED])
              oam_update_count += lines;
            if (hblank_irq_en)
              io_registers[REG_IF] |= eswap16(IRQ_HBLANK);
            if (stop_y == lyc)
              dispstat |= 0x04u;
            else
              dispstat &= ~0x04u;
            write_ioreg(REG_DISPSTAT, dispstat);
            video_count = 272;
            execute_cycles = 272;
            return batch_cycles;
          }
        }
      }
    }
  }
  return 0u;
}
#endif

static __attribute__((noinline)) u32 cgba_update_gba_slow(int remaining_cycles)
{
  u32 changed_pc = 0;
  u32 frame_complete = 0;
  irq_type irq_raised = IRQ_NONE;
  int dma_cycles;
  u32 idle_pass = cgba_idle_wait;
  u32 idle_if_entry = 0;
  unsigned timer_extra = 0;
  if (__builtin_expect(idle_pass != 0u, 0)) {
    cgba_idle_wait = 0;
    /* Registered gba_over.h poll loops (idle_pass == 2) may poll VCOUNT or
     * DISPSTAT directly with IRQs disabled; only batch across event slices
     * when an enabled IRQ can actually wake the loop. Self-loops (1) can only
     * exit via an IRQ and always batch. */
    if (idle_pass == 2u &&
        (!read_ioreg(REG_IME) || (reg[REG_CPSR] & 0x80u) ||
         !read_ioreg(REG_IE)))
      idle_pass = 0u;
    else
      idle_if_entry = read_ioreg(REG_IF);
  }

  do
  {
    cpu_alert_type dma_alert = CPU_ALERT_NONE;
#if defined(CGBA_GPSP_HEADLESS_TEST) && defined(CGBA_SH4_DIAG_COUNTERS)
    cgba_update_gba_slices++;
#endif
    // Number of cycles we ask to run - cycles that we did not execute
    // (remaining_cycles can be negative and should be close to zero)
    unsigned completed_cycles = execute_cycles - remaining_cycles;
    unsigned elapsed_cycles = completed_cycles + timer_extra;
    timer_extra = 0;
    cpu_ticks += elapsed_cycles;

    remaining_cycles = 0;

    // Timers can trigger DMA (usually sound) and consume cycles.
#if defined(CGBA_SILENT_TIMERS) && CGBA_SILENT_TIMERS
    if (__builtin_expect(cgba_timer0_fast_only != 0u, 1))
    {
      s32 cnt = timer[0].count - (s32)elapsed_cycles;
      if (cnt <= 0)
      {
        u32 period = timer[0].reload;
        cnt += (s32)period;
        if (__builtin_expect(cnt <= 0, 0))
          cnt = cgba_timer0_refill_slow(cnt, period);
      }
      timer[0].count = cnt;
      write_ioreg(REG_TMXD(0), -cnt);
      dma_cycles = 0;
    }
    else
#endif
    if (cgba_timer_active_mask) {
      irq_type t_irq = IRQ_NONE;
      dma_cycles = update_timers_slow(&t_irq, elapsed_cycles);
      irq_raised |= t_irq;
    } else {
      dma_cycles = 0;
    }
    int serial_active = 0;
    u32 s_cyc;
    {
      extern u32 serial_irq_cycles;
      extern int serial_mode;
      s_cyc = serial_irq_cycles;
      if (__builtin_expect((s_cyc | (u32)serial_mode) != 0u, 0)) {
        irq_type s_irq = IRQ_NONE;
        serial_active = cgba_poll_serial(completed_cycles, &s_irq);
        irq_raised |= s_irq;
        s_cyc = serial_irq_cycles;
      }
    }

    // Video count tracks the video cycles remaining until the next event
    video_count -= completed_cycles;

    // Ran out of cycles, move to the next video area
    if(video_count <= 0)
    {
      u32 vcount = read_ioreg(REG_VCOUNT);
      u32 dispstat = read_ioreg(REG_DISPSTAT);

      // Check if we are in hrefresh (0) or hblank (1)
      if ((dispstat & 0x02) == 0)
      {
        // Transition from hrefresh to hblank
        dispstat |= 0x02;
        video_count += (272);    // hblank duration, 272 cycles

        // Check if we are drawing (0) or we are in vblank (1)
        if ((dispstat & 0x01) == 0)
        {
          // Render the scan line
          if(reg[OAM_UPDATED])
            oam_update_count++;

#ifdef CGBA_VIDEO_DEFERRED_ACTIVE
          if (__builtin_expect(skip_next_frame == 0, 1)) {
            u32 pcnt = cgba_pending_sl_count;
            if (__builtin_expect(pcnt != 0u && vcount == cgba_pending_sl_start + pcnt, 1)) {
              cgba_pending_sl_count = pcnt + 1u;
            } else {
              if (pcnt != 0u)
                cgba_flush_pending_scanlines();
              cgba_pending_sl_start = vcount;
              cgba_pending_sl_count = 1u;
            }
          }
#else
          update_scanline();
#endif

          // Trigger the HBlank DMAs if enabled
          if (__builtin_expect(cgba_hblank_dma_mask != 0, 0)) {
#if defined(CGBA_BULK_DMA) && CGBA_BULK_DMA && defined(CGBA_VIDEO_DEFERRED_ACTIVE)
            int fcyc = (cgba_hblank_dma_mask == 1u && cgba_dma0_bg2_fast != 0u)
                         ? cgba_try_hblank_bg2_dma0_fast()
                         : 0;
            if (__builtin_expect(fcyc != 0, 1)) {
              dma_cycles += fcyc;
            } else
#endif
            {
              cgba_ugba_hblank_dma(dma_cycles);
              dma_alert |= cgba_ugba_bres.dma_alert;
              dma_cycles = cgba_ugba_bres.dma_cycles;
            }
          }
        }

        // Trigger the hblank interrupt, if enabled in DISPSTAT
        if (__builtin_expect((dispstat & 0x10) != 0, 0))
          irq_raised |= IRQ_HBLANK;
      }
      else
      {
        // Transition from hblank to the next scan line (vdraw or vblank)
        video_count += 960;
        dispstat &= ~0x02;
        vcount++;

        if (__builtin_expect(vcount == 160 || vcount == 228, 0)) {
          cgba_ugba_vcount_boundary(vcount, dispstat, dma_cycles);
          vcount = cgba_ugba_bres.vcount;
          dispstat = cgba_ugba_bres.dispstat;
          irq_raised |= cgba_ugba_bres.irq_raised;
          dma_alert |= cgba_ugba_bres.dma_alert;
          dma_cycles = cgba_ugba_bres.dma_cycles;
          frame_complete |= cgba_ugba_bres.frame_complete;
        }

        // Vcount trigger (flag) and IRQ if enabled
        if (__builtin_expect(vcount == (dispstat >> 8), 0))
        {
          dispstat |= 0x04;
          if(dispstat & 0x20)
            irq_raised |= IRQ_VCOUNT;
        }
        else
          dispstat &= ~0x04;

        write_ioreg(REG_VCOUNT, vcount);
      }
      write_ioreg(REG_DISPSTAT, dispstat);
    }

#ifdef HAVE_DYNAREC
    /* Scheduled DMA runs while translated execution is parked here. A code
     * overwrite invalidates RAM translations, and either SMC or a DMA write
     * to WAITCNT must prevent the SH4 update stub from resuming its old block. */
    if (__builtin_expect(dma_alert != CPU_ALERT_NONE, 0)) {
      if (dma_alert & CPU_ALERT_SMC)
        flush_translation_cache_ram();
      if (dma_alert & (CPU_ALERT_SMC | CPU_ALERT_TIMING))
        changed_pc = 0x40000000;
    }
#else
    (void)dma_alert;
#endif

    // Flag any V/H blank interrupts, DMA IRQs, Vcount, etc.
    if (__builtin_expect(irq_raised != IRQ_NONE, 0)) {
      io_registers[REG_IF] |= eswap16((u16)irq_raised);
      irq_raised = IRQ_NONE;
    }

    // Raise any pending interrupts. This changes the CPU mode.
    if (__builtin_expect((io_registers[REG_IE] & io_registers[REG_IF]) != 0 &&
                         io_registers[REG_IME] != 0 &&
                         !(reg[REG_CPSR] & 0x80), 0))
      if (check_and_raise_interrupts())
        changed_pc = 0x40000000;

    // Figure out when we need to stop CPU execution. The next event is
    // a video event or a timer event, whatever happens first.
    execute_cycles = MAX(video_count, 0);
#if defined(CGBA_GPSP_HEADLESS_TEST) && defined(CGBA_SH4_DIAG_COUNTERS)
    { extern u32 cgba_cap_src[8]; cgba_cap_src[0]++; }   /* default: video */
#endif
    if (__builtin_expect(s_cyc != 0u, 0)) {
#if defined(CGBA_GPSP_HEADLESS_TEST) && defined(CGBA_SH4_DIAG_COUNTERS)
      if (s_cyc < execute_cycles) { extern u32 cgba_cap_src[8]; cgba_cap_src[1]++; }
#endif
      execute_cycles = MIN(execute_cycles, s_cyc);
    }

    // If we are paused due to a DMA, cap the number of cyles to that amount.
    if (__builtin_expect(reg[CPU_HALT_STATE] == CPU_DMA, 0)) {
      u32 dma_cyc = reg[REG_SLEEP_CYCLES];
      if (dma_cyc & 0x80000000)
        dma_cyc &= 0x7FFFFFFF;
      else
        dma_cyc -= MIN(dma_cyc, completed_cycles);
      reg[REG_SLEEP_CYCLES] = dma_cyc;
      if (!dma_cyc)
        reg[CPU_HALT_STATE] = CPU_ACTIVE;
      else
        execute_cycles = MIN(execute_cycles, dma_cyc);
    }

    /* Only IRQ or cascade-feeding timers must break the slice at their
       overflow; sound-clock timers batch (update_timers loops). */
    if (__builtin_expect(cgba_timer_cap_mask != 0, 0))
      cgba_ugba_timer_cap();
#if defined(CGBA_GPSP_HEADLESS_TEST) && defined(CGBA_SH4_DIAG_COUNTERS)
    { extern u32 cgba_cap_src[8]; cgba_cap_src[6] += execute_cycles;
      if (execute_cycles < 192) cgba_cap_src[7]++; }
#endif
    if (__builtin_expect(reg[CPU_HALT_STATE] == CPU_ACTIVE && !idle_pass, 1))
      break;
#if defined(CGBA_SILENT_TIMERS) && CGBA_SILENT_TIMERS
    /* Fast-forward halted/idle-waiting video periods when no IRQ, DMA, timer
     * cap, or serial event can interrupt before the next visible boundary. */
    timer_extra = cgba_ugba_idle_fastforward(
      frame_complete, serial_active,
      idle_pass, changed_pc, idle_if_entry);
    irq_raised |= cgba_ugba_bres.irq_raised;
#endif
  } while((reg[CPU_HALT_STATE] != CPU_ACTIVE ||
           (idle_pass && !changed_pc && read_ioreg(REG_IF) == idle_if_entry))
          && !frame_complete);

  // We voluntarily limit this. It is not accurate but it would be much harder.
  if (__builtin_expect(dma_cycles != 0, 0)) {
    dma_cycles = MIN(64, dma_cycles);
    dma_cycles = MIN(execute_cycles, dma_cycles);
    return (execute_cycles - dma_cycles) | changed_pc | frame_complete;
  }

  return execute_cycles | changed_pc | frame_complete;
}

#if defined(CGBA_SILENT_TIMERS) && CGBA_SILENT_TIMERS
static __attribute__((noinline)) u32 cgba_ugba_finish_cold_dma(
  cpu_alert_type dma_alert, u32 changed_pc, u32 frame_complete,
  int dma_cycles, unsigned completed_cycles)
{
#ifdef HAVE_DYNAREC
  if (dma_alert != CPU_ALERT_NONE) {
    if (dma_alert & CPU_ALERT_SMC)
      flush_translation_cache_ram();
    if (dma_alert & (CPU_ALERT_SMC | CPU_ALERT_TIMING))
      changed_pc = 0x40000000;
  }
#else
  (void)dma_alert;
#endif
  if ((io_registers[REG_IE] & io_registers[REG_IF]) != 0 &&
      io_registers[REG_IME] != 0 && !(reg[REG_CPSR] & 0x80))
    if (check_and_raise_interrupts())
      changed_pc = 0x40000000;

  execute_cycles = MAX(video_count, 0);
  if (reg[CPU_HALT_STATE] == CPU_DMA) {
    u32 dma_cyc = reg[REG_SLEEP_CYCLES];
    if (dma_cyc & 0x80000000)
      dma_cyc &= 0x7FFFFFFF;
    else
      dma_cyc -= MIN(dma_cyc, completed_cycles);
    reg[REG_SLEEP_CYCLES] = dma_cyc;
    if (!dma_cyc)
      reg[CPU_HALT_STATE] = CPU_ACTIVE;
    else
      execute_cycles = MIN(execute_cycles, dma_cyc);
  }
  if (cgba_timer_cap_mask != 0)
    cgba_ugba_timer_cap();
  if (reg[CPU_HALT_STATE] != CPU_ACTIVE && !frame_complete) {
    u32 tail = cgba_update_gba_slow(execute_cycles);
    return tail | changed_pc | frame_complete;
  }
  if (dma_cycles != 0) {
    dma_cycles = MIN(64, dma_cycles);
    dma_cycles = MIN(execute_cycles, dma_cycles);
    return (execute_cycles - dma_cycles) | changed_pc | frame_complete;
  }
  return execute_cycles | changed_pc | frame_complete;
}
#endif

CGBA_HOT_TEXT u32 function_cc update_gba(int remaining_cycles)
{
  trace_update_gba(remaining_cycles);
  cgba_trace_update_gba(remaining_cycles);

  if (__builtin_expect(remaining_cycles < -64, 0))
    remaining_cycles = -64;

#if defined(CGBA_GPSP_HEADLESS_TEST) && defined(CGBA_SH4_DIAG_COUNTERS)
  cgba_update_gba_calls++;
  if (reg[CPU_HALT_STATE] != CPU_ACTIVE)
    cgba_update_gba_halt_calls++;
#endif

#if defined(CGBA_SILENT_TIMERS) && CGBA_SILENT_TIMERS
  {
    extern u32 serial_irq_cycles;
    extern int serial_mode;
    if (__builtin_expect((cgba_idle_wait | reg[CPU_HALT_STATE] |
                          cgba_timer_cap_mask | serial_irq_cycles |
                          (u32)serial_mode) != 0u ||
                         cgba_timer0_fast_only == 0u, 0))
      return cgba_update_gba_slow(remaining_cycles);

#if defined(CGBA_GPSP_HEADLESS_TEST) && defined(CGBA_SH4_DIAG_COUNTERS)
    cgba_update_gba_slices++;
#endif
    unsigned completed_cycles = execute_cycles - remaining_cycles;
    cpu_ticks += completed_cycles;

    /* Fast single sound timer (Timer 0, prescale=1, no IRQ/cascade). */
    {
      s32 cnt = timer[0].count - (s32)completed_cycles;
      if (cnt <= 0) {
        u32 period = timer[0].reload;
        cnt += (s32)period;
        if (__builtin_expect(cnt <= 0, 0))
          cnt = cgba_timer0_refill_slow(cnt, period);
      }
      timer[0].count = cnt;
      write_ioreg(REG_TMXD(0), -cnt);
    }

    int vc = video_count - (int)completed_cycles;
    video_count = vc;

    int dma_cycles = 0;
    u32 changed_pc = 0;
    u32 irq_raised = 0;

    if (__builtin_expect(vc <= 0, 1)) {
      u32 vcount = read_ioreg(REG_VCOUNT);
      u32 dispstat = read_ioreg(REG_DISPSTAT);

      if ((dispstat & 0x02u) == 0u) {
        dispstat |= 0x02u;
        vc += 272;
        video_count = vc;

        if ((dispstat & 0x01u) == 0u) {
          if (reg[OAM_UPDATED])
            oam_update_count++;

#ifdef CGBA_VIDEO_DEFERRED_ACTIVE
          if (__builtin_expect(skip_next_frame == 0, 1)) {
            u32 pcnt = cgba_pending_sl_count;
            if (__builtin_expect(pcnt != 0u && vcount == cgba_pending_sl_start + pcnt, 1)) {
              cgba_pending_sl_count = pcnt + 1u;
            } else {
              if (pcnt != 0u)
                cgba_flush_pending_scanlines();
              cgba_pending_sl_start = vcount;
              cgba_pending_sl_count = 1u;
            }
          }
#else
          update_scanline();
#endif

          if (__builtin_expect(cgba_hblank_dma_mask != 0, 0)) {
#if defined(CGBA_BULK_DMA) && CGBA_BULK_DMA && defined(CGBA_VIDEO_DEFERRED_ACTIVE)
            int fcyc = (cgba_hblank_dma_mask == 1u && cgba_dma0_bg2_fast != 0u)
                         ? cgba_try_hblank_bg2_dma0_fast()
                         : 0;
            if (__builtin_expect(fcyc != 0, 1)) {
              dma_cycles = fcyc;
            } else
#endif
            {
              cgba_ugba_hblank_dma(0);
              if ((dispstat & 0x10u) != 0u)
                io_registers[REG_IF] |= eswap16(IRQ_HBLANK);
              write_ioreg(REG_DISPSTAT, dispstat);
              return cgba_ugba_finish_cold_dma(
                cgba_ugba_bres.dma_alert, 0u, 0u,
                cgba_ugba_bres.dma_cycles, completed_cycles);
            }
          }
        }

        if (__builtin_expect((dispstat & 0x10u) != 0u, 0))
          irq_raised = IRQ_HBLANK;
        write_ioreg(REG_DISPSTAT, dispstat);
      } else {
        vc += 960;
        video_count = vc;
        dispstat &= ~0x02u;
        vcount++;

        if (__builtin_expect(vcount == 160u || vcount == 228u, 0)) {
          cgba_ugba_vcount_boundary(vcount, dispstat, 0);
          vcount = cgba_ugba_bres.vcount;
          dispstat = cgba_ugba_bres.dispstat;
          irq_raised = cgba_ugba_bres.irq_raised;
          if (__builtin_expect(vcount == (dispstat >> 8), 0)) {
            dispstat |= 0x04u;
            if (dispstat & 0x20u)
              irq_raised |= IRQ_VCOUNT;
          } else {
            dispstat &= ~0x04u;
          }
          write_ioreg(REG_VCOUNT, vcount);
          write_ioreg(REG_DISPSTAT, dispstat);
          if (irq_raised != 0u)
            io_registers[REG_IF] |= eswap16((u16)irq_raised);
          return cgba_ugba_finish_cold_dma(
            cgba_ugba_bres.dma_alert, 0u, cgba_ugba_bres.frame_complete,
            cgba_ugba_bres.dma_cycles, completed_cycles);
        }

        if (__builtin_expect(vcount == (dispstat >> 8), 0)) {
          dispstat |= 0x04u;
          if (dispstat & 0x20u)
            irq_raised = IRQ_VCOUNT;
        } else {
          dispstat &= ~0x04u;
        }
        write_ioreg(REG_VCOUNT, vcount);
        write_ioreg(REG_DISPSTAT, dispstat);
      }
    }

    if (__builtin_expect(irq_raised != 0u, 0))
      io_registers[REG_IF] |= eswap16((u16)irq_raised);

    if (__builtin_expect((io_registers[REG_IE] & io_registers[REG_IF]) != 0 &&
                         io_registers[REG_IME] != 0 &&
                         !(reg[REG_CPSR] & 0x80), 0))
      if (check_and_raise_interrupts())
        changed_pc = 0x40000000;

    u32 ec = (vc > 0) ? (u32)vc : 0u;
    execute_cycles = ec;
#if defined(CGBA_GPSP_HEADLESS_TEST) && defined(CGBA_SH4_DIAG_COUNTERS)
    { extern u32 cgba_cap_src[8]; cgba_cap_src[0]++;
      cgba_cap_src[6] += ec;
      if (ec < 192) cgba_cap_src[7]++; }
#endif
    if (__builtin_expect(dma_cycles != 0, 0)) {
      if (dma_cycles > 64) dma_cycles = 64;
      if ((u32)dma_cycles > ec) dma_cycles = (int)ec;
      return (ec - (u32)dma_cycles) | changed_pc;
    }
    return ec | changed_pc;
  }
#else
  return cgba_update_gba_slow(remaining_cycles);
#endif
}

void reset_gba(void)
{
  gbp_reset();
  init_memory();
  init_main();
  init_cpu();
  reset_sound();
  cgba_discard_pending_scanlines();
  video_invalidate_scanline_cache(0, 160);
}

#ifdef TRACE_REGISTERS
void print_regs(void)
{
  printf("R0=%08x R1=%08x R2=%08x R3=%08x "
         "R4=%08x R5=%08x R6=%08x R7=%08x "
         "R8=%08x R9=%08x R10=%08x R11=%08x "
         "R12=%08x R13=%08x R14=%08x\n",
         reg[0], reg[1], reg[2], reg[3],
         reg[4], reg[5], reg[6], reg[7],
         reg[8], reg[9], reg[10], reg[11],
         reg[12], reg[13], reg[14]);
}
#endif

bool main_check_savestate(const u8 *src)
{
  int i;
  const u8 *p1 = bson_find_key(src, "emu");
  const u8 *p2 = bson_find_key(src, "timers");
  if (!p1 || !p2)
    return false;

  if (!bson_contains_key(p1, "cpu-ticks", BSON_TYPE_INT32) ||
      !bson_contains_key(p1, "exec-cycles", BSON_TYPE_INT32) ||
      !bson_contains_key(p1, "video-count", BSON_TYPE_INT32) ||
      !bson_contains_key(p1, "sleep-cycles", BSON_TYPE_INT32))
    return false;
  /* serial-irq-cycles is optional for forward compatibility with states
   * written before this field existed; missing simply means "no pending
   * serial IRQ", which is also the default after serialproto_reset. */

  for (i = 0; i < 4; i++)
  {
    char tname[2] = {'0' + i, 0};
    const u8 *p = bson_find_key(p2, tname);
    if (!p)
      return false;

    if (!bson_contains_key(p, "count", BSON_TYPE_INT32) ||
        !bson_contains_key(p, "reload", BSON_TYPE_INT32) ||
        !bson_contains_key(p, "prescale", BSON_TYPE_INT32) ||
        !bson_contains_key(p, "freq-step", BSON_TYPE_INT32) ||
        !bson_contains_key(p, "dsc", BSON_TYPE_INT32) ||
        !bson_contains_key(p, "irq", BSON_TYPE_INT32) ||
        !bson_contains_key(p, "status", BSON_TYPE_INT32))
      return false;
  }

  cgba_recompute_timer_cap_mask();
  return true;
}

bool main_read_savestate(const u8 *src)
{
  int i;
  const u8 *p1 = bson_find_key(src, "emu");
  const u8 *p2 = bson_find_key(src, "timers");
  if (!p1 || !p2)
    return false;

  if (!(bson_read_int32(p1, "cpu-ticks", &cpu_ticks) &&
         bson_read_int32(p1, "exec-cycles", &execute_cycles) &&
         bson_read_int32(p1, "video-count", (u32*)&video_count) &&
         bson_read_int32(p1, "sleep-cycles", &reg[REG_SLEEP_CYCLES])))
    return false;

  if (!bson_read_int32(p1, "frame-count", &frame_counter))
    frame_counter = 60 * 10;  // Use "fake" 10 seconds.

  {
    u32 sirq;
    if (bson_read_int32(p1, "serial-irq-cycles", &sirq))
      serial_set_irq_cycles(sirq);
    else
      serial_set_irq_cycles(0);   /* Older states: no pending IRQ. */
  }

  /* random_state is also optional for backwards compat. Missing means
   * 'use whatever is currently in the static'; the RFU path will reseed
   * from cpu_ticks on the next rfu_reset, which is also deterministic. */
  bson_read_int32(p1, "rand-state", &random_state);

  /* gbp-state is optional for backwards compat.  Older states either
   * never had a GBP session active or are post-handshake (steady-state
   * loop where missing the precise gbp_seq_n is harmless within a few
   * frames).  Default: don't touch the in-memory values, gbp_reset
   * runs at content load and that's a safe starting point. */
  {
    u32 gbps;
    if (bson_read_int32(p1, "gbp-state", &gbps))
      gbp_set_state(gbps);
  }

  for (i = 0; i < 4; i++)
  {
    char tname[2] = {'0' + i, 0};
    const u8 *p = bson_find_key(p2, tname);

    if (!(
      bson_read_int32(p, "count", (u32*)&timer[i].count) &&
      bson_read_int32(p, "reload", &timer[i].reload) &&
      bson_read_int32(p, "prescale", &timer[i].prescale) &&
      bson_read_int32(p, "freq-step", &timer[i].frequency_step) &&
      bson_read_int32(p, "dsc", &timer[i].direct_sound_channels) &&
      bson_read_int32(p, "irq", &timer[i].irq) &&
      bson_read_int32(p, "status", &timer[i].status)))
      return false;

    /* Older builds applied TMxCNT_H clock-select bits to cascaded timers,
       stretching both their count and reload by 64/256/1024.  Count-up mode
       ignores those bits.  Modulo the unscaled period to preserve the exact
       parent-overflow phase when importing such a state. */
    if(timer[i].status == TIMER_CASCADE && i == 0)
    {
      /* TM0 count-up is ignored; old builds could nevertheless save this
         impossible state.  Resume it as the selected prescaled timer. */
      timer[i].status = TIMER_PRESCALE;
    }
    else if(timer[i].status == TIMER_CASCADE)
    {
      /* Only legacy nonzero-prescaler states need phase conversion.  A
         canonical count-up timer can legitimately have count > reload when
         the guest changes the reload latch while it is already running. */
      if(timer[i].prescale != 0 && timer[i].count > 0 &&
         timer[i].reload != 0)
        timer[i].count = ((timer[i].count - 1) % timer[i].reload) + 1;
      timer[i].prescale = 0;
    }
   }

  return true;
}

void main_finalize_savestate_load(void)
{
  unsigned i;

  /* memory_read_savestate() runs after main_read_savestate() and restores the
     old raw I/O image.  Republish active counters from normalized internal
     state so a legacy transient-zero TMxD cannot survive the load boundary. */
  for(i = 0; i < 4; i++)
  {
    unsigned shift;

    if(timer[i].status == TIMER_INACTIVE)
      continue;
    shift = timer[i].status == TIMER_CASCADE ? 0 : timer[i].prescale;
    write_ioreg(REG_TMXD(i), -(timer[i].count >> shift));
  }
  cgba_recompute_timer_cap_mask();
}

unsigned main_write_savestate(u8* dst)
{
  int i;
  u8 *wbptr, *wbptr2, *startp = dst;
  bson_start_document(dst, "emu", wbptr);
  bson_write_int32(dst, "frame-count", frame_counter);
  bson_write_int32(dst, "cpu-ticks", cpu_ticks);
  bson_write_int32(dst, "exec-cycles", execute_cycles);
  bson_write_int32(dst, "video-count", video_count);
  bson_write_int32(dst, "sleep-cycles", reg[REG_SLEEP_CYCLES]);
  bson_write_int32(dst, "serial-irq-cycles", serial_get_irq_cycles());
  bson_write_int32(dst, "rand-state", random_state);
  bson_write_int32(dst, "gbp-state", gbp_get_state());
  bson_finish_document(dst, wbptr);

  bson_start_document(dst, "timers", wbptr);
  for (i = 0; i < 4; i++)
  {
    char tname[2] = {'0' + i, 0};
    bson_start_document(dst, tname, wbptr2);
    bson_write_int32(dst, "count", timer[i].count);
    bson_write_int32(dst, "reload", timer[i].reload);
    bson_write_int32(dst, "prescale", timer[i].prescale);
    bson_write_int32(dst, "freq-step", timer[i].frequency_step);
    bson_write_int32(dst, "dsc", timer[i].direct_sound_channels);
    bson_write_int32(dst, "irq", timer[i].irq);
    bson_write_int32(dst, "status", timer[i].status);
    bson_finish_document(dst, wbptr2);
  }
  bson_finish_document(dst, wbptr);

  return (unsigned int)(dst - startp);
}
