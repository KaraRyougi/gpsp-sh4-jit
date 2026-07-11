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


static unsigned update_timers(irq_type *irq_raised, unsigned completed_cycles)
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
}

u32 function_cc update_gba(int remaining_cycles)
{
  u32 changed_pc = 0;
  u32 frame_complete = 0;
  irq_type irq_raised = IRQ_NONE;
  int dma_cycles;
  u32 idle_pass = cgba_idle_wait;
  u32 idle_if_entry = idle_pass ? read_ioreg(REG_IF) : 0;
  cgba_idle_wait = 0;
  trace_update_gba(remaining_cycles);
  cgba_trace_update_gba(remaining_cycles);

  remaining_cycles = MAX(remaining_cycles, -64);

#if defined(CGBA_GPSP_HEADLESS_TEST) && defined(CGBA_SH4_DIAG_COUNTERS)
  cgba_update_gba_calls++;
  if (reg[CPU_HALT_STATE] != CPU_ACTIVE)
    cgba_update_gba_halt_calls++;
#endif
  do
  {
    unsigned i;
    cpu_alert_type dma_alert = CPU_ALERT_NONE;
#if defined(CGBA_GPSP_HEADLESS_TEST) && defined(CGBA_SH4_DIAG_COUNTERS)
    cgba_update_gba_slices++;
#endif
    // Number of cycles we ask to run - cycles that we did not execute
    // (remaining_cycles can be negative and should be close to zero)
    unsigned completed_cycles = execute_cycles - remaining_cycles;
    cpu_ticks += completed_cycles;

    remaining_cycles = 0;

    // Timers can trigger DMA (usually sound) and consume cycles.
    dma_cycles = cgba_timer_active_mask ?
      update_timers(&irq_raised, completed_cycles) : 0;
    // Check for serial port IRQs as well. Skip the call (once per event
    // slice) when nothing is scheduled and no cycle-driven device is
    // active: RFU/Pokemon-serial poll unconditionally; the AdvWars link
    // only acts as a netplay CLIENT (master pretend-IRQs).
    {
      extern u32 serial_irq_cycles;
      extern int serial_mode;
      int serial_active = serial_irq_cycles != 0;
      if (!serial_active)
        switch (serial_mode) {
        case SERIAL_MODE_RFU:
        case SERIAL_MODE_SERIAL_POKE:
          serial_active = 1; break;
        case SERIAL_MODE_SERIAL_AW1:
        case SERIAL_MODE_SERIAL_AW2:
          serial_active = netplay_client_id != 0; break;
        default:
          break;
        }
      if (serial_active && update_serial(completed_cycles))
        irq_raised |= IRQ_SERIAL;
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
          u32 i;

          // Render the scan line
          if(reg[OAM_UPDATED])
            oam_update_count++;

          update_scanline();

          // Trigger the HBlank DMAs if enabled
          for (i = 0; i < 4; i++)
          {
            if(dma[i].start_type == DMA_START_HBLANK)
              dma_alert |= dma_transfer(i, &dma_cycles);
          }
        }

        // Trigger the hblank interrupt, if enabled in DISPSTAT
        if (dispstat & 0x10)
          irq_raised |= IRQ_HBLANK;
      }
      else
      {
        // Transition from hblank to the next scan line (vdraw or vblank)
        video_count += 960;
        dispstat &= ~0x02;
        vcount++;

        if(vcount == 160)
        {
          // Transition from vrefresh to vblank
          u32 i;
          dispstat |= 0x01;

          // Reinit affine transformation counters for the next frame
          video_reload_counters();

          // Trigger VBlank interrupt if enabled
          if (dispstat & 0x8)
            irq_raised |= IRQ_VBLANK;
#if defined(CGBA_GPSP_HEADLESS_TEST) && defined(CGBA_SH4_VBEDGE_TRACE)
          {
            extern int cgba_intrwait_state;
            static u32 vbn;
            if (cgba_intrwait_state && vbn < 24) {
              static const char h[] = "0123456789ABCDEF";
              volatile unsigned char *port = (volatile unsigned char *)0xb7000000u;
              u32 vals[6] = { dispstat, (u32)irq_raised, read_ioreg(REG_IE),
                              read_ioreg(REG_IF), read_ioreg(REG_IME),
                              reg[REG_CPSR] };
              int vi, bi;
              vbn++;
              *port='V';*port='B';*port=':';
              for (vi = 0; vi < 6; vi++) {
                for (bi = 7; bi >= 0; bi--) *port = h[(vals[vi]>>(bi*4))&0xF];
                *port=' ';
              }
              *port='\n';
            }
          }
#endif

          // Trigger the VBlank DMAs if enabled
          for (i = 0; i < 4; i++)
          {
            if(dma[i].start_type == DMA_START_VBLANK)
              dma_alert |= dma_transfer(i, &dma_cycles);
          }
        }
        else if (vcount == 228)
        {
          // Transition from vblank to next screen
          vcount = 0;
          dispstat &= ~0x01;

          /* If there's no cheat hook, run on vblank! */
          if (cheat_master_hook == ~0U)
             process_cheats();

/*        printf("frame update (%x), %d instructions total, %d RAM flushes\n",
           reg[REG_PC], instruction_count - last_frame, flush_ram_count);
          last_frame = instruction_count;
*/
/*          printf("%d gbc audio updates\n", gbc_update_count);
          printf("%d oam updates\n", oam_update_count); */
          gbc_update_count = 0;
          oam_update_count = 0;
          flush_ram_count = 0;

          // Force audio generation. Need to flush samples for this frame.
          render_gbc_sound();

          // We completed a frame, tell the dynarec to exit to the main thread
          frame_complete = 0x80000000;
          frame_counter++;
        }

        // Vcount trigger (flag) and IRQ if enabled
        if(vcount == (dispstat >> 8))
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
    if (dma_alert & CPU_ALERT_SMC)
      flush_translation_cache_ram();
    if (dma_alert & (CPU_ALERT_SMC | CPU_ALERT_TIMING))
      changed_pc = 0x40000000;
#else
    (void)dma_alert;
#endif

    // Flag any V/H blank interrupts, DMA IRQs, Vcount, etc.
    if (irq_raised)
      flag_interrupt(irq_raised);

    // Raise any pending interrupts. This changes the CPU mode.
    if (check_and_raise_interrupts())
      changed_pc = 0x40000000;

    // Figure out when we need to stop CPU execution. The next event is
    // a video event or a timer event, whatever happens first.
    execute_cycles = MAX(video_count, 0);
#if defined(CGBA_GPSP_HEADLESS_TEST) && defined(CGBA_SH4_DIAG_COUNTERS)
    { extern u32 cgba_cap_src[8]; cgba_cap_src[0]++; }   /* default: video */
#endif
    {
      u32 cc = serial_next_event();
#if defined(CGBA_GPSP_HEADLESS_TEST) && defined(CGBA_SH4_DIAG_COUNTERS)
      if (cc < execute_cycles) { extern u32 cgba_cap_src[8]; cgba_cap_src[1]++; }
#endif
      execute_cycles = MIN(execute_cycles, cc);
    }

    // If we are paused due to a DMA, cap the number of cyles to that amount.
    if (reg[CPU_HALT_STATE] == CPU_DMA) {
      u32 dma_cyc = reg[REG_SLEEP_CYCLES];
      // The first iteration is marked by bit 31 set, just do nothing now.
      if (dma_cyc & 0x80000000)
        dma_cyc &= 0x7FFFFFFF;  // Start counting DMA cycles from now on.
      else
        dma_cyc -= MIN(dma_cyc, completed_cycles);  // Account DMA cycles.

      reg[REG_SLEEP_CYCLES] = dma_cyc;
      if (!dma_cyc)
        reg[CPU_HALT_STATE] = CPU_ACTIVE;   // DMA finished, resume execution.
      else
        execute_cycles = MIN(execute_cycles, dma_cyc);  // Continue sleeping.
    }

    /* Only IRQ or cascade-feeding timers must break the slice at their
       overflow; sound-clock timers batch (update_timers loops). */
    if (cgba_timer_cap_mask)
      for (i = 0; i < 4; i++)
      {
         if ((cgba_timer_cap_mask & (1u << i)) &&
             timer[i].count < execute_cycles) {
            execute_cycles = timer[i].count;
#if defined(CGBA_GPSP_HEADLESS_TEST) && defined(CGBA_SH4_DIAG_COUNTERS)
            { extern u32 cgba_cap_src[8]; cgba_cap_src[2 + i]++; }
#endif
         }
      }
#if defined(CGBA_GPSP_HEADLESS_TEST) && defined(CGBA_SH4_DIAG_COUNTERS)
    { extern u32 cgba_cap_src[8]; cgba_cap_src[6] += execute_cycles;
      if (execute_cycles < 192) cgba_cap_src[7]++; }
#endif
  } while((reg[CPU_HALT_STATE] != CPU_ACTIVE ||
           (idle_pass && !changed_pc && read_ioreg(REG_IF) == idle_if_entry))
          && !frame_complete);

  // We voluntarily limit this. It is not accurate but it would be much harder.
  dma_cycles = MIN(64, dma_cycles);
  dma_cycles = MIN(execute_cycles, dma_cycles);

  return (execute_cycles - dma_cycles) | changed_pc | frame_complete;
}

void reset_gba(void)
{
  gbp_reset();
  init_memory();
  init_main();
  init_cpu();
  reset_sound();
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
