/*-
 * EnsoniqAudioPCI.cpp
 * EnsoniqAudioPCI
 *
 * Created by Zenith432 on July 14th 2009.
 *
 * Support the ENSONIQ AudioPCI board and Creative Labs SoundBlaster PCI
 * boards based on the ES1370, ES1371 and ES1373 chips.
 *
 * Copyright (c) 2009 Zenith432.
 * Copyright (c) 2005 Maxxuss.
 * Copyright (c) 1999 Russell Cattelan <cattelan@thebarn.com>
 * Copyright (c) 1999 Cameron Grant <cg@freebsd.org>
 * Copyright (c) 1998 by Joachim Kuebart.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * 3. All advertising materials mentioning features or use of this
 *    software must display the following acknowledgement:
 *	This product includes software developed by Joachim Kuebart.
 *
 * 4. The name of the author may not be used to endorse or promote
 *    products derived from this software without specific prior
 *    written permission.
 *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.	IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Part of this code was heavily inspired by the linux driver from
 * Thomas Sailer (sailer@ife.ee.ethz.ch)
 * Just about everything has been touched and reworked in some way but
 * the all the underlying sequences/timing/register values are from
 * Thomas' code.
 *
 */

/*
 * Some of the code was adapted from AppleAC97AudioAMDCS5535.cpp and is subject
 *   to the APSL Version 1.1, see http://www.opensource.apple.com/apsl
 */

#include <IOKit/pci/IOPCIDevice.h>
#include <IOKit/IOLib.h>
#include <IOKit/IOWorkLoop.h>
#include <IOKit/IOFilterInterruptEventSource.h>
#include <pexpert/i386/protos.h>
#include <IOAC97CodecDevice.h>
#include <IOKit/audio/IOAudioEngine.h>
#include <IOKit/audio/IOAudioDefines.h>
#include <IOKit/IODeviceTreeSupport.h>
#include "EnsoniqAudioPCI.h"
#include "es137x.h"
#include "es137x_xtra.h"

#define DMA_STATES_SIZE (sizeof(DMAEngineState) * kDMAEngineCount)

#define ENGINE_TO_CHANNEL(engine) gDMAEngineDir[engine]

#define FORMAT_MONO 0
#define FORMAT_STEREO 1
#define FORMAT_8BIT 0
#define FORMAT_16BIT 2

#define CLASS EnsoniqAudioPCI
#define super IOAC97Controller
OSDefineMetaClassAndStructors(EnsoniqAudioPCI, IOAC97Controller);

extern OSSymbol const* gIODTNameKey;

static __attribute__((used)) char const copyright[] = "Copyright 2009-2010 Zenith432";

#pragma mark -
#pragma mark Power States
#pragma mark -

enum {
	kPowerStateOff = 0,
	kPowerStateDoze,
	kPowerStateOn,
	kPowerStateCount
};

static IOPMPowerState gPowerStates[kPowerStateCount] =
{
	{ kIOPMPowerStateVersion1, 0,                 0,            0,            0, 0, 0, 0, 0, 0, 0, 0 },
	{ kIOPMPowerStateVersion1, 0,                 kIOPMDoze,    kIOPMDoze,    0, 0, 0, 0, 0, 0, 0, 0 },
	{ kIOPMPowerStateVersion1, kIOPMDeviceUsable, kIOPMPowerOn, kIOPMPowerOn, 0, 0, 0, 0, 0, 0, 0, 0 }
};

#pragma mark -
#pragma mark Supported DMA Engines
#pragma mark -

enum {
	kColdReset, kWarmReset
};

enum {
	kDMAEnginePCMOut  = 0,
	kDMAEnginePCMIn   = 1,
	kDMAEngineCount   = 2,
	kDMAEngineInvalid = 0xFF
};

#define kDMAEngineSupportMask ((1 << kDMAEnginePCMOut) | (1 << kDMAEnginePCMIn))

enum {
	kEngineIdle      = 0x00,
	kEngineActive    = 0x01,
	kEngineRunning   = 0x02,
	kEngineInterrupt = 0x80
};

struct DMAEngineState
{
	IOOptionBits flags;						// offset 0
	IOBufferMemoryDescriptor* sampleMemory;	// offset 4
	IOPhysicalAddress sampleMemoryPhysAddr;	// offset 8
	bool interruptReady;					// offset 12
	void* interruptTarget;					// offset 16
	IOAC97DMAEngineAction interruptAction;	// offset 20
	void* interruptParam;					// offset 24
};

static int const gDMAEngineDir[2] = { kIOAC97DMADataDirectionOutput, kIOAC97DMADataDirectionInput };

#pragma mark -
#pragma mark Overridden Methods from IOAC97Controller
#pragma mark -

#define BDOOR_MAGIC 0x564D5868U
#define BDOOR_PORT 0x5658U
#define BDOOR_CMD_GETVERSION 10

static int vmware_checkpresent(void)
{
	unsigned eax = BDOOR_MAGIC, ebx = ~BDOOR_MAGIC, ecx = BDOOR_CMD_GETVERSION, edx = BDOOR_PORT, esi = 0, edi = 0;
	__asm__ volatile ("in %%dx, %0": "+a" (eax), "+b" (ebx), "+c" (ecx), "+d" (edx), "+S" (esi), "+D" (edi));
	return ebx == BDOOR_MAGIC;
}

static bool checkDMAEngineID(IOAC97DMAEngineID engine)
{
#if 0
	if (engine > 31)
		return false;
	if (!((1 << (engine)) & kDMAEngineSupportMask))
		return false;
#else
	if (engine >= kDMAEngineCount)
		return false;
#endif
	return true;
}

IOReturn CLASS::startDMAEngine(IOAC97DMAEngineID engine, IOOptionBits options)
{
	DMAEngineState* dma;

	if (!checkDMAEngineID(engine))
		return kIOReturnBadArgument;
	dma = &fDMAState[engine];
	if (!(dma->flags & kEngineActive))
		return kIOReturnNotReady;
	if (dma->flags & kEngineRunning)
		return kIOReturnSuccess;
	if (dma->flags & kEngineInterrupt)
		dma->interruptReady = true;
	eschan_trigger(ENGINE_TO_CHANNEL(engine), 2);
	dma->flags |= kEngineRunning;
	return kIOReturnSuccess;
}

void CLASS::stopDMAEngine(IOAC97DMAEngineID engine)
{
	DMAEngineState* dma;

	if (!checkDMAEngineID(engine))
		return;
	dma = &fDMAState[engine];
	if (!(dma->flags & kEngineRunning))
		return;
	eschan_trigger(ENGINE_TO_CHANNEL(engine), 1);
	IOSleep(10);
	dma->interruptReady = false;
	dma->flags &= ~kEngineRunning;
}

IOByteCount CLASS::getDMAEngineHardwarePointer(IOAC97DMAEngineID engine)
{
	if (fVMwareBugs)
		return fFrameCountCache[ENGINE_TO_CHANNEL(engine)];
	return eschan_getptr(ENGINE_TO_CHANNEL(engine));
}

IOReturn CLASS::codecRead(IOAC97CodecID codec, IOAC97CodecOffset offset, IOAC97CodecWord* word)
{
	if (codec > 1 || offset >= kCodecRegisterCount)
		return kIOReturnBadArgument;
	*word = static_cast<IOAC97CodecWord>(es1371_rdcd(static_cast<int>(offset)));
	return kIOReturnSuccess;
}

IOReturn CLASS::codecWrite(IOAC97CodecID codec, IOAC97CodecOffset offset, IOAC97CodecWord word)
{
	if (codec > 1 || offset >= kCodecRegisterCount)
		return kIOReturnBadArgument;
	es1371_wrcd(static_cast<int>(offset), word);
	return kIOReturnSuccess;
}

IOReturn CLASS::prepareAudioConfiguration(IOAC97AudioConfig* config)
{
	if (!config)
		return kIOReturnBadArgument;
	if (!selectDMAEngineForConfiguration(config))
		return kIOReturnUnsupported;
	if (!selectSlotMapsForConfiguration(config))
		return kIOReturnUnsupported;
	return super::prepareAudioConfiguration(config);
}

IOReturn CLASS::activateAudioConfiguration(IOAC97AudioConfig* config, void* target, IOAC97DMAEngineAction action, void* param)
{
	DMAEngineState* dma;
    IOAC97DMAEngineID engine;
	IOByteCount size;
	IOReturn r;

	if (!config)
		return kIOReturnBadArgument;
	engine = config->getDMAEngineID();
	if (!checkDMAEngineID(engine))
		return kIOReturnBadArgument;
	dma = &fDMAState[engine];
	if (dma->flags != kEngineIdle)
		return kIOReturnBusy;
	hwActivateConfiguration(config);	// Added
	if (!dma->sampleMemory) {
		dma->sampleMemory = IOBufferMemoryDescriptor::inTaskWithPhysicalMask(kernel_task,
																			 kIOMemoryPhysicallyContiguous,
																			 static_cast<mach_vm_size_t>(fBufferNumPages) << PAGE_SHIFT,
																			 0xFFFFFFFFULL & -PAGE_SIZE);
		if (!dma->sampleMemory)
			return kIOReturnNoMemory;
		if (dma->sampleMemory->prepare() != kIOReturnSuccess) {
			dma->sampleMemory->release();
			dma->sampleMemory = 0;
			return kIOReturnNoMemory;
		}
	}
	bzero(dma->sampleMemory->getBytesNoCopy(), dma->sampleMemory->getCapacity());
	if (!dma->sampleMemoryPhysAddr) {
		dma->sampleMemoryPhysAddr = dma->sampleMemory->getPhysicalSegment(0, &size);
	}
	if (target && action) {
		fInterruptSource->disable();
		dma->interruptReady = false;
		dma->interruptTarget = target;
		dma->interruptAction = action;
		dma->interruptParam = param;
		if (engine == kDMAEnginePCMOut)
			fEnginePCMOut = OSDynamicCast(IOAudioEngine, static_cast<OSObject*>(target));	// just to be safe
		fInterruptSource->enable();
		dma->flags |= kEngineInterrupt;
	}
	dma->flags |= kEngineActive;
	eschan_prepare(ENGINE_TO_CHANNEL(engine),
				   static_cast<UInt>(dma->sampleMemoryPhysAddr),
				   fBufferNumPages << PAGE_SHIFT,
				   fBufferNumPages << (PAGE_SHIFT - 2 - fPartitionShift),
				   FORMAT_STEREO | FORMAT_16BIT,
				   /* kIOAC97SampleRate48K */ config->getSampleRate());		// Added
	config->setDMABufferMemory(dma->sampleMemory);
	config->setDMABufferCount(fBufferNumPages);
	config->setDMABufferSize(PAGE_SIZE);
	r = super::activateAudioConfiguration(config, target, action, param);
	if (r != kIOReturnSuccess)
		deactivateAudioConfiguration(config);
	return r;
}

void CLASS::deactivateAudioConfiguration(IOAC97AudioConfig* config)
{
	DMAEngineState* dma;
	IOAC97DMAEngineID engine;

	if (!config)
		return;
	engine = config->getDMAEngineID();
	if (!checkDMAEngineID(engine))
		return;
	dma = &fDMAState[engine];
	if (!(dma->flags & kEngineActive))
		return;
	if (dma->flags & kEngineRunning)
		stopDMAEngine(engine);
	if (dma->flags & kEngineInterrupt) {
		fInterruptSource->disable();
		dma->interruptReady = false;
		dma->interruptTarget = 0;
		dma->interruptAction = 0;
		dma->interruptParam = 0;
		if (engine == kDMAEnginePCMOut)
			fEnginePCMOut = 0;
		fInterruptSource->enable();
	}
	hwDeactivateConfiguration(config);
	dma->flags = kEngineIdle;
	super::deactivateAudioConfiguration(config);
}

#pragma mark -
#pragma mark Methods from FreeBSD es137x driver
#pragma mark -

__attribute__((visibility("hidden")))
UInt CLASS::es_rd(int regno, int size)
{
	switch (size) {
		case 1:
			return inb(fIOBase + regno);
		case 2:
			return inw(fIOBase + regno);
		case 4:
#ifdef __x86_64__
			{
				UInt data;
				__asm__ ("inl %1, %0" : "=a" (data) : "d" (static_cast<UInt16>(fIOBase + regno)));
				return data;
			}
#else
			return inl(fIOBase + regno);
#endif
		default:
			return 0xFFFFFFFFU;
	}
}

__attribute__((visibility("hidden")))
void CLASS::es_wr(int regno, UInt data, int size)
{
	switch (size) {
		case 1:
			outb(fIOBase + regno, static_cast<UInt8>(data));
			break;
		case 2:
			outw(fIOBase + regno, static_cast<UInt16>(data));
			break;
		case 4:
#ifdef __x86_64__
			__asm__ volatile ("outl %1, %0" : : "d" (static_cast<UInt16>(fIOBase + regno)), "a" (data));
#else
			outl(fIOBase + regno, data);
#endif
			break;
	}
}

__attribute__((visibility("hidden")))
UInt CLASS::es1371_wait_src_ready()
{
	UInt t, r;

	for (t = 0; t < 4096U; ++t) {
		if (!((r = es_rd(ES1371_REG_SMPRATE, 4)) &
			  ES1371_SRC_RAM_BUSY))
			return r;
		IODelay(2);
	}
	IOLog("%s: wait src ready timeout 0x%x [0x%x]\n", getName(), ES1371_REG_SMPRATE, r);
	return 0;
}

__attribute__((visibility("hidden")))
void CLASS::es1371_src_write(UInt16 reg, UInt16 data)
{
	UInt r;

	r = es1371_wait_src_ready() & (ES1371_DIS_SRC | ES1371_DIS_P1 |
								   ES1371_DIS_P2 | ES1371_DIS_R1);
	r |= ES1371_SRC_RAM_ADDRO(reg) |  ES1371_SRC_RAM_DATAO(data);
	es_wr(ES1371_REG_SMPRATE, r | ES1371_SRC_RAM_WE, 4);
}

__attribute__((visibility("hidden")))
UInt CLASS::es1371_src_read(UInt16 reg)
{
	UInt r;

	r = es1371_wait_src_ready() & (ES1371_DIS_SRC | ES1371_DIS_P1 |
								   ES1371_DIS_P2 | ES1371_DIS_R1);
	r |= ES1371_SRC_RAM_ADDRO(reg);
	es_wr(ES1371_REG_SMPRATE, r, 4);
	return (ES1371_SRC_RAM_DATAI(es1371_wait_src_ready()));
}

__attribute__((visibility("hidden")))
UInt CLASS::es1371_dac_rate(UInt rate, int channel)
{
	UInt freq, r, result, dac, dis;

	if (rate > kIOAC97SampleRate48K)
		rate = kIOAC97SampleRate48K;
	if (rate < 4000)
		rate = 4000;
	freq = ((rate << 15) + 1500) / 3000;
	result = (freq * 3000) >> 15;

	dac = (channel == ES_DAC1) ? ES_SMPREG_DAC1 : ES_SMPREG_DAC2;
	dis = (channel == ES_DAC1) ? ES1371_DIS_P2 : ES1371_DIS_P1;
	r = (es1371_wait_src_ready() & (ES1371_DIS_SRC | ES1371_DIS_P1 |
									ES1371_DIS_P2 | ES1371_DIS_R1));
	es_wr(ES1371_REG_SMPRATE, r, 4);
	es1371_src_write(dac + ES_SMPREG_INT_REGS,
					 (es1371_src_read(dac + ES_SMPREG_INT_REGS) & 0x00ff) |
					 ((freq >> 5) & 0xfc00));
	es1371_src_write(dac + ES_SMPREG_VFREQ_FRAC, freq & 0x7fff);
	r = (es1371_wait_src_ready() &
		 (ES1371_DIS_SRC | dis | ES1371_DIS_R1));
	es_wr(ES1371_REG_SMPRATE, r, 4);
	return (result);
}

__attribute__((visibility("hidden")))
UInt CLASS::es1371_adc_rate(UInt rate, int channel)
{
	UInt n, truncm, freq, result;

	if (rate > kIOAC97SampleRate48K)
		rate = kIOAC97SampleRate48K;
	if (rate < 4000)
		rate = 4000;
	n = rate / 3000;
	if ((1 << n) & ((1 << 15) | (1 << 13) | (1 << 11) | (1 << 9)))
		n--;
	truncm = (21 * n - 1) | 1;
	freq = ((48000U << 15) / rate) * n;
	result = (48000U << 15) / (freq / n);
	if (channel) {
		if (rate >= 24000) {
			if (truncm > 239)
				truncm = 239;
			es1371_src_write(ES_SMPREG_ADC + ES_SMPREG_TRUNC_N,
							 (((239 - truncm) >> 1) << 9) | (n << 4));
		} else {
			if (truncm > 119)
				truncm = 119;
			es1371_src_write(ES_SMPREG_ADC + ES_SMPREG_TRUNC_N,
							 0x8000 | (((119 - truncm) >> 1) << 9) | (n << 4));
		}
		es1371_src_write(ES_SMPREG_ADC + ES_SMPREG_INT_REGS,
						 (es1371_src_read(ES_SMPREG_ADC + ES_SMPREG_INT_REGS) &
						  0x00ff) | ((freq >> 5) & 0xfc00));
		es1371_src_write(ES_SMPREG_ADC + ES_SMPREG_VFREQ_FRAC,
						 freq & 0x7fff);
		es1371_src_write(ES_SMPREG_VOL_ADC, n << 8);
		es1371_src_write(ES_SMPREG_VOL_ADC + 1, n << 8);
	}
	return (result);
}

__attribute__((visibility("hidden")))
int CLASS::es1371_init()
{
	UInt cssr;
#if 0
	UInt devid, revid, subdev;
#endif
	int idx;

	/* This is NOT ES1370 */
#if 0
	es->escfg = ES_SET_IS_ES1370(es->escfg, 0);
	num = 0;
#endif
	ctrl = 0;	// Added
	sctrl = 0;
#if 0
	cssr = 0;
	devid = pci_get_devid(es->dev);
	revid = pci_get_revid(es->dev);
	subdev = (pci_get_subdevice(es->dev) << 16) |
	pci_get_subvendor(es->dev);
	/*
	 * Joyport blacklist. Either we're facing with broken hardware
	 * or because this hardware need special (unknown) initialization
	 * procedures.
	 */
	switch (subdev) {
		case 0x20001274:	/* old Ensoniq */
			es->ctrl = 0;
			break;
		default:
			es->ctrl = CTRL_JYSTK_EN;
			break;
	}
	if (devid == CT4730_PCI_ID) {
		/* XXX amplifier hack? */
		es->ctrl |= (1 << 16);
	}
#endif
	/* initialize the chips */
	es_wr(ES1370_REG_CONTROL, ctrl, 4);
	es_wr(ES1370_REG_SERIAL_CONTROL, sctrl, 4);
	es_wr(ES1371_REG_LEGACY, 0, 4);
#if 0
	if ((devid == ES1371_PCI_ID && revid == ES1371REV_ES1373_8) ||
	    (devid == ES1371_PCI_ID && revid == ES1371REV_CT5880_A) ||
	    (devid == CT5880_PCI_ID && revid == CT5880REV_CT5880_C) ||
	    (devid == CT5880_PCI_ID && revid == CT5880REV_CT5880_D) ||
	    (devid == CT5880_PCI_ID && revid == CT5880REV_CT5880_E)) {
#endif
		cssr = 1 << 29;
		es_wr(ES1370_REG_STATUS, cssr, 4);
		IODelay(20000);
#if 0
	}
#endif
	/* AC'97 warm reset to start the bitclk */
#if 0
	es_wr(ES1370_REG_CONTROL, es->ctrl, 4);
	es_wr(ES1371_REG_LEGACY, ES1371_SYNC_RES, 4);
#else	// Added: correction
	es_wr(ES1370_REG_CONTROL, ctrl | ES1371_SYNC_RES, 4);
#endif
	IODelay(2000);
	es_wr(ES1370_REG_CONTROL, ctrl, 4);
	es1371_wait_src_ready();
	/* Init the sample rate converter */
	es_wr(ES1371_REG_SMPRATE, ES1371_DIS_SRC, 4);
	for (idx = 0; idx < 0x80; ++idx)
		es1371_src_write(idx, 0);
	es1371_src_write(ES_SMPREG_DAC1 + ES_SMPREG_TRUNC_N, 16 << 4);
	es1371_src_write(ES_SMPREG_DAC1 + ES_SMPREG_INT_REGS, 16 << 10);
	es1371_src_write(ES_SMPREG_DAC2 + ES_SMPREG_TRUNC_N, 16 << 4);
	es1371_src_write(ES_SMPREG_DAC2 + ES_SMPREG_INT_REGS, 16 << 10);
	es1371_src_write(ES_SMPREG_VOL_ADC, 1 << 12);
	es1371_src_write(ES_SMPREG_VOL_ADC + 1, 1 << 12);
	es1371_src_write(ES_SMPREG_VOL_DAC1, 1 << 12);
	es1371_src_write(ES_SMPREG_VOL_DAC1 + 1, 1 << 12);
	es1371_src_write(ES_SMPREG_VOL_DAC2, 1 << 12);
	es1371_src_write(ES_SMPREG_VOL_DAC2 + 1, 1 << 12);
	es1371_adc_rate(22050, ES_ADC);
	es1371_dac_rate(22050, ES_DAC1);
	es1371_dac_rate(22050, ES_DAC2);
	/*
	 * WARNING:
	 * enabling the sample rate converter without properly programming
	 * its parameters causes the chip to lock up (the SRC busy bit will
	 * be stuck high, and I've found no way to rectify this other than
	 * power cycle)
	 */
	es1371_wait_src_ready();
	es_wr(ES1371_REG_SMPRATE, 0, 4);
	/* try to reset codec directly */
	es_wr(ES1371_REG_CODEC, 0, 4);
	es_wr(ES1370_REG_STATUS, cssr, 4);

	return (0);
}

__attribute__((visibility("hidden")))
int CLASS::es1371_wrcd(int addr, UInt data)
{
	UInt t, x, orig;

	for (t = 0; t < 4096U; ++t) {
		if (!(es_rd(ES1371_REG_CODEC, 4) & CODEC_WIP))		// Note: bugfix
			break;
	}
	/* save the current state for later */
	x = orig = es_rd(ES1371_REG_SMPRATE, 4);
	/* enable SRC state data in SRC mux */
	es_wr(ES1371_REG_SMPRATE, (x & (ES1371_DIS_SRC | ES1371_DIS_P1 |
									ES1371_DIS_P2 | ES1371_DIS_R1)) | 0x00010000, 4);
	/* busy wait */
	for (t = 0; t < 4096U; ++t) {
	  	if ((es_rd(ES1371_REG_SMPRATE, 4) & 0x00870000) ==
		    0x00000000)
			break;
	}
	/* wait for a SAFE time to write addr/data and then do it, dammit */
	for (t = 0; t < 4096U; ++t) {
		if ((es_rd(ES1371_REG_SMPRATE, 4) & 0x00870000) ==
			0x00010000)
			break;
	}

	es_wr(ES1371_REG_CODEC, ((addr << CODEC_POADD_SHIFT) &
							 CODEC_POADD_MASK) | ((data << CODEC_PODAT_SHIFT) &
													  CODEC_PODAT_MASK), 4);
	/* restore SRC reg */
	es1371_wait_src_ready();
	es_wr(ES1371_REG_SMPRATE, orig, 4);

	return (0);
}

__attribute__((visibility("hidden")))
int CLASS::es1371_rdcd(int addr)
{
	UInt t, x, orig;

	for (t = 0; t < 4096U; ++t) {
		if (!(es_rd(ES1371_REG_CODEC, 4) & CODEC_WIP))
			break;
	}

	/* save the current state for later */
	x = orig = es_rd(ES1371_REG_SMPRATE, 4);
	/* enable SRC state data in SRC mux */
	es_wr(ES1371_REG_SMPRATE, (x & (ES1371_DIS_SRC | ES1371_DIS_P1 |
									ES1371_DIS_P2 | ES1371_DIS_R1)) | 0x00010000, 4);
	/* busy wait */
	for (t = 0; t < 4096U; ++t) {
		if ((es_rd(ES1371_REG_SMPRATE, 4) & 0x00870000) ==
			0x00000000)
			break;
	}
	/* wait for a SAFE time to write addr/data and then do it, dammit */
	for (t = 0; t < 4096U; ++t) {
		if ((es_rd(ES1371_REG_SMPRATE, 4) & 0x00870000) ==
			0x00010000)
			break;
	}

	es_wr(ES1371_REG_CODEC, ((addr << CODEC_POADD_SHIFT) &
							 CODEC_POADD_MASK) | CODEC_PORD, 4);

	/* restore SRC reg */
	es1371_wait_src_ready();
	es_wr(ES1371_REG_SMPRATE, orig, 4);

	/* now wait for the stinkin' data (RDY) */
	for (t = 0; t < 4096U; ++t) {
		if ((x = es_rd(ES1371_REG_CODEC, 4)) & CODEC_RDY)
			break;
	}

	return ((x & CODEC_PIDAT_MASK) >> CODEC_PIDAT_SHIFT);
}

__attribute__((visibility("hidden")))
UInt CLASS::eschan_prepare(int channel, UInt snd_dbuf, UInt bufsz, UInt cnt, UInt format, UInt rate)
{
	UInt bufcnt, startptr, dispatchptr;

	bufcnt = bufsz >> 2;
	startptr = (bufcnt - 1) & 0xFFFFU;
	dispatchptr = 0;
	if (fVMwareBugs && channel != ES_ADC) {
		startptr |= (bufcnt - (bufcnt >> 1)) << 16;
		dispatchptr = bufsz - bufcnt;
	}
	if (channel >= 0 && channel < 3) {
		fFrameCountCache[channel] = 0;
		fStartingFrameCount[channel] = startptr;
		fDispatchPoint[channel] = dispatchptr;
	}
	switch (channel) {
		case ES_DAC1:
			ctrl &= ~CTRL_DAC1_EN;
			es_wr(ES1370_REG_CONTROL, ctrl, 4);
			es_wr(ES1370_REG_MEMPAGE, ES1370_REG_DAC1_FRAMEADR >> 8, 1);
			es_wr(ES1370_REG_DAC1_FRAMEADR & 0xFFU, snd_dbuf, 4);
			es_wr(ES1370_REG_DAC1_FRAMECNT & 0xFFU, startptr, 4);
			es_wr(ES1370_REG_DAC1_SCOUNT, cnt - 1, 4);
			sctrl &= ~SCTRL_P1FMT;
			if (format & FORMAT_16BIT)
				sctrl |= SCTRL_P1SEB;
			if (format & FORMAT_STEREO)
				sctrl |= SCTRL_P1SMB;
			sctrl &= ~(SCTRL_P1LOOPSEL | SCTRL_P1PAUSE | SCTRL_P1SCTRLD);
			sctrl |= SCTRL_P1INTEN;
			es_wr(ES1370_REG_SERIAL_CONTROL, sctrl, 4);
			return es1371_dac_rate(rate, channel);
		case ES_DAC2:
			ctrl &= ~CTRL_DAC2_EN;
			es_wr(ES1370_REG_CONTROL, ctrl, 4);
			es_wr(ES1370_REG_MEMPAGE, ES1370_REG_DAC2_FRAMEADR >> 8, 1);
			es_wr(ES1370_REG_DAC2_FRAMEADR & 0xFFU, snd_dbuf, 4);
			es_wr(ES1370_REG_DAC2_FRAMECNT & 0xFFU, startptr, 4);
			es_wr(ES1370_REG_DAC2_SCOUNT, cnt - 1, 4);
			sctrl &= ~SCTRL_P2FMT;
			if (format & FORMAT_16BIT)
				sctrl |= SCTRL_P2SEB;
			if (format & FORMAT_STEREO)
				sctrl |= SCTRL_P2SMB;
			sctrl &= ~(SCTRL_P2ENDINC | SCTRL_P2STINC | SCTRL_P2LOOPSEL | SCTRL_P2PAUSE | SCTRL_P2DACSEN);
			sctrl |= SCTRL_P2INTEN;
			sctrl |= ((((format >> 1) & 1) + 1) << SCTRL_SH_P2ENDINC);
			es_wr(ES1370_REG_SERIAL_CONTROL, sctrl, 4);
			return es1371_dac_rate(rate, channel);
		case ES_ADC:
			ctrl &= ~CTRL_ADC_EN;
			es_wr(ES1370_REG_CONTROL, ctrl, 4);
			es_wr(ES1370_REG_MEMPAGE, ES1370_REG_ADC_FRAMEADR >> 8, 1);
			es_wr(ES1370_REG_ADC_FRAMEADR & 0xFFU, snd_dbuf, 4);
			es_wr(ES1370_REG_ADC_FRAMECNT & 0xFFU, startptr, 4);
			es_wr(ES1370_REG_ADC_SCOUNT, cnt - 1, 4);
			sctrl &= ~SCTRL_R1FMT;
			if (format & FORMAT_16BIT)
				sctrl |= SCTRL_R1SEB;
			if (format & FORMAT_STEREO)
				sctrl |= SCTRL_R1SMB;
			sctrl &= ~SCTRL_R1LOOPSEL;
			sctrl |= SCTRL_R1INTEN;
			es_wr(ES1370_REG_SERIAL_CONTROL, sctrl, 4);
			return es1371_adc_rate(rate, 1);
	}
	return 0;
}

__attribute__((visibility("hidden")))
int CLASS::eschan_trigger(int channel, int go)
{
	UInt flag;

	switch (channel) {
		case ES_DAC1:
			flag = CTRL_DAC1_EN;
			break;
		case ES_DAC2:
			flag = CTRL_DAC2_EN;
			break;
		case ES_ADC:
			flag = CTRL_ADC_EN;
			break;
		default:
			return 0;
	}

	if (go == 2)
		ctrl |= flag;
	else
		ctrl &= ~flag;
	if (go == 2) {
		fFrameCountCache[channel] = 0;
		if (fVMwareBugs && channel != ES_ADC)
			fStartPhase |= 1U << channel;
		eschan_load_starting_ptr(channel);
	}
	es_wr(ES1370_REG_CONTROL, ctrl, 4);
	if (go != 2)
		fFrameCountCache[channel] = 0;
	return 0;
}

__attribute__((visibility("hidden")))
UInt CLASS::eschan_getptr(int channel)
{
	UInt reg;

	switch (channel) {
		case ES_DAC1:
			reg = ES1370_REG_DAC1_FRAMECNT;
			break;
		case ES_DAC2:
			reg = ES1370_REG_DAC2_FRAMECNT;
			break;
		case ES_ADC:
			reg = ES1370_REG_ADC_FRAMECNT;
			break;
		default:
			return 0;
	}
	es_wr(ES1370_REG_MEMPAGE, reg >> 8, 1);
	return (es_rd(reg & 0xFFU, 4) & 0xFFFF0000U) >> 14;
}

__attribute__((visibility("hidden")))
UInt CLASS::eschan_getptr_and_cache(int channel)
{
	UInt ptr = eschan_getptr(channel);

	if (fStartPhase & (1U << channel))
		return ptr;
	fFrameCountCache[channel] = ptr;
	return ptr;
}

__attribute__((visibility("hidden")))
UInt CLASS::eschan_getSampleCounter(int channel)
{
	UInt reg, r;

	switch (channel) {
		case ES_DAC1:
			reg = ES1370_REG_DAC1_SCOUNT;
			break;
		case ES_DAC2:
			reg = ES1370_REG_DAC2_SCOUNT;
			break;
		case ES_ADC:
			reg = ES1370_REG_ADC_SCOUNT;
			break;
		default:
			return 0;
	}
	r = es_rd(reg, 4);
	return ((r & 0xFFFFU) << 2) - ((r & 0xFFFF0000U) >> 14);
}

__attribute__((visibility("hidden")))
UInt CLASS::es_intr()
{
	UInt intsrc, sctrl_;

	intsrc = es_rd(ES1370_REG_STATUS, 4);
	if (!(intsrc & STAT_INTR))
		return 0;

	sctrl_ = sctrl;
	if (intsrc & STAT_ADC)
		sctrl_ &= ~SCTRL_R1INTEN;
	if (intsrc & STAT_DAC1)
		sctrl_ &= ~SCTRL_P1INTEN;
	if (intsrc & STAT_DAC2)
		sctrl_ &= ~SCTRL_P2INTEN;

	es_wr(ES1370_REG_SERIAL_CONTROL, sctrl_, 4);
	es_wr(ES1370_REG_SERIAL_CONTROL, sctrl, 4);

	return intsrc & (STAT_ADC | STAT_DAC2 | STAT_DAC1);
}

__attribute__((visibility("hidden")))
void CLASS::eschan_load_starting_ptr(int channel)
{
	UInt reg;

	switch (channel) {
		case ES_DAC1:
			reg = ES1370_REG_DAC1_FRAMECNT;
			break;
		case ES_DAC2:
			reg = ES1370_REG_DAC2_FRAMECNT;
			break;
		case ES_ADC:
			reg = ES1370_REG_ADC_FRAMECNT;
			break;
		default:
			return;
	}
	es_wr(ES1370_REG_MEMPAGE, reg >> 8, 1);
	es_wr(reg & 0xFFU, fStartingFrameCount[channel], 4);
}

#pragma mark -
#pragma mark Overridden Methods from IOService
#pragma mark -

/*
 * Added
 */
bool CLASS::init(OSDictionary* dictionary)
{
	if (!super::init(dictionary))
		return false;
	fPCI = 0;
	fWorkLoop = 0;
	fInterruptSource = 0;
	fACLinkPowerDown = false;
	fDMAState = 0;
	fIOBase = 0;
	fSetPowerStateThreadCall = 0;
	bzero(&fCodecs[0], sizeof fCodecs);
	fBusyOutputSlots = 0;
	ctrl = 0;
	sctrl = 0;
	fVMwareBugs = 1;
	fBufferNumPages = 32U;
	return true;
}

bool CLASS::start(IOService* provider)
{
	if (!provider)
		return false;
	if (!super::start(provider))
		return false;
	fBusyOutputSlots = kIOAC97Slot_0 | kIOAC97Slot_1 | kIOAC97Slot_2;
	fVMwareBugs = vmware_checkpresent();
	processBootOptions();
	fDMAState = static_cast<DMAEngineState*>(IOMalloc(DMA_STATES_SIZE));
	if (!fDMAState) {
		super::stop(provider);
		return false;
	}
	bzero(fDMAState, DMA_STATES_SIZE);
	fSetPowerStateThreadCall = thread_call_allocate(handleSetPowerState, this);
	if (!fSetPowerStateThreadCall) {
		super::stop(provider);
		return false;
	}
	fEnginePCMOut = 0;
	fWorkLoop = IOWorkLoop::workLoop();
	if (!fWorkLoop) {
		super::stop(provider);
		return false;
	}
	if (!provider->open(this)) {
		super::stop(provider);
		return false;
	}
	if (!configureProvider(provider)) {
		provider->close(this);
		super::stop(provider);
		return false;
	}
	resetACLink(kColdReset);
	if (!attachCodecDevices()) {
		provider->close(this);
		super::stop(provider);
		return false;
	}
	fInterruptSource = IOFilterInterruptEventSource::filterInterruptEventSource(
		this,
		interruptOccurred,
		interruptFilter,
		provider);
	if (!fInterruptSource ||
		fWorkLoop->addEventSource(fInterruptSource) != kIOReturnSuccess) {
		IOLog("%s: failed to attach interrupt event source\n", getName());
		provider->close(this);
		super::stop(provider);
		return false;
	}
	fInterruptSource->enable();
	provider->close(this);
	PMinit();
	registerPowerDriver(this, &gPowerStates[0], kPowerStateCount);
	provider->joinPMtree(this);
	setProperty(kIOAC97HardwareNameKey, "ES137x");
	publishCodecDevices();
	return true;
}

void CLASS::stop(IOService* provider)
{
	resetACLink(kWarmReset);
	PMstop();
	super::stop(provider);
}

void CLASS::free()
{
	UInt i;

	if (fDMAState) {
		for (i = 0; i < kDMAEngineCount; ++i) {
			DMAEngineState* dma = &fDMAState[i];
			if (!dma->sampleMemory)
				continue;
			dma->sampleMemory->complete();
			dma->sampleMemory->release();
		}
		bzero(fDMAState, DMA_STATES_SIZE);
		IOFree(fDMAState, DMA_STATES_SIZE);
		fDMAState = 0;
	}
	for (i = 0; i < kIOAC97MaxCodecCount; ++i)
		if (fCodecs[i]) {
			fCodecs[i]->release();
			fCodecs[i] = 0;
		}
	if (fSetPowerStateThreadCall) {
		thread_call_free(fSetPowerStateThreadCall);
		fSetPowerStateThreadCall = 0;
	}
	if (fInterruptSource) {
		fInterruptSource->disable();
		if (fWorkLoop)
			fWorkLoop->removeEventSource(fInterruptSource);
		fInterruptSource->release();
		fInterruptSource = 0;
	}
	if (fWorkLoop) {
		fWorkLoop->release();
		fWorkLoop = 0;
	}
	super::free();
}

IOReturn CLASS::setPowerState(unsigned long powerStateOrdinal, IOService* whatDevice)
{
	retain();
	if (TRUE == thread_call_enter1(fSetPowerStateThreadCall, reinterpret_cast<thread_call_param_t>(powerStateOrdinal)))
		release();
	return (10 * 1000 * 1000);	// 10 seconds
}

#pragma mark -
#pragma mark Private Methods
#pragma mark -

__attribute__((visibility("hidden")))
void CLASS::handleSetPowerState(thread_call_param_t param0, thread_call_param_t param1)
{
	CLASS* self = static_cast<CLASS*>(param0);
	unsigned long powerStateOrdinal = reinterpret_cast<unsigned long>(param1);

	if (gPowerStates[powerStateOrdinal].capabilityFlags & kIOPMDeviceUsable) {
		if (self->fACLinkPowerDown) {
			self->resetACLink(kColdReset);
			for (int i = 0; i < kIOAC97MaxCodecCount; ++i)
				if (self->fCodecs[i])
					self->waitCodecReady(i);
			self->fACLinkPowerDown = false;
		}
	} else
		self->fACLinkPowerDown = true;
	self->acknowledgeSetPowerState();
	self->release();
}

__attribute__((visibility("hidden")))
bool CLASS::interruptFilter(OSObject* owner, IOFilterInterruptEventSource* source)
{
	CLASS* self = static_cast<CLASS*>(owner);
	DMAEngineState* dma;
	UInt r;
#ifdef FAST_ERASE
	bool bDACIntr = false;
#endif

	r = self->es_intr();
	if (!r)
		return false;
	if (!self->fVMwareBugs)
		goto dispatch;
#ifdef FAST_ERASE
	bDACIntr = ((r & (STAT_DAC2 | STAT_DAC1)) != 0);
#endif
	if (r & STAT_DAC2) {
		UInt ptr = self->eschan_getptr(ES_DAC2);
		if (self->fStartPhase & (1U << ES_DAC2)) {
			r &= ~STAT_DAC2;
			if (!ptr)
				self->fStartPhase &= ~(1 << ES_DAC2);
		} else {
			if (ptr != self->fDispatchPoint[ES_DAC2])
				r &= ~STAT_DAC2;
			self->fFrameCountCache[ES_DAC2] = ptr;
		}
	}
	if (r & STAT_DAC1) {
		UInt ptr = self->eschan_getptr(ES_DAC1);
		if (self->fStartPhase & (1U << ES_DAC1)) {
			r &= ~STAT_DAC1;
			if (!ptr)
				self->fStartPhase &= ~(1 << ES_DAC1);
		} else {
			if (ptr != self->fDispatchPoint[ES_DAC1])
				r &= ~STAT_DAC1;
			self->fFrameCountCache[ES_DAC1] = ptr;
		}
	}
	if ((r & STAT_ADC) && self->eschan_getptr_and_cache(ES_ADC))
		r &= ~STAT_ADC;
	if (!r)
		goto skip_adc;
dispatch:
	dma = &self->fDMAState[kDMAEnginePCMOut];
#ifdef FAST_ERASE
	if (!dma->interruptReady)
		bDACIntr = false;
#endif
	if (dma->interruptReady &&
		(r & (STAT_DAC2 | STAT_DAC1)) != 0 /* &&
		dma->interruptTarget != 0 &&
		(static_cast<IOAudioEngine*>(dma->interruptTarget)->numActiveUserClients == 0 */) {
		dma->interruptAction(dma->interruptTarget, dma->interruptParam);
	}
	dma = &self->fDMAState[kDMAEnginePCMIn];
	if (dma->interruptReady &&
		(r & STAT_ADC) != 0 /* &&
		dma->interruptTarget != 0 &&
		(static_cast<IOAudioEngine*>(dma->interruptTarget)->numActiveUserClients == 0 */) {
		dma->interruptAction(dma->interruptTarget, dma->interruptParam);
	}
skip_adc:
#ifdef FAST_ERASE
	if (bDACIntr &&
		self->fEnginePCMOut != 0)
		return true;
#endif
	return false;
}

__attribute__((visibility("hidden")))
void CLASS::interruptOccurred(OSObject* owner, IOInterruptEventSource* source, int count)
{
#ifdef FAST_ERASE
	CLASS* me = static_cast<CLASS*>(owner);
	IOWorkLoop* wl;

	if (!me || !me->fEnginePCMOut)
		return;
	wl = me->fEnginePCMOut->getWorkLoop();
	if (!wl)
		return;
	wl->runAction(engineAction, me->fEnginePCMOut, 0, 0, 0, 0);
#endif /* FAST_ERASE */
}

#ifdef FAST_ERASE
__attribute__((visibility("hidden")))
IOReturn CLASS::engineAction(OSObject* target, void* arg0, void* arg1, void* arg2, void* arg3)
{
	IOAudioEngine* engine = static_cast<IOAudioEngine*>(target);
	if (engine) {
		engine->performErase();
		engine->performFlush();
	}
	return kIOReturnSuccess;
}
#endif /* FAST_ERASE */

__attribute__((visibility("hidden")))
bool CLASS::configureProvider(IOService* provider)
{
	OSString* s;
	OSData* d;

	fPCI = OSDynamicCast(IOPCIDevice, provider);
	if (!fPCI)
		return false;
	fPCI->setMemoryEnable(false);
	fPCI->setIOEnable(true);
	fPCI->setBusMasterEnable(true);
	fIOBase = static_cast<UInt16>(fPCI->configRead32(kIOPCIConfigBaseAddress0));
	if (!(fIOBase & 1))
		return false;
	fIOBase &= ~1;
	if (fPCI->hasPCIPowerManagement(kPCIPMCPMESupportFromD3Cold))
		fPCI->enablePCIPowerManagement(kPCIPMCSPowerStateD3);
	s = OSDynamicCast(OSString, getProperty(kIOAudioEngineDescriptionKey));
	if (s) {
		fPCI->setProperty(gIODTNameKey, s);
		d = OSData::withBytes(s->getCStringNoCopy(), s->getLength() + 1);
		if (d) {
			fPCI->setProperty(gIODTModelKey, d);
			d->release();
		}
	}
	return true;
}

__attribute__((visibility("hidden")))
void CLASS::processBootOptions()
{
	UInt boot_arg;

	if (PE_parse_boot_argn("es_oso", &boot_arg, sizeof boot_arg))
		setProperty(kIOAudioEngineSampleOffsetKey, static_cast<UInt64>(boot_arg), 32U);
	if (PE_parse_boot_argn("es_iso", &boot_arg, sizeof boot_arg))
		setProperty(kIOAudioEngineInputSampleOffsetKey, static_cast<UInt64>(boot_arg), 32U);
	if (PE_parse_boot_argn("es_osl", &boot_arg, sizeof boot_arg))
		setProperty(kIOAudioEngineOutputSampleLatencyKey, static_cast<UInt64>(boot_arg), 32U);
	if (PE_parse_boot_argn("es_isl", &boot_arg, sizeof boot_arg))
		setProperty(kIOAudioEngineInputSampleLatencyKey, static_cast<UInt64>(boot_arg), 32U);
	if (PE_parse_boot_argn("es_mco", &boot_arg, sizeof boot_arg))
		setProperty("IOAudioEngineMixClipOverhead", static_cast<UInt64>(boot_arg), 32U);
	if (PE_parse_boot_argn("es_stable", &boot_arg, sizeof boot_arg))
		setProperty(kIOAudioEngineClockIsStableKey, boot_arg ? true : false);
	if (PE_parse_boot_argn("es_cabfs", &boot_arg, sizeof boot_arg))
		setProperty("IOAudioEngineCoreAudioBufferFrameSize", static_cast<UInt64>(boot_arg), 32U);
	if (PE_parse_boot_argn("-es_debug", &boot_arg, sizeof boot_arg))
		setProperty("IOAudioEngineDebug", true);
	if (PE_parse_boot_argn("es_bm", &boot_arg, sizeof boot_arg))
		if (1U <= boot_arg && boot_arg <= 64U)
			fBufferNumPages = boot_arg;
	setProperty("EnsoniqAudioPCIBufferNumPages", static_cast<UInt64>(fBufferNumPages), 32U);
	if (fVMwareBugs) {
		/*
		 * During testing it was found that
		 * On OS 10.6 using 8 parts/buffer reduces the probability of CoreAudio overloads.
		 * On OS 10.5 using 8 parts/buffer does not reduce the probability of overloads,
		 *   and also makes them less pleasant to the ear.
		 *
		 * So the default is 4 ppb on Leopard and 8 ppb on SnowLeopard.
		 *   --zenith432 Nov 22 2010
		 */
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ >= 1060
		fPartitionShift = 3;
#else
		fPartitionShift = 2;
#endif
		if (PE_parse_boot_argn("es_bp", &boot_arg, sizeof boot_arg))
			switch (boot_arg) {
				case 4:
					fPartitionShift = 2;
					break;
				case 8:
					fPartitionShift = 3;
					break;
			}
	}
	setProperty("EnsoniqAudioPCIBufferNumPartitions", 1ULL << fPartitionShift, 32U);
}

__attribute__((visibility("hidden")))
IOItemCount CLASS::attachCodecDevices()
{
	IOAC97CodecDevice* codec = createCodecDevice(0);
	if (!codec)
		return 0;
	if (!codec->attach(this)) {
		codec->release();
		return 0;
	}
	fCodecs[0] = codec;
	return 1;
}

__attribute__((visibility("hidden")))
void CLASS::publishCodecDevices()
{
	for (int i = kIOAC97MaxCodecCount - 1; i >= 0; --i)
		if (fCodecs[i])
			fCodecs[i]->registerService();
}

__attribute__((visibility("hidden")))
IOAC97CodecDevice* CLASS::createCodecDevice(IOAC97CodecID codecID)
{
	return IOAC97CodecDevice::codec(this, codecID);
}

__attribute__((visibility("hidden")))
bool CLASS::selectDMAEngineForConfiguration(IOAC97AudioConfig* config)
{
	config->setDMAEngineID(kDMAEngineInvalid);
	if (config->getDMAEngineType() != kIOAC97DMAEngineTypeAudioPCM)
		return false;
	if (config->getSampleFormat() != kIOAC97SampleFormatPCM16)	// Added
		return false;
	switch (config->getDMADataDirection()) {
		case kIOAC97DMADataDirectionOutput:
			config->setDMAEngineID(kDMAEnginePCMOut);
			break;
		case kIOAC97DMADataDirectionInput:
			config->setDMAEngineID(kDMAEnginePCMIn);
			break;
		default:
			return false;
	}
	return true;
}

__attribute__((visibility("hidden")))
bool CLASS::selectSlotMapsForConfiguration(IOAC97AudioConfig* config)
{
	IOAC97DMAEngineID engine;
	IOItemCount channels;
	IOOptionBits want;
	IOOptionBits map[4];
	IOOptionBits slotsOK;
	int count = 0;

	channels = config->getAudioChannelCount();
	engine = config->getDMAEngineID();
	slotsOK  = ~fBusyOutputSlots;
	switch (engine) {
		case kDMAEnginePCMOut:
			want = 0;
			switch (channels) {
				case 2:
					want = kIOAC97Slot_3_4;
					break;
			}
			if (!want || ((want & slotsOK) != want))
				break;
			map[0] = want;
			count = 1;
			break;
		case kDMAEnginePCMIn:
			if (channels == 2)
				map[count++] = kIOAC97Slot_3_4;
			break;
	}
	if (count) {
		config->setDMAEngineSlotMaps(&map[0], count);
		return true;
	}
	return false;
}

__attribute__((visibility("hidden")))
bool CLASS::waitCodecReady(IOAC97CodecID codecID)
{
	return es1371_wait_src_ready() != 0;
}

__attribute__((visibility("hidden")))
bool CLASS::hwActivateConfiguration(IOAC97AudioConfig const* config)
{
	if (config->getDMADataDirection() == kIOAC97DMADataDirectionOutput)
		fBusyOutputSlots |= config->getSlotMap();
	return true;
}

__attribute__((visibility("hidden")))
void CLASS::hwDeactivateConfiguration(IOAC97AudioConfig const* config)
{
	if (config->getDMADataDirection() == kIOAC97DMADataDirectionOutput)
		fBusyOutputSlots &= ~config->getSlotMap();
}
