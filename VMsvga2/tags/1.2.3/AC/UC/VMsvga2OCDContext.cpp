/*
 *  VMsvga2OCDContext.cpp
 *  VMsvga2Accel
 *
 *  Created by Zenith432 on October 11th 2009.
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

#include <IOKit/IOLib.h>
#include "Vlog.h"
#include "VMsvga2Accel.h"
#include "VMsvga2OCDContext.h"
#include "ACMethods.h"

#define CLASS VMsvga2OCDContext
#define super IOUserClient
OSDefineMetaClassAndStructors(VMsvga2OCDContext, IOUserClient);

#if LOGGING_LEVEL >= 1
#define OCDLog(log_level, fmt, ...) do { if (log_level <= m_log_level) VLog("IOOCD: ", fmt, ##__VA_ARGS__); } while (false)
#else
#define OCDLog(log_level, fmt, ...)
#endif

static IOExternalMethod iofbFuncsCache[kIOVMOCDNumMethods] =
{
// IONVOCDContext
	{0, reinterpret_cast<IOMethod>(&CLASS::finish), kIOUCScalarIScalarO, 0, 0},
	{0, reinterpret_cast<IOMethod>(&CLASS::wait_for_stamp), kIOUCScalarIScalarO, 1, 0},
// NVOCDContext
	{0, reinterpret_cast<IOMethod>(&CLASS::check_error_notifier), kIOUCScalarIStructO, 0, 16U},
	{0, reinterpret_cast<IOMethod>(&CLASS::mark_texture_for_ocd_use), kIOUCScalarIScalarO, 1, 0},
	{0, reinterpret_cast<IOMethod>(&CLASS::FreeEvent), kIOUCScalarIScalarO, 0, 0},
	{0, reinterpret_cast<IOMethod>(&CLASS::GetHandleIndex), kIOUCScalarIScalarO, 0, 2},
};

#pragma mark -
#pragma mark struct definitions
#pragma mark -

struct NvNotificationRec
{
	UInt32 data[4];
};

#pragma mark -
#pragma mark IOUserClient Methods
#pragma mark -

IOExternalMethod* CLASS::getTargetAndMethodForIndex(IOService** targetP, UInt32 index)
{
	if (index >= kIOVMOCDNumMethods)
		OCDLog(1, "%s(%p, %u)\n", __FUNCTION__, targetP, index);
	if (!targetP || index >= kIOVMOCDNumMethods)
		return 0;
#if 0
	if (index >= kIOVMOCDNumMethods) {
		if (m_provider)
			*targetP = m_provider;
		else
			return 0;
	} else
		*targetP = this;
#else
	*targetP = this;
#endif
	return &m_funcs_cache[index];
}

IOReturn CLASS::clientClose()
{
	OCDLog(2, "%s\n", __FUNCTION__);
	if (!terminate(0))
		IOLog("%s: terminate failed\n", __FUNCTION__);
	m_owning_task = 0;
	m_provider = 0;
	return kIOReturnSuccess;
}

IOReturn CLASS::clientMemoryForType(UInt32 type, IOOptionBits* options, IOMemoryDescriptor** memory)
{
	OCDLog(1, "%s(%u, %p, %p)\n", __FUNCTION__, type, options, memory);
	return super::clientMemoryForType(type, options, memory);
}

IOReturn CLASS::connectClient(IOUserClient* client)
{
	OCDLog(1, "%s(%p)\n", __FUNCTION__, client);
	return super::connectClient(client);
}

IOReturn CLASS::registerNotificationPort(mach_port_t port, UInt32 type, UInt32 refCon)
{
	OCDLog(1, "%s(%p, %u, %u)\n", __FUNCTION__, port, type, refCon);
	return super::registerNotificationPort(port, type, refCon);
}

bool CLASS::start(IOService* provider)
{
	m_provider = OSDynamicCast(VMsvga2Accel, provider);
	if (!m_provider)
		return false;
	m_log_level = m_provider->getLogLevelAC();
	return super::start(provider);
}

bool CLASS::initWithTask(task_t owningTask, void* securityToken, UInt32 type)
{
	m_log_level = 1;
	if (!super::initWithTask(owningTask, securityToken, type))
		return false;
	m_owning_task = owningTask;
	m_funcs_cache = &iofbFuncsCache[0];
	return true;
}

CLASS* CLASS::withTask(task_t owningTask, void* securityToken, UInt32 type)
{
	CLASS* inst;

	inst = new CLASS;

	if (inst && !inst->initWithTask(owningTask, securityToken, type))
	{
		inst->release();
		inst = 0;
	}

	return (inst);
}

#pragma mark -
#pragma mark IONVOCDContext Methods
#pragma mark -

IOReturn CLASS::finish()
{
	OCDLog(2, "%s()\n", __FUNCTION__);
	return kIOReturnUnsupported;
}

IOReturn CLASS::wait_for_stamp(uintptr_t c1)
{
	OCDLog(2, "%s(%lu)\n", __FUNCTION__, c1);
	return kIOReturnUnsupported;
}

#pragma mark -
#pragma mark NVOCDContext Methods
#pragma mark -

IOReturn CLASS::check_error_notifier(struct NvNotificationRec volatile* struct_out, size_t* struct_out_size)
{
	OCDLog(2, "%s(%p, %lu)\n", __FUNCTION__, struct_out, *struct_out_size);
	if (*struct_out_size < sizeof *struct_out)
		return kIOReturnBadArgument;
	return kIOReturnUnsupported;
}

IOReturn CLASS::mark_texture_for_ocd_use(uintptr_t c1)
{
	OCDLog(2, "%s(%lu)\n", __FUNCTION__, c1);
	return kIOReturnUnsupported;
}

IOReturn CLASS::FreeEvent()
{
	OCDLog(2, "%s()\n", __FUNCTION__);
	return kIOReturnUnsupported;
}

IOReturn CLASS::GetHandleIndex(UInt32*, UInt32*)
{
	OCDLog(2, "%s(out1, out2)\n", __FUNCTION__);
	return kIOReturnUnsupported;
}