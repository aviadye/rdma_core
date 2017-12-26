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

#include <unistd.h>
#include <config.h>
#include <valgrind/memcheck.h>
#include <infiniband/verbs_ioctl.h>

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <alloca.h>
#include <sys/ioctl.h>

#include <infiniband/verbs.h>
#include <infiniband/driver.h>

/*
 * Automatically annotate pointers passed as OUT to the kernel as filled in
 * upon success
 */
static void annotate_buffers(struct ibv_command_buffer *cmd)
{
	struct ib_uverbs_attr *attr;
	int i;

	for (i = 0; i != cmd->hdr.num_attrs; i++) {
		attr = &cmd->attrs[i];

		/* Check if the kernel didn't write to an optional output */
		if (!(attr->flags & UVERBS_ATTR_F_VALID_OUTPUT))
			continue;

		VALGRIND_MAKE_MEM_DEFINED((void *)(uintptr_t)attr->data,
					  attr->len);
	}
}

int execute_ioctl(struct ibv_context *context, struct ibv_command_buffer *cmd)
{
	struct verbs_context *verbs_ctx = verbs_get_ctx(context);

	assert(verbs_ctx);
	cmd->hdr.driver_id = verbs_ctx->driver_id;
	cmd->hdr.num_attrs = cmd->iter - cmd->attrs;
	cmd->hdr.length = sizeof(cmd->hdr) +
			  sizeof(cmd->attrs[0]) * cmd->hdr.num_attrs;
	if (ioctl(context->cmd_fd, RDMA_VERBS_IOCTL, &cmd->hdr))
		return errno;

	annotate_buffers(cmd);

	return 0;
}

/*
 * Fill in the legacy driver specific structures. These all follow the
 * 'common' structures which we do not use anymore.
 */
int _execute_ioctl_legacy(struct ibv_context *context,
			  struct ibv_command_buffer *cmdb, const void *cmd,
			  size_t cmd_common_len, size_t cmd_total_len,
			  void *resp, size_t resp_common_len,
			  size_t resp_total_len)
{
	if (cmd_common_len < cmd_total_len)
		fill_attr_in(cmdb, UVERBS_UHW_IN,
			     cmd_total_len - cmd_common_len,
			     (const uint8_t *)cmd + cmd_common_len);
	if (resp_common_len < resp_total_len)
		fill_attr_in(cmdb, UVERBS_UHW_OUT,
			     resp_total_len - resp_common_len,
			     (const uint8_t *)resp + resp_common_len);

	return execute_ioctl(context, cmdb);
}

int ibv_cmd_get_num_params(uint16_t object_id, uint16_t method_id)
{
	assert(1);
	return 0;
}

