// SPDX-FileCopyrightText: 2026 BFU Electronics LTD
// SPDX-License-Identifier: Apache-2.0
//
// Renode peripheral models for a full-system emulation of the BFU-KB56 keyboard (STM32G030K6T6).
// Written from RM0454 (STM32G0x0 reference manual; the STM32G030 is covered there) for exactly what the
// firmware uses:
//   RCC (clock gates), GPIO with pin-level electrics, EXTI, PWR/SYSCFG (Stop, remap),
//   I2C1 in slave mode with an external bus master that plays the ESP32 host,
//   and the 5x12 key matrix with diodes.
// Loaded at run time with "include @UaKbModels.cs".
//
using System;
using System.Collections.Generic;
using System.Globalization;
using System.Linq;
using System.Text;
using Antmicro.Renode.Core;
using Antmicro.Renode.Logging;
using Antmicro.Renode.Peripherals.Bus;
using Antmicro.Renode.Peripherals.CPU;
using Antmicro.Renode.Time;

namespace Antmicro.Renode.Peripherals.UaKb
{
    // ------------------------------------------------------------------ helpers
    public static class Clock
    {
        public static double NowUs(IMachine m)
        {
            return m.ElapsedVirtualTime.TimeElapsed.TotalMicroseconds;
        }
    }

    // ------------------------------------------------------------------ RCC
    [AllowedTranslations(AllowedTranslation.ByteToDoubleWord | AllowedTranslation.WordToDoubleWord)]
    public class G0_RCC : IDoubleWordPeripheral, IKnownSize
    {
        public G0_RCC(IMachine machine) { this.machine = machine; Reset(); }

        public long Size { get { return 0x400; } }

        public void Reset()
        {
            regs.Clear();
            regs[0x00] = 0x00000500;            // CR: HSION | HSIRDY
            regs[0x38] = 0x00000100;            // AHBENR: FLASHEN
            regs[0x60] = 0x0C000000;            // CSR: reset flags (PIN + POR)
        }

        public uint ReadDoubleWord(long offset)
        {
            uint v;
            regs.TryGetValue(offset, out v);
            if(offset == 0x00) v |= ((v >> 8) & 1u) << 10;   // HSIRDY follows HSION
            return v;
        }

        public void WriteDoubleWord(long offset, uint value)
        {
            regs[offset] = value;
        }

        public bool PortClock(int port) { return Bit(0x34, port); }
        public bool Apb1(int bit) { return Bit(0x3C, bit); }
        public bool Apb2(int bit) { return Bit(0x40, bit); }
        public uint I2c1Sel { get { return (ReadDoubleWord(0x54) >> 12) & 3u; } }

        private bool Bit(long off, int bit) { uint v; regs.TryGetValue(off, out v); return ((v >> bit) & 1u) != 0; }

        private readonly Dictionary<long, uint> regs = new Dictionary<long, uint>();
        private readonly IMachine machine;
    }

    // ------------------------------------------------------------------ simple register files
    [AllowedTranslations(AllowedTranslation.ByteToDoubleWord | AllowedTranslation.WordToDoubleWord)]
    public class G0_RegFile : IDoubleWordPeripheral, IKnownSize
    {
        public G0_RegFile(IMachine machine, string name = "regs", uint resetValue0 = 0) { this.name = name; this.reset0 = resetValue0; Reset(); }
        public long Size { get { return 0x400; } }
        public void Reset() { regs.Clear(); regs[0] = reset0; }
        public uint ReadDoubleWord(long offset) { uint v; regs.TryGetValue(offset, out v); return v; }
        public void WriteDoubleWord(long offset, uint value) { regs[offset] = value; }
        public uint Get(long offset) { return ReadDoubleWord(offset); }
        private readonly Dictionary<long, uint> regs = new Dictionary<long, uint>();
        private readonly string name;
        private readonly uint reset0;
    }

    // ------------------------------------------------------------------ PWR + low-power state tracking
    [AllowedTranslations(AllowedTranslation.ByteToDoubleWord | AllowedTranslation.WordToDoubleWord)]
    public class G0_PWR : IDoubleWordPeripheral, IKnownSize
    {
        public G0_PWR(IMachine machine, ICPU cpu, G0_RCC rcc)
        {
            this.machine = machine;
            this.rcc = rcc;
            var tcpu = cpu as TranslationCPU;
            if(tcpu != null)
            {
                tcpu.AddHookAtWfiStateChange(OnWfi);
            }
            // Renode's NVIC does not read back SCB->SCR.SLEEPDEEP, so watch the writes
            machine.SystemBus.AddWatchpointHook(0xE000ED10, SysbusAccessWidth.DoubleWord, Access.Write,
                (c, addr, width, value) => { sleepDeepBit = (value & 4u) != 0; });
            Reset();
        }

        public long Size { get { return 0x400; } }

        public void Reset()
        {
            cr1 = 0x00000208;
        }

        public uint ReadDoubleWord(long offset)
        {
            if(offset == 0x00) return cr1;
            if(offset == 0x14) return 0x00000000;   // SR2: regulator ready
            return 0;
        }

        public void WriteDoubleWord(long offset, uint value)
        {
            if(offset == 0x00)
            {
                if(!rcc.Apb1(28)) { this.Log(LogLevel.Error, "CHECK|PWR written while PWREN=0 (write ignored)"); return; }
                cr1 = value;
            }
        }

        public uint LowPowerMode { get { return cr1 & 7u; } }

        // true while the core sleeps in WFI with SLEEPDEEP and LPMS = Stop 0 / Stop 1
        public bool InStop { get { return inWfi && deepSleep && LowPowerMode <= 1; } }
        public bool InWfi { get { return inWfi; } }

        public double TimeInStopUs { get { return stopUs + (InStop ? Clock.NowUs(machine) - stopStart : 0); } }
        public double TimeInSleepUs { get { return sleepUs + (inWfi && !InStop ? Clock.NowUs(machine) - sleepStart : 0); } }
        public int StopEntries { get { return stopEntries; } }

        public event Action<bool> StopChanged;

        private void OnWfi(bool entering)
        {
            var now = Clock.NowUs(machine);
            if(entering)
            {
                deepSleep = sleepDeepBit;
                inWfi = true;
                if(InStop)
                {
                    stopStart = now;
                    stopEntries++;
                    this.Log(LogLevel.Info, "POWER|{0}|STOP_ENTER|lpms={1}", Fmt(now), LowPowerMode);
                    var h = StopChanged; if(h != null) h(true);
                }
                else
                {
                    sleepStart = now;
                }
            }
            else
            {
                if(InStop)
                {
                    stopUs += now - stopStart;
                    this.Log(LogLevel.Info, "POWER|{0}|STOP_EXIT|{1}", Fmt(now), Fmt(now - stopStart));
                    inWfi = false;
                    var h = StopChanged; if(h != null) h(false);
                }
                else
                {
                    sleepUs += now - sleepStart;
                    inWfi = false;
                }
            }
        }

        public string Stats()
        {
            return string.Format(CultureInfo.InvariantCulture, "STATS|stop_us={0:F1}|sleep_us={1:F1}|stop_entries={2}|now_us={3:F1}",
                TimeInStopUs, TimeInSleepUs, stopEntries, Clock.NowUs(machine));
        }

        public void LogStats() { this.Log(LogLevel.Info, "POWER|{0}|{1}", Fmt(Clock.NowUs(machine)), Stats()); }

        internal static string Fmt(double us) { return us.ToString("F2", CultureInfo.InvariantCulture); }

        private uint cr1;
        private bool inWfi, deepSleep, sleepDeepBit;
        private double stopStart, sleepStart, stopUs, sleepUs;
        private int stopEntries;
        private readonly IMachine machine;
        private readonly G0_RCC rcc;
    }

    // ------------------------------------------------------------------ EXTI (RM0444 ch. 13)
    [AllowedTranslations(AllowedTranslation.ByteToDoubleWord | AllowedTranslation.WordToDoubleWord)]
    public class G0_EXTI : IDoubleWordPeripheral, IKnownSize
    {
        public G0_EXTI(IMachine machine)
        {
            this.machine = machine;
            IRQ01 = new GPIO();
            IRQ23 = new GPIO();
            IRQ415 = new GPIO();
            Reset();
        }

        public GPIO IRQ01 { get; private set; }
        public GPIO IRQ23 { get; private set; }
        public GPIO IRQ415 { get; private set; }
        public long Size { get { return 0x400; } }

        public void Reset()
        {
            rtsr = ftsr = rpr = fpr = emr = 0;
            imr = 0xFFF80000;                     // direct lines enabled at reset
            exticr = new uint[4];
            Update();
        }

        public uint ReadDoubleWord(long offset)
        {
            switch(offset)
            {
            case 0x00: return rtsr;
            case 0x04: return ftsr;
            case 0x0C: return rpr;
            case 0x10: return fpr;
            case 0x60: case 0x64: case 0x68: case 0x6C: return exticr[(offset - 0x60) / 4];
            case 0x80: return imr;
            case 0x84: return emr;
            default: return 0;
            }
        }

        public void WriteDoubleWord(long offset, uint value)
        {
            switch(offset)
            {
            case 0x00: rtsr = value & 0x3FFFF; break;
            case 0x04: ftsr = value & 0x3FFFF; break;
            case 0x08: rpr |= value & rtsr; fpr |= value & ftsr; break;   // SWIER
            case 0x0C: rpr &= ~value; break;
            case 0x10: fpr &= ~value; break;
            case 0x60: case 0x64: case 0x68: case 0x6C: exticr[(offset - 0x60) / 4] = value; break;
            case 0x80: imr = value; break;
            case 0x84: emr = value; break;
            }
            Update();
        }

        // called by the GPIO ports on every pin level change
        public void PinChanged(int port, int pin, bool level)
        {
            var sel = (int)((exticr[pin >> 2] >> ((pin & 3) * 8)) & 0xFF);
            if(sel != port) return;
            var bit = 1u << pin;
            if(level && (rtsr & bit) != 0) rpr |= bit;
            if(!level && (ftsr & bit) != 0)
            {
                fpr |= bit;
                if((imr & bit) != 0)
                {
                    this.Log(LogLevel.Info, "EXTI|{0}|FALLING|line={1}|port={2}", G0_PWR.Fmt(Clock.NowUs(machine)), pin, port);
                }
            }
            Update();
        }

        public bool Line23Unmasked { get { return (imr & (1u << 23)) != 0; } }

        private void Update()
        {
            var p = (rpr | fpr) & imr;
            IRQ01.Set((p & 0x0003) != 0);
            IRQ23.Set((p & 0x000C) != 0);
            IRQ415.Set((p & 0xFFF0) != 0);
        }

        private uint rtsr, ftsr, rpr, fpr, imr, emr;
        private uint[] exticr;
        private readonly IMachine machine;
    }

    // ------------------------------------------------------------------ GPIO with pin electrics
    // Pin level = MCU driver (push-pull / open-drain) + external drivers (switches, jumpers, bus)
    // + pull resistors (internal PUPDR or external on the PCB). Conflicts and reads of floating
    // inputs are reported as CHECK errors.
    [AllowedTranslations(AllowedTranslation.ByteToDoubleWord | AllowedTranslation.WordToDoubleWord)]
    public class G0_GPIO : IDoubleWordPeripheral, IKnownSize
    {
        public G0_GPIO(IMachine machine, G0_RCC rcc, G0_EXTI exti, int portIndex, uint modeResetValue = 0xFFFFFFFF,
                       uint pullResetValue = 0, uint externalPullUps = 0, string portName = "P?")
        {
            this.machine = machine;
            this.rcc = rcc;
            this.exti = exti;
            this.portIndex = portIndex;
            this.modeReset = modeResetValue;
            this.pullReset = pullResetValue;
            this.extPullUps = externalPullUps;
            this.portName = portName;
            for(var i = 0; i < 16; i++) external[i] = -1;
            Reset();
        }

        public long Size { get { return 0x400; } }

        public event Action<int, bool> PinChanged;

        public void Reset()
        {
            moder = modeReset; otyper = 0; ospeedr = 0; pupdr = pullReset; odr = 0; afrl = 0; afrh = 0;
            for(var i = 0; i < 16; i++) { last[i] = Resolve(i, false); }
            floatWarned = 0;
        }

        public uint ReadDoubleWord(long offset)
        {
            switch(offset)
            {
            case 0x00: return moder;
            case 0x04: return otyper;
            case 0x08: return ospeedr;
            case 0x0C: return pupdr;
            case 0x10: return ReadIdr();
            case 0x14: return odr;
            case 0x20: return afrl;
            case 0x24: return afrh;
            default: return 0;
            }
        }

        public void WriteDoubleWord(long offset, uint value)
        {
            if(!rcc.PortClock(portIndex))
            {
                this.Log(LogLevel.Error, "CHECK|{0} written at 0x{1:X2} while its clock is off (write ignored)", portName, offset);
                return;
            }
            switch(offset)
            {
            case 0x00: moder = value; break;
            case 0x04: otyper = value & 0xFFFF; break;
            case 0x08: ospeedr = value; break;
            case 0x0C: pupdr = value; break;
            case 0x14: odr = value & 0xFFFF; break;
            case 0x18: odr = (odr | (value & 0xFFFF)) & ~((value >> 16) & ~value & 0xFFFF); break;
            case 0x20: afrl = value; break;
            case 0x24: afrh = value; break;
            case 0x28: odr &= ~(value & 0xFFFF); break;
            }
            Refresh();
        }

        // ---- board side
        // drive: -1 = released, 0 = pulled hard low (switch, jumper), 1 = driven high
        public void SetExternal(int pin, int drive)
        {
            external[pin] = drive;
            Refresh();
        }

        public bool Level(int pin) { return last[pin]; }
        public uint Mode(int pin) { return (moder >> (pin * 2)) & 3u; }
        public bool OpenDrain(int pin) { return ((otyper >> pin) & 1u) != 0; }
        public uint Pull(int pin) { return (pupdr >> (pin * 2)) & 3u; }
        public uint AF(int pin) { return pin < 8 ? (afrl >> (pin * 4)) & 0xF : (afrh >> ((pin - 8) * 4)) & 0xF; }

        public string Describe(int pin)
        {
            var modes = new[] { "in", "out", "af", "analog" };
            return string.Format("{0}{1}:{2}{3}{4}{5}={6}", portName, pin, modes[Mode(pin)], Mode(pin) == 2 ? AF(pin).ToString() : "",
                OpenDrain(pin) ? ",od" : "", Pull(pin) == 1 ? ",pu" : Pull(pin) == 2 ? ",pd" : "", Level(pin) ? 1 : 0);
        }

        private uint ReadIdr()
        {
            uint v = 0;
            for(var i = 0; i < 16; i++)
            {
                if(Mode(i) == 3) continue;   // analog: input buffer off, reads 0
                bool floating;
                var l = Resolve(i, true, out floating);
                if(floating && Mode(i) == 0 && (floatWarned & (1u << i)) == 0)
                {
                    floatWarned |= 1u << i;
                    this.Log(LogLevel.Warning, "CHECK|{0}{1} read while floating (no pull-up, no driver)", portName, i);
                }
                if(l) v |= 1u << i;
            }
            return v;
        }

        private bool Resolve(int pin, bool reading)
        {
            bool f;
            return Resolve(pin, reading, out f);
        }

        private bool Resolve(int pin, bool reading, out bool floating)
        {
            floating = false;
            int mcu = -1;
            var mode = Mode(pin);
            if(mode == 1)
            {
                var o = ((odr >> pin) & 1u) != 0;
                mcu = OpenDrain(pin) ? (o ? -1 : 0) : (o ? 1 : 0);
            }
            var ext = external[pin];
            if(mcu >= 0 && ext >= 0 && mcu != ext)
            {
                if((conflictWarned & (1u << pin)) == 0)
                {
                    conflictWarned |= 1u << pin;
                    this.Log(LogLevel.Error, "CHECK|{0}{1} driver conflict: MCU drives {2}, board drives {3}", portName, pin, mcu, ext);
                }
                return false;
            }
            if(mcu >= 0) return mcu == 1;
            if(ext >= 0) return ext == 1;
            if(((extPullUps >> pin) & 1u) != 0) return true;
            var pull = Pull(pin);
            if(mode != 3 && pull == 1) return true;
            if(mode != 3 && pull == 2) return false;
            floating = true;
            return last[pin];
        }

        private void Refresh()
        {
            for(var i = 0; i < 16; i++)
            {
                var l = Resolve(i, false);
                if(l != last[i])
                {
                    last[i] = l;
                    if(exti != null) exti.PinChanged(portIndex, i, l);
                    var h = PinChanged; if(h != null) h(i, l);
                }
            }
        }

        private uint moder, otyper, ospeedr, pupdr, odr, afrl, afrh, floatWarned, conflictWarned;
        private readonly int[] external = new int[16];
        private readonly bool[] last = new bool[16];
        private readonly IMachine machine;
        private readonly G0_RCC rcc;
        private readonly G0_EXTI exti;
        private readonly int portIndex;
        private readonly uint modeReset, pullReset, extPullUps;
        private readonly string portName;
    }

    // ------------------------------------------------------------------ key matrix with diodes
    // COL -> switch -> diode anode, cathode -> ROW. A column is pulled low only through a
    // pressed key whose row is low. A released column needs columnRiseNs to recover (RC of the
    // pull-up and the column capacitance; see COLUMN_RISE_NS in run_full.py).
    // Mapped at an unused address only so that the monitor can reach it as sysbus.matrix.
    public class UaKbMatrix : IDoubleWordPeripheral, IKnownSize
    {
        public long Size { get { return 0x10; } }
        public uint ReadDoubleWord(long offset) { return 0; }
        public void WriteDoubleWord(long offset, uint value) { }

        public UaKbMatrix(IMachine machine, G0_GPIO gpioA, G0_GPIO gpioB, ulong columnRiseNs = 0)
        {
            this.machine = machine;
            this.a = gpioA;
            this.b = gpioB;
            this.riseNs = columnRiseNs;
            colPort = new G0_GPIO[] { a, a, a, a, a, a, a, a, a, b, a, a };
            colPin = new int[] { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 11, 12 };
            b.PinChanged += (pin, level) => { if(pin < 5) Recompute(); };
            pendingRelease = new int[12];
        }

        public void Reset() { }   // the switches are mechanical: a CPU reset does not release them

        public void Press(int row, int col) { Set(row, col, true); }
        public void Release(int row, int col) { Set(row, col, false); }
        public void ReleaseAll() { for(var i = 0; i < 60; i++) keys[i] = false; Recompute(); }
        public ulong ColumnRiseNs { get { return riseNs; } set { riseNs = value; } }

        // Contact bounce: random make/break chatter for bounceUs, ending in the final state.
        public void Bounce(int row, int col, bool finalPressed, int bounceUs, int seed)
        {
            this.Log(LogLevel.Info, "MATRIX|{0}|BOUNCE|r={1}|c={2}|final={3}|us={4}", G0_PWR.Fmt(Clock.NowUs(machine)), row, col, finalPressed, bounceUs);
            var rnd = new Random(seed);
            double t = 0;
            var state = !finalPressed;
            while(true)
            {
                t += 30 + rnd.Next(300);
                if(t >= bounceUs) break;          // the final state below is the last change
                state = !state;
                var s = state;
                var at = (ulong)(t * 1000);
                machine.ScheduleAction(TimeInterval.FromNanoseconds(at), _ => Set(row, col, s));
            }
            machine.ScheduleAction(TimeInterval.FromNanoseconds((ulong)((bounceUs + 50) * 1000)), _ => Set(row, col, finalPressed));
        }

        private void Set(int row, int col, bool pressed)
        {
            keys[row * 12 + col] = pressed;
            Recompute();
        }

        private void Recompute()
        {
            for(var c = 0; c < 12; c++)
            {
                var low = false;
                for(var r = 0; r < 5; r++)
                {
                    if(keys[r * 12 + c] && !b.Level(r)) { low = true; break; }
                }
                if(low)
                {
                    pendingRelease[c]++;            // cancel a pending rise
                    if(!pulled[c]) { pulled[c] = true; colPort[c].SetExternal(colPin[c], 0); }
                }
                else if(pulled[c])
                {
                    if(riseNs == 0)
                    {
                        pulled[c] = false;
                        colPort[c].SetExternal(colPin[c], -1);
                    }
                    else
                    {
                        var gen = ++pendingRelease[c];
                        var cc = c;
                        machine.ScheduleAction(TimeInterval.FromNanoseconds(riseNs), _ =>
                        {
                            if(pendingRelease[cc] != gen) return;
                            pulled[cc] = false;
                            colPort[cc].SetExternal(colPin[cc], -1);
                        });
                    }
                }
            }
        }

        private readonly bool[] keys = new bool[60];
        private readonly bool[] pulled = new bool[12];
        private readonly int[] pendingRelease;
        private readonly G0_GPIO[] colPort;
        private readonly int[] colPin;
        private readonly G0_GPIO a, b;
        private readonly IMachine machine;
        private ulong riseNs;
    }

    // ------------------------------------------------------------------ I2C1: slave + external bus master
    [AllowedTranslations(AllowedTranslation.ByteToDoubleWord | AllowedTranslation.WordToDoubleWord)]
    public class G0_I2C_Slave : IDoubleWordPeripheral, IKnownSize
    {
        public G0_I2C_Slave(IMachine machine, G0_RCC rcc, G0_GPIO gpioB, G0_EXTI exti, G0_PWR pwr,
                            int sclPin = 6, int sdaPin = 7, int intPin = 5, uint af = 6, long busHz = 400000,
                            ulong timeoutUs = 25000, ulong stopWakeupNs = 5000)
        {
            this.machine = machine;
            this.rcc = rcc;
            this.gpio = gpioB;
            this.exti = exti;
            this.pwr = pwr;
            this.sclPin = sclPin; this.sdaPin = sdaPin; this.intPin = intPin; this.af = af;
            this.busHz = busHz;
            this.timeoutUs = timeoutUs;
            this.stopWakeupNs = stopWakeupNs;
            IRQ = new GPIO();
            pwr.StopChanged += inStop => UpdateIrq();
            Reset();
        }

        public GPIO IRQ { get; private set; }
        public long Size { get { return 0x400; } }

        public void Reset()
        {
            cr1 = cr2 = oar1 = oar2 = timingr = 0;
            txe = true; txis = rxne = addr = nackf = stopf = busy = dir = false;
            rxdr = txdr = 0;
            addcode = 0;
            UpdateIrq();
        }

        // ---------------- registers
        public uint ReadDoubleWord(long offset)
        {
            switch(offset)
            {
            case 0x00: return cr1;
            case 0x04: return cr2;
            case 0x08: return oar1;
            case 0x0C: return oar2;
            case 0x10: return timingr;
            case 0x18: return Isr();
            case 0x24:
                var v = rxdr;
                if(rxne) { rxne = false; UpdateIrq(); Kick(); }
                return v;
            case 0x28: return txdr;
            default: return 0;
            }
        }

        public void WriteDoubleWord(long offset, uint value)
        {
            if(!rcc.Apb1(21)) { this.Log(LogLevel.Error, "CHECK|I2C1 written while I2C1EN=0 (write ignored)"); return; }
            switch(offset)
            {
            case 0x00:
                var wasOn = (cr1 & 1u) != 0;
                cr1 = value;
                if(wasOn && (cr1 & 1u) == 0) { txe = true; txis = rxne = addr = nackf = stopf = false; }
                break;
            case 0x04: cr2 = value; break;
            case 0x08: oar1 = value; break;
            case 0x0C: oar2 = value; break;
            case 0x10: timingr = value; break;
            case 0x18:
                if((value & 1u) != 0 && !txe) { txe = true; }           // flush TXDR
                break;
            case 0x1C:
                if((value & (1u << 3)) != 0) addr = false;
                if((value & (1u << 4)) != 0) nackf = false;
                if((value & (1u << 5)) != 0) stopf = false;
                break;
            case 0x28:
                txdr = (byte)value;
                if(txe) { txe = false; txis = false; }
                else { this.Log(LogLevel.Warning, "CHECK|TXDR written while not empty (byte overwritten)"); }
                break;
            }
            UpdateIrq();
            Kick();
        }

        private uint Isr()
        {
            uint v = 0;
            if(txe) v |= 1u << 0;
            if(txis) v |= 1u << 1;
            if(rxne) v |= 1u << 2;
            if(addr) v |= 1u << 3;
            if(nackf) v |= 1u << 4;
            if(stopf) v |= 1u << 5;
            if(busy) v |= 1u << 15;
            if(dir) v |= 1u << 16;
            v |= (uint)(addcode & 0x7F) << 17;
            return v;
        }

        private bool Enabled(int bit) { return ((cr1 >> bit) & 1u) != 0; }

        private bool IrqLevel()
        {
            var level = (addr && Enabled(3)) || (rxne && Enabled(2)) || (txis && Enabled(1)) || (nackf && Enabled(4)) || (stopf && Enabled(5));
            if(level && pwr.InStop && !exti.Line23Unmasked)
            {
                level = false;   // the wake-up line is masked: the core is not woken
            }
            return level;
        }

        // IrqLatencyNs > 0 delays every rising edge of the interrupt, as if the core were busy in a
        // critical section or a higher-priority handler: used to look for flag-ordering problems.
        private void UpdateIrq()
        {
            var level = IrqLevel();
            if(!level || irqLatencyNs == 0 || IRQ.IsSet)
            {
                IRQ.Set(level);
                return;
            }
            if(irqPending) return;
            irqPending = true;
            machine.ScheduleAction(TimeInterval.FromNanoseconds(irqLatencyNs), _ => { irqPending = false; IRQ.Set(IrqLevel()); });
        }

        public ulong IrqLatencyNs { get { return irqLatencyNs; } set { irqLatencyNs = value; } }

        // ---------------- address phase
        private bool AddressAck(byte address, bool read)
        {
            var now = Clock.NowUs(machine);
            if(!rcc.Apb1(21) || !Enabled(0)) { Note("slave disabled"); return false; }
            if(!PinsOk()) { return false; }
            if((oar1 & (1u << 15)) == 0 || (oar1 & (1u << 10)) != 0 || ((oar1 >> 1) & 0x7F) != address) { return false; }
            if(pwr.InStop)
            {
                if(!Enabled(18)) { this.Log(LogLevel.Error, "CHECK|address match in Stop but WUPEN=0: no ACK"); return false; }
                if(((cr1 >> 8) & 0xFu) != 0) { this.Log(LogLevel.Error, "CHECK|WUPEN needs the digital filter off (DNF=0): no ACK in Stop"); return false; }
                if(rcc.I2c1Sel != 2) { this.Log(LogLevel.Error, "CHECK|address match in Stop but I2C1 kernel clock is not HSI16: no ACK"); return false; }
                wokeFromStop = true;
                this.Log(LogLevel.Info, "HOSTBUS|{0}|WAKE_BY_ADDRESS", G0_PWR.Fmt(now));
            }
            return true;
        }

        private bool PinsOk()
        {
            foreach(var p in new[] { sclPin, sdaPin })
            {
                if(gpio.Mode(p) != 2 || gpio.AF(p) != af || !gpio.OpenDrain(p))
                {
                    this.Log(LogLevel.Error, "CHECK|I2C pin misconfigured: {0} (need AF{1}, open drain)", gpio.Describe(p), af);
                    return false;
                }
            }
            return true;
        }

        private void Note(string s) { this.Log(LogLevel.Info, "HOSTBUS|{0}|{1}", G0_PWR.Fmt(Clock.NowUs(machine)), s); }

        // =====================================================================================
        // External bus master (the ESP32 host). Operations are queued from the monitor and run
        // in virtual time at the configured SCL frequency, with clock stretching by the slave.
        //   W aa b0 b1 ..      write
        //   WR aa reg n        write register pointer, repeated START, read n bytes
        //   R aa n             read n bytes
        //   POLL aa            COUNT then the whole FIFO (the loop from PROTOCOL.md)
        //   WAITINT ms         wait for INT low
        //   DELAY us           idle bus
        //   SPEED hz           SCL frequency
        //   MARK text          marker in the log
        // =====================================================================================
        public void Queue(string op)
        {
            ops.Enqueue(op);
            if(current == null && !scheduled) { scheduled = true; machine.ScheduleAction(TimeInterval.Empty, _ => { scheduled = false; NextOp(); }); }
        }

        public int Pending { get { return ops.Count + (current != null ? 1 : 0); } }
        // Stop 1 -> Run wake-up time used when an address match wakes the MCU (DS12991 table 32)
        public ulong StopWakeupNs { get { return stopWakeupNs; } set { stopWakeupNs = value; } }
        public bool IntLow { get { return !gpio.Level(intPin); } }

        private abstract class Step { }
        private class DelayStep : Step { public double Ns; }
        private class WaitStep : Step { public Func<bool> Cond; public double TimeoutUs; public double PollUs; public bool Stretch; }

        private void NextOp()
        {
            if(current != null) return;
            if(ops.Count == 0) { return; }
            var op = ops.Dequeue();
            opText = op;
            opStart = Clock.NowUs(machine);
            maxStretchUs = 0;
            current = RunOp(op).GetEnumerator();
            Resume();
        }

        private void Resume()
        {
            while(current != null)
            {
                if(waiting != null)
                {
                    if(waiting.Cond())
                    {
                        if(waiting.Stretch) { var s = Clock.NowUs(machine) - waitStart; if(s > maxStretchUs) maxStretchUs = s; }
                        waiting = null;
                        lastWaitTimedOut = false;
                    }
                    else if(Clock.NowUs(machine) - waitStart > waiting.TimeoutUs)
                    {
                        waiting = null;
                        lastWaitTimedOut = true;
                    }
                    else
                    {
                        if(!pollScheduled)
                        {
                            pollScheduled = true;
                            var g = ++pollGen;
                            machine.ScheduleAction(TimeInterval.FromNanoseconds((ulong)(waiting.PollUs * 1000)), _ => { if(g == pollGen) { pollScheduled = false; Resume(); } });
                        }
                        return;
                    }
                }
                if(!current.MoveNext())
                {
                    current = null;
                    NextOp();
                    return;
                }
                var st = current.Current;
                var d = st as DelayStep;
                if(d != null)
                {
                    machine.ScheduleAction(TimeInterval.FromNanoseconds((ulong)d.Ns), _ => Resume());
                    return;
                }
                var w = st as WaitStep;
                if(w != null)
                {
                    waiting = w;
                    waitStart = Clock.NowUs(machine);
                    pollGen++;
                    pollScheduled = false;
                }
            }
        }

        // a register access may have satisfied the condition the master waits for
        private void Kick()
        {
            if(waiting == null || kickScheduled) return;
            kickScheduled = true;
            machine.ScheduleAction(TimeInterval.Empty, _ => { kickScheduled = false; if(waiting != null) { pollGen++; pollScheduled = false; Resume(); } });
        }

        private double BitNs { get { return 1e9 / busHz; } }
        private Step Bits(double n) { return new DelayStep { Ns = n * BitNs }; }
        private Step Stretch(Func<bool> c) { return new WaitStep { Cond = c, TimeoutUs = timeoutUs, PollUs = 20, Stretch = true }; }

        private void Result(string s)
        {
            this.Log(LogLevel.Info, "HOST|{0}|{1}|{2}|stretch_us={3}|dur_us={4}", G0_PWR.Fmt(opStart), opText, s,
                G0_PWR.Fmt(maxStretchUs), G0_PWR.Fmt(Clock.NowUs(machine) - opStart));
        }

        private class Seg { public byte Addr; public bool Read; public byte[] Data; public int Count; }

        private IEnumerable<Step> RunOp(string op)
        {
            var t = op.Trim().Split(new[] { ' ' }, StringSplitOptions.RemoveEmptyEntries);
            var cmd = t[0].ToUpperInvariant();
            if(cmd == "MARK") { Result("MARK"); yield break; }
            if(cmd == "SPEED") { busHz = long.Parse(t[1]); Result("OK"); yield break; }
            if(cmd == "DELAY") { yield return new DelayStep { Ns = double.Parse(t[1], CultureInfo.InvariantCulture) * 1000 }; Result("OK"); yield break; }
            if(cmd == "WAITINT")
            {
                yield return new WaitStep { Cond = () => IntLow, TimeoutUs = double.Parse(t[1], CultureInfo.InvariantCulture) * 1000, PollUs = 10 };
                Result(lastWaitTimedOut ? "TIMEOUT" : "INT_LOW");
                yield break;
            }
            var a = Hex(t[1]);
            List<Seg> segs;
            if(cmd == "W") segs = new List<Seg> { new Seg { Addr = a, Data = t.Skip(2).Select(Hex).ToArray() } };
            else if(cmd == "R") segs = new List<Seg> { new Seg { Addr = a, Read = true, Count = int.Parse(t[2]) } };
            else if(cmd == "WR") segs = new List<Seg> { new Seg { Addr = a, Data = new[] { Hex(t[2]) } }, new Seg { Addr = a, Read = true, Count = int.Parse(t[3]) } };
            else if(cmd == "POLL")
            {
                var got = new List<byte>();
                var res = "";
                foreach(var s in Transfer(new List<Seg> { new Seg { Addr = a, Data = new byte[] { 0x04 } }, new Seg { Addr = a, Read = true, Count = 1 } }, got, r => res = r)) yield return s;
                if(res != "ACK") { Result("COUNT_" + res); yield break; }
                var n = got[0];
                if(n == 0) { Result("EMPTY"); yield break; }
                var ev = new List<byte>();
                foreach(var s in Transfer(new List<Seg> { new Seg { Addr = a, Data = new byte[] { 0x10 } }, new Seg { Addr = a, Read = true, Count = n * 8 } }, ev, r => res = r)) yield return s;
                Result(res + "|count=" + n + "|data=" + string.Join(" ", ev.Select(x => x.ToString("X2"))));
                yield break;
            }
            else { Result("BAD_OP"); yield break; }

            var data = new List<byte>();
            var result = "";
            foreach(var s in Transfer(segs, data, r => result = r)) yield return s;
            Result(result + (data.Count > 0 ? "|data=" + string.Join(" ", data.Select(x => x.ToString("X2"))) : ""));
        }

        private IEnumerable<Step> Transfer(List<Seg> segs, List<byte> readOut, Action<string> done)
        {
            busy = true;
            var first = true;
            foreach(var seg in segs)
            {
                yield return Bits(first ? 1 : 1.5);         // START or repeated START
                first = false;
                yield return Bits(9);                        // address + R/W + ACK bit
                if(!AddressAck(seg.Addr, seg.Read))
                {
                    yield return Bits(1);                    // STOP
                    busy = false;
                    done("NACK_ADDR");
                    yield break;
                }
                dir = seg.Read;
                addcode = seg.Addr;
                addr = true;
                var addrStart = Clock.NowUs(machine);
                if(pwr.InStop && stopWakeupNs > 0)
                {
                    yield return new DelayStep { Ns = stopWakeupNs };   // Stop -> Run wake-up while SCL is held low
                }
                UpdateIrq();
                yield return Stretch(() => !addr);           // SCL held low until ADDRCF
                if(lastWaitTimedOut) { Abort(); done("TIMEOUT_ADDR"); yield break; }
                var held = Clock.NowUs(machine) - addrStart;   // includes the wake-up time
                if(held > maxStretchUs) maxStretchUs = held;

                if(!seg.Read)
                {
                    foreach(var b in seg.Data)
                    {
                        yield return Bits(8);
                        yield return Stretch(() => !rxne);   // previous byte still in RXDR
                        if(lastWaitTimedOut) { Abort(); done("TIMEOUT_RX"); yield break; }
                        rxdr = b; rxne = true; UpdateIrq();
                        yield return Bits(1);                // ACK
                    }
                }
                else
                {
                    for(var i = 0; i < seg.Count; i++)
                    {
                        if(txe)
                        {
                            txis = true; UpdateIrq();
                            yield return Stretch(() => !txe);
                            if(lastWaitTimedOut) { Abort(); done("TIMEOUT_TX"); yield break; }
                        }
                        var v = txdr;
                        txe = true; txis = true; UpdateIrq();   // byte moves to the shift register; ask for the next
                        yield return Bits(8);
                        readOut.Add(v);
                        yield return Bits(1);                // master ACK, NACK on the last byte
                        if(i == seg.Count - 1)
                        {
                            nackf = true; txis = false; UpdateIrq();
                        }
                    }
                }
            }
            yield return Bits(1);                            // STOP
            stopf = true; busy = false; UpdateIrq();
            done("ACK");
        }

        private void Abort()
        {
            busy = false; stopf = true; UpdateIrq();
            this.Log(LogLevel.Error, "CHECK|bus master timed out waiting for the slave (SCL stretched > {0} us)", timeoutUs);
        }

        private static byte Hex(string s) { return byte.Parse(s, NumberStyles.HexNumber); }

        private uint cr1, cr2, oar1, oar2, timingr;
        private bool txe, txis, rxne, addr, nackf, stopf, busy, dir;
        private byte rxdr, txdr, addcode;
        private bool wokeFromStop;

        private readonly Queue<string> ops = new Queue<string>();
        private IEnumerator<Step> current;
        private WaitStep waiting;
        private double waitStart, opStart, maxStretchUs;
        private bool lastWaitTimedOut, pollScheduled, kickScheduled, scheduled, irqPending;
        private ulong irqLatencyNs;
        private int pollGen;
        private string opText;

        private readonly IMachine machine;
        private readonly G0_RCC rcc;
        private readonly G0_GPIO gpio;
        private readonly G0_EXTI exti;
        private readonly G0_PWR pwr;
        private readonly int sclPin, sdaPin, intPin;
        private readonly uint af;
        private long busHz;
        private readonly ulong timeoutUs;
        private ulong stopWakeupNs;
    }
}
