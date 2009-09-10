/*
 *  VMsvga2GLContext.h
 *  VMsvga2Accel
 *
 *  Created by Zenith432 on August 21st 2009.
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

#ifndef __VMSVGA2GLCONTEXT_H__
#define __VMSVGA2GLCONTEXT_H__

#include <IOKit/IOUserClient.h>

typedef unsigned long eIOGLContextModeBits;

class VMsvga2GLContext: public IOUserClient
{
	OSDeclareDefaultStructors(VMsvga2GLContext);

private:
	task_t m_owning_task;
	class VMsvga2Accel* m_provider;
	IOExternalMethod* m_funcs_cache;

public:
	IOExternalMethod* getTargetAndMethodForIndex(IOService** targetP, UInt32 index);
	IOReturn clientClose();
#if 0
	IOReturn clientMemoryForType(UInt32 type, IOOptionBits* options, IOMemoryDescriptor** memory);
	IOReturn externalMethod(uint32_t selector, IOExternalMethodArguments* arguments, IOExternalMethodDispatch* dispatch = 0, OSObject* target = 0, void* reference = 0);
#endif
	/*
	 * Methods overridden from superclass
	 */
	bool start(IOService* provider);
	void stop(IOService* provider);
	bool initWithTask(task_t owningTask, void* securityToken, UInt32 type);
	static VMsvga2GLContext* withTask(task_t owningTask, void* securityToken, UInt32 type);

	/*
	 * Methods corresponding to Apple's GeForce.kext GL Context User Client
	 */
	/*
	 * IONVGLContext
	 */
	IOReturn set_surface(UInt32, eIOGLContextModeBits, UInt32, UInt32);
	IOReturn set_swap_rect(SInt32, SInt32, SInt32, SInt32);
	IOReturn set_swap_interval(SInt32, SInt32);
	IOReturn get_config(UInt32*, UInt32*, UInt32*);
	IOReturn get_surface_size(SInt32*, SInt32*, SInt32*, SInt32*);
	IOReturn get_surface_info(UInt32, SInt32*, SInt32*, SInt32*);
	IOReturn read_buffer(struct sIOGLContextReadBufferData const*);
	IOReturn finish();
	IOReturn wait_for_stamp(UInt32);
	IOReturn new_texture(struct sIOGLNewTextureData const*, struct sIOGLNewTextureReturnData*, size_t struct_in_size, size_t* struct_out_size);
	IOReturn delete_texture(UInt32);
	IOReturn become_global_shared(UInt32);
	IOReturn page_off_texture(struct sIOGLContextPageoffTexture const*);
	IOReturn purge_texture(UInt32);
	IOReturn set_surface_volatile_state(UInt32);
	IOReturn set_surface_get_config_status(struct sIOGLContextSetSurfaceData const*, struct sIOGLContextGetConfigStatus*, size_t struct_in_size, size_t* struct_out_size);
	IOReturn reclaim_resources();
	IOReturn TBD_0x14E0000(UInt32, UInt32);
	IOReturn set_stereo(UInt32, UInt32);
	IOReturn purge_accelerator(UInt32);
	IOReturn get_channel_memory(struct sIOGLChannelMemoryData*, size_t* struct_out_size);
	/*
	 * NVGLContext
	 */
	IOReturn get_query_buffer(UInt32, struct sIOGLGetQueryBuffer*);
	IOReturn get_notifiers(UInt32*, UInt32*);
	IOReturn new_heap_object(struct sNVGLNewHeapObjectData const*, struct sIOGLNewTextureReturnData*, size_t struct_in_size, size_t* struct_out_size);
	IOReturn kernel_printf(char const*, size_t struct_in_size);
	IOReturn nv_rm_config_get(UInt32 const* struct_in, UInt32* struct_out, size_t struct_in_size, size_t* struct_out_size);
	IOReturn nv_rm_config_get_ex(UInt32 const* struct_in, UInt32* struct_out, size_t struct_in_size, size_t* struct_out_size);
	IOReturn nv_client_request(void const* struct_in, void* struct_out, size_t struct_in_size, size_t* struct_out_size);
	IOReturn pageoff_surface_texture(struct sNVGLContextPageoffSurfaceTextureData const*);
	IOReturn get_data_buffer_with_offset(struct sIOGLContextGetDataBuffer*);
	IOReturn nv_rm_control(UInt32 const* struct_in, UInt32* struct_out, size_t struct_in_size, size_t* struct_out_size);
	IOReturn get_power_state(UInt32*, UInt32*);
};

#endif /* __VMSVGA2GLCONTEXT_H__ */
