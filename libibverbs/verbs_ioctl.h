/*
 * Copyright (c) 2018 Mellanox Technologies, Ltd.  All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef __INFINIBAND_VERBS_IOCTL_H
#define __INFINIBAND_VERBS_IOCTL_H

#include <stddef.h>
#include <string.h>
#include <stdint.h>
#include <assert.h>
#include <rdma/rdma_user_ioctl.h>
#include <rdma/ib_user_ioctl_verbs.h>

struct ibv_context;

struct ibv_command_buffer {
	size_t			allocated_attrs;
	struct ib_uverbs_attr	*iter;
	struct ib_uverbs_ioctl_hdr hdr;
	struct ib_uverbs_attr	attrs[0];
};

/*
 * Constructing an array of ibv_command_buffer is a reasonable way to expand
 * the VLA in hdr.attrs on the stack and also allocate some internal state in
 * a single contiguos stack memory region. It will over-allocate the region in
 * some cases, but this approach allows the number of elements to be dynamic,
 * and not fixed as a compile time constant.
 */
#define _IOCTL_NUM_CMDB(_num_attrs)                                            \
	((sizeof(struct ibv_command_buffer) +                                  \
	  sizeof(struct ib_uverbs_attr) * (_num_attrs) +                       \
	  sizeof(struct ibv_command_buffer) - 1) /                             \
	 sizeof(struct ibv_command_buffer))

/*
 * C99 does not permit an initializer for VLAs, so this function does the init
 * instead. It is called in the wonky way so that DELCARE_COMMAND_BUFFER can
 * still be a 'variable', and we so we don't require C11 mode.
 */
static inline int _ioctl_init_cmbd(struct ibv_command_buffer *cmd,
				   uint16_t object_id, uint16_t method_id,
				   size_t num_attrs)
{
	cmd->allocated_attrs = num_attrs;

	memset(&cmd->hdr, 0, sizeof(cmd->hdr) + sizeof(cmd->attrs[0]) * num_attrs);
	cmd->hdr.object_id = object_id;
	cmd->hdr.method_id = method_id;
	cmd->iter = cmd->attrs;
	return 0;
}

/*
 * Construct a 0 filled IOCTL command buffer on the stack with enough space
 * for _num_attrs elements. This version requires _num_attrs to be a compile
 * time constant.
 */
#define DECLARE_COMMAND_BUFFER(_name, _object_id, _method_id, _num_attrs)      \
	struct ibv_command_buffer _name[_IOCTL_NUM_CMDB(_num_attrs)] = {       \
		{.allocated_attrs = (_num_attrs),                              \
		 .iter = _name[0].attrs,				       \
		 .hdr = {.object_id = (_object_id),                            \
			 .method_id = (_method_id)}}}

/* Used in places that call execute_ioctl_legacy */
#define DECLARE_COMMAND_BUFFER_LEGACY(name, object_id, method_id, num_attrs)   \
	DECLARE_COMMAND_BUFFER(name, object_id, method_id, (num_attrs) + 2)

/*
 * Construct a 0 filled IOCTL command buffer on the stack with enough space
 * for _num_attr local attributes and enough space for all the attributes in
 * _drv.
 */
#define _DECLARE_COMMAND_BUFFER_WITH_DRV(_name, _object_id, _method_id,        \
					_num_attrs)			       \
	struct ibv_command_buffer _name[_IOCTL_NUM_CMDB(_num_attrs)];          \
	int __attribute__((unused)) __##_name##dummy = _ioctl_init_cmbd(       \
		_name, _object_id, _method_id, _num_attrs)

int ibv_cmd_get_num_params(uint16_t object_id, uint16_t method_id);

#define DECLARE_COMMAND_BUFFER_DRV(_name, _object_id, _method_id, _num_drv_attrs) \
	_DECLARE_COMMAND_BUFFER_WITH_DRV(_name, _object_id, _method_id,	       \
					 ibv_cmd_get_num_params(_object_id,    \
								_method_id) +  \
					 _num_drv_attrs)

int execute_ioctl(struct ibv_context *context, struct ibv_command_buffer *cmd);

/*
 * execute, including the legacy driver specific attributes that use the
 * UVERBS_UHW_IN, UVERBS_UHW_OUT
 */
int _execute_ioctl_legacy(struct ibv_context *context,
			  struct ibv_command_buffer *cmdb, const void *cmd,
			  size_t cmd_common_len, size_t cmd_total_len,
			  void *resp, size_t resp_common_len,
			  size_t resp_total_len);
#define execute_ioctl_legacy(context, cmdb, cmd, cmd_size, resp, resp_size)    \
	_execute_ioctl_legacy(context, cmdb, cmd, sizeof(*(cmd)), cmd_size,    \
			      resp, sizeof(*(resp)), resp_size)

/* Make the attribute optional. */
static inline struct ib_uverbs_attr *attr_optional(struct ib_uverbs_attr *attr)
{
	attr->flags &= ~UVERBS_ATTR_F_MANDATORY;
	return attr;
}

static inline unsigned short
_ioctl_get_total_num_attrs(struct ibv_command_buffer *cmd)
{
	return cmd->iter - (struct ib_uverbs_attr *)(&cmd->hdr + 1);
}

static inline struct ib_uverbs_attr *
_ioctl_next_attr(struct ibv_command_buffer *cmd, uint16_t attr_id)
{
	struct ib_uverbs_attr *attr;

	assert(_ioctl_get_total_num_attrs(cmd) < cmd->allocated_attrs);
	attr = cmd->iter++;
	attr->attr_id = attr_id;

	/*
	 * All attributes default to mandatory. Wrapper the fill_* call in
	 * attr_optional() to make it optional.
	 */
	attr->flags = UVERBS_ATTR_F_MANDATORY;

	return attr;
}

/* Send attributes of kernel type UVERBS_ATTR_TYPE_IDR */
static inline struct ib_uverbs_attr *
fill_attr_obj(struct ibv_command_buffer *cmd, uint16_t attr_id, uint32_t idr)
{
	struct ib_uverbs_attr *attr = _ioctl_next_attr(cmd, attr_id);

	attr->data = idr;
	return attr;
}

static inline struct ib_uverbs_attr *
fill_attr_obj_out(struct ibv_command_buffer *cmd, uint16_t attr_id)
{
	return fill_attr_obj(cmd, attr_id, 0);
}

static inline uint64_t get_attr_obj_out(struct ibv_command_buffer *cmd,
					unsigned int idx)
{
	return cmd->attrs[idx].data;
}

/* Send attributes of kernel type UVERBS_ATTR_TYPE_PTR_IN */
static inline struct ib_uverbs_attr *
fill_attr_in(struct ibv_command_buffer *cmd, uint16_t attr_id, size_t len,
	     const void *data)
{
	struct ib_uverbs_attr *attr = _ioctl_next_attr(cmd, attr_id);

	assert(len <= UINT16_MAX);

	attr->len = len;
	if (len <= sizeof(uint64_t))
		memcpy(&attr->data, data, len);
	else
		attr->data = (uintptr_t)data;

	return attr;
}

#define fill_attr_in_ptr(cmd, attr_id, ptr)                                    \
	fill_attr_in(cmd, attr_id, sizeof(*ptr), ptr)

static inline struct ib_uverbs_attr *
fill_attr_uint64(struct ibv_command_buffer *cmd, uint16_t attr_id, uint64_t data)
{
	struct ib_uverbs_attr *attr = _ioctl_next_attr(cmd, attr_id);

	attr->len = sizeof(data);
	attr->data = data;

	return attr;
}

static inline struct ib_uverbs_attr *
fill_attr_uint32(struct ibv_command_buffer *cmd, uint16_t attr_id, uint32_t data)
{
	struct ib_uverbs_attr *attr = _ioctl_next_attr(cmd, attr_id);

	attr->len = sizeof(data);
	attr->data = data;

	return attr;
}

static inline struct ib_uverbs_attr *
fill_attr_in_fd(struct ibv_command_buffer *cmd, uint16_t attr_id, int fd)
{
	struct ib_uverbs_attr *attr;

	if (fd == -1)
		return NULL;

	attr = _ioctl_next_attr(cmd, attr_id);
	attr->data = fd;
	return attr;
}

/* Send attributes of kernel type UVERBS_ATTR_TYPE_PTR_OUT */
static inline struct ib_uverbs_attr *
fill_attr_out(struct ibv_command_buffer *cmd, uint16_t attr_id, size_t len,
	      void *data)
{
	struct ib_uverbs_attr *attr;

	attr = _ioctl_next_attr(cmd, attr_id);

	assert(len <= UINT16_MAX);
	attr->len = len;
	attr->data = (uintptr_t)data;

	return attr;
}

#define fill_attr_out_ptr(cmd, attr_id, ptr)                                   \
	fill_attr_out(cmd, attr_id, sizeof(*(ptr)), (ptr))

static inline struct ib_uverbs_attr *
fill_attr_in_enum(struct ibv_command_buffer *cmd, uint16_t attr_id, uint8_t
		  elem_id, size_t len, void *data)
{
	struct ib_uverbs_attr *attr;

	attr = fill_attr_in(cmd, attr_id, len, data);
	attr->attr_data.enum_data.elem_id = elem_id;

	return attr;
}

#define fill_attr_in_enum_ptr(cmd, attr_id, ptr)			       \
	fill_attr_in_enum(cmd, attr_id, elem_id, sizeof(*(ptr)), (ptr))

#endif
