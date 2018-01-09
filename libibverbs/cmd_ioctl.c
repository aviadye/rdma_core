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

static struct ib_uverbs_attr *copy_flow_action_esp(const struct ibv_flow_action_esp *esp,
						   struct ibv_command_buffer *cmd,
						   int handle_idx)
{
	struct ib_uverbs_attr *handle;

	handle = fill_attr_obj(cmd, FLOW_ACTION_ESP_HANDLE, handle_idx);
	if (esp->comp_mask & IBV_FLOW_ACTION_ESP_MASK_ESN)
		fill_attr_in(cmd, FLOW_ACTION_ESP_ESN,
				sizeof(esp->esn), (void *)&esp->esn);

	if (esp->keymat_ptr)
		fill_attr_in_enum(cmd, FLOW_ACTION_ESP_KEYMAT,
				  FLOW_ACTION_ESP_KEYMAT_AES_GCM,
				  esp->keymat_len, esp->keymat_ptr);
	if (esp->replay_ptr)
		fill_attr_in_enum(cmd, FLOW_ACTION_ESP_REPLAY,
				  FLOW_ACTION_ESP_REPLAY_BMP,
				  esp->replay_len, esp->replay_ptr);
	if (esp->esp_encap)
		fill_attr_in_ptr(cmd, FLOW_ACTION_ESP_ENCAP, esp->esp_encap);
	if (esp->esp_attr)
		fill_attr_in_ptr(cmd, FLOW_ACTION_ESP_ATTRS, esp->esp_attr);

	return handle;
}

int ibv_cmd_create_flow_action_esp(struct ibv_context *ctx,
				   const struct ibv_flow_action_esp *attr,
				   struct ibv_flow_action *flow_action,
				   struct ibv_command_buffer *cmd)
{
	int ret;
	struct ib_uverbs_attr *handle = copy_flow_action_esp(attr, cmd, 0);

	ret = execute_ioctl(ctx, cmd);
	if (ret)
		return errno;

	flow_action->context = ctx;
	flow_action->type = IBV_FLOW_ACTION_ESP;
	flow_action->handle = handle->data;

	return 0;
}

int ibv_cmd_destroy_flow_action(struct ibv_flow_action *action)
{
	DECLARE_COMMAND_BUFFER(cmdb, UVERBS_OBJECT_FLOW_ACTION,
			       UVERBS_FLOW_ACTION_DESTROY,
			       DESTROY_FLOW_ACTION_HANDLE);

	fill_attr_obj(cmdb, DESTROY_FLOW_ACTION_HANDLE, action->handle);
	return execute_ioctl(action->context, cmdb);
}

int ibv_cmd_get_num_params(uint16_t object_id, uint16_t method_id)
{
	switch (object_id) {
	case UVERBS_OBJECT_FLOW_ACTION:
		switch (method_id) {
		case UVERBS_FLOW_ACTION_ESP_MODIFY:
		case UVERBS_FLOW_ACTION_ESP_CREATE:
			return FLOW_ACTION_ESP_ENCAP + 1;
		}
	}

	assert(1);
	return 0;
}

int ibv_cmd_modify_flow_action_esp(struct ibv_flow_action *flow_action,
				   const struct ibv_flow_action_esp *attr,
				   struct ibv_command_buffer *cmd)
{
	copy_flow_action_esp(attr, cmd, flow_action->handle);

	return execute_ioctl(flow_action->context, cmd);
}

