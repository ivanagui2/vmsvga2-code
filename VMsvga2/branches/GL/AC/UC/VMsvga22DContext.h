/*
 *  VMsvga22DContext.h
 *  VMsvga2Accel
 *
 *  Created by Zenith432 on August 10th 2009.
 *  Copyright 2009 Zenith432. All rights reserved.
 *  Portions Copyright (c) Apple Computer, Inc.
 *
 *  Permission is hereby granted, free of charge, to any person
 *  obtaining a copy of this software and associated documentation
 *  files (the "Software"), to deal in the Software without
 *  restriction, including without limitation the rights to use, copy,
 *  modify, merge, publish, distribute, sublicense, and/or sell copies
 *  of the Software, and to permit persons to whom the Software is
 *  furnished to do so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be
 *  included in all copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 *  EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 *  MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 *  NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 *  BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 *  ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 *  CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 *  SOFTWARE.
 */

#ifndef __VMSVGA22DCONTEXT_H__
#define __VMSVGA22DCONTEXT_H__

#include <IOKit/IOUserClient.h>

typedef uintptr_t eIOContextModeBits;
struct IOSurfacePagingControlInfoStruct;
struct IOSurfaceVsyncControlInfoStruct;

class VMsvga22DContext: public IOUserClient
{
	OSDeclareDefaultStructors(VMsvga22DContext);

private:
	task_t m_owning_task;
	class VMsvga2Accel* m_provider;
	IOExternalMethod* m_funcs_cache;
	SInt32 m_log_level;

	unsigned bTargetIsCGSSurface:1;
	class VMsvga2Surface* surface_client;
	UInt32 framebufferIndex;

	IOReturn locateSurface(UInt32 surface_id);

public:
	IOExternalMethod* getTargetAndMethodForIndex(IOService** targetP, UInt32 index);
	IOReturn clientClose();
#if 0
	IOReturn clientMemoryForType(UInt32 type, IOOptionBits* options, IOMemoryDescriptor** memory);
#endif
	/*
	 * Methods overridden from superclass
	 */
	bool start(IOService* provider);
	bool initWithTask(task_t owningTask, void* securityToken, UInt32 type);
	static VMsvga22DContext* withTask(task_t owningTask, void* securityToken, UInt32 type);

	/*
	 * GA Support Methods
	 */
	IOReturn useAccelUpdates(uintptr_t state);
	IOReturn RectCopy(struct IOBlitCopyRectangleStruct const* copyRects,
					  size_t copyRectsSize);
	IOReturn RectFill(uintptr_t color,
					  struct IOBlitRectangleStruct const* rects,
					  size_t rectsSize);
	IOReturn CopyRegion(uintptr_t source_surface_id,
						intptr_t destX,
						intptr_t destY,
						IOAccelDeviceRegion const* region,
						size_t regionSize);

	/*
	 * Methods corresponding to Apple's GeForce.kext 2D Context User Client
	 */
	/*
	 * IONV2DContext
	 */
	IOReturn set_surface(uintptr_t surface_id, eIOContextModeBits options, void* struct_out, size_t* struct_out_size);
	IOReturn get_config(UInt32* config_1, UInt32* config_2);
	IOReturn get_surface_info1(uintptr_t, eIOContextModeBits, void*, size_t*);
	IOReturn swap_surface(uintptr_t options, UInt32* swapFlags);
	IOReturn scale_surface(uintptr_t options, uintptr_t width, uintptr_t height);
	IOReturn lock_memory(uintptr_t options, UInt64* struct_out, size_t* struct_out_size);
	IOReturn unlock_memory(uintptr_t options, UInt32* swapFlags);
	IOReturn finish(uintptr_t options);
	IOReturn declare_image(UInt64 const*, UInt64*, size_t, size_t*);
	IOReturn create_image(uintptr_t, uintptr_t, UInt64*, size_t*);
	IOReturn create_transfer(uintptr_t, uintptr_t, UInt64*, size_t*);
	IOReturn delete_image(uintptr_t image_id);
	IOReturn wait_image(uintptr_t image_id);
	IOReturn set_surface_paging_options(IOSurfacePagingControlInfoStruct const*,
										IOSurfacePagingControlInfoStruct*,
										size_t,
										size_t*);
	IOReturn set_surface_vsync_options(IOSurfaceVsyncControlInfoStruct const*,
									   IOSurfaceVsyncControlInfoStruct*,
									   size_t,
									   size_t*);
	IOReturn set_macrovision(uintptr_t new_state);

	/*
	 * NV2DContext
	 */
	IOReturn read_configs(UInt32 const*, UInt32*, size_t, size_t*);
	IOReturn read_config_Ex(UInt32 const*, UInt32*, size_t, size_t*);
	IOReturn get_surface_info2(UInt32 const*, UInt32*, size_t, size_t*);
	IOReturn kernel_printf(char const*, size_t);
};

#endif /* __VMSVGA22DCONTEXT_H__ */