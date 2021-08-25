/*
 * Copyright (c) Manuel Heiss 2021
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "includes.h"
#include "system/filesys.h"
#include "smbd/smbd.h"
#include "lib/util/tevent_unix.h"

/* Read-direct module.
 *
 * The purpose of this module is to open all files with `O_DIRECT` flag set.
 * Thus a read access to the file will bypass the kernel filesystem cache and read the file content directly from device.
 *
 */

#define MODULE "rdirect"


static int rdirect_openat(struct vfs_handle_struct *handle,
		       const struct files_struct *dirfsp,
		       const struct smb_filename *smb_fname,
		       struct files_struct *fsp,
		       int flags,
		       mode_t mode)
{
	DEBUG(10, ("vfs_rdirect:openat file %s, flags=0x%x, mode=0x%x\n",
	       smb_fname->base_name, flags, mode));

	//add O_DIRECT flag
	if (((flags & O_DIRECTORY) == 0) //don't do it, when the opened file is a directory.
	    && (mode == 0) //special files like /proc/self/fd/.. are opend for example with mode=0x1e4. I don't know if it is good to set the O_DIRECT flag in this case - so in don't do it!
	)
	{
		flags |= O_DIRECT;
	}
	return SMB_VFS_NEXT_OPENAT(handle, dirfsp, smb_fname, fsp, flags, mode);
}



static ssize_t rdirect_pread(vfs_handle_struct *const handle, files_struct * const fsp, void * const data,
			size_t n, const off_t offset)
{
	DEBUG(10, ("vfs_rdirect:pread file %s, data=%p, n=%lu, offset=%ld\n",
	       fsp_str_dbg(fsp), data, n, offset));

	//ensure minimum buffer size
	if (n < 512)
	{
		return -1;
	}

	//direct read requires the destination buffer to be 512bytes aligned!
	//round up to next 512byte boundary
	uintptr_t buffer = (uintptr_t)data;
	buffer = buffer + (512 - 1);
	buffer = buffer & ~((uintptr_t)(512 - 1));
	//get the number of bytes, i have rounded up
	const size_t rndup = (size_t)(buffer - (uintptr_t)data);
	//reduce buffer size by the up-rounded bytes
	n = n - rndup;

	//read file
	ssize_t count = SMB_VFS_NEXT_PREAD(handle, fsp, (void *)buffer, n, offset);
	if (count > 0)
	{
		if (rndup != 0)
		{
			//move the read bytes by the number of bytes I rounded up - back to the position the have to be!
			for (size_t i = 0; i < (size_t)count; ++i)
			{
				((uint8_t *)data)[i] = ((const uint8_t *)buffer)[i];
			}
		}
	}
	return count;
}



struct rdirect_pread_state {
	ssize_t bytes_read;
	struct vfs_aio_state vfs_aio_state;
};

/*
 * Fake up an async read by calling the synchronous API.
 */
static struct tevent_req *rdirect_pread_send(struct vfs_handle_struct *handle,
					      TALLOC_CTX *mem_ctx,
					      struct tevent_context *ev,
					      struct files_struct *fsp,
					      void *data,
					      size_t n, off_t offset)
{
	struct tevent_req *req = NULL;
	struct rdirect_pread_state *state = NULL;
	int ret = -1;

	// DEBUG(10, ("vfs_rdirect:pread_send file %s, data=%p, n=%lu, offset=%ld\n",
	//        fsp_str_dbg(fsp), data, n, offset));

	req = tevent_req_create(mem_ctx, &state, struct rdirect_pread_state);
	if (req == NULL) {
		return NULL;
	}

	ret = rdirect_pread(handle, fsp, data, n, offset);
	if (ret < 0) {
		tevent_req_error(req, ret);
		return tevent_req_post(req, ev);
	}

	state->bytes_read = ret;
	tevent_req_done(req);
	/* Return and schedule the completion of the call. */
	return tevent_req_post(req, ev);
}


static ssize_t rdirect_pread_recv(struct tevent_req *req,
				   struct vfs_aio_state *vfs_aio_state)
{
	struct rdirect_pread_state *state =
		tevent_req_data(req, struct rdirect_pread_state);

	// DEBUG(10, ("vfs_rdirect:pread_recv\n"));

	if (tevent_req_is_unix_error(req, &vfs_aio_state->error)) {
		return -1;
	}
	*vfs_aio_state = state->vfs_aio_state;
	return state->bytes_read;
}



/* VFS operations structure */
static struct vfs_fn_pointers vfs_rdirect_fns = {
	/* File operations */
	.openat_fn = rdirect_openat,
	.pread_fn = rdirect_pread,
	.pread_send_fn = rdirect_pread_send,
	.pread_recv_fn = rdirect_pread_recv
};


static_decl_vfs;
NTSTATUS vfs_rdirect_init(TALLOC_CTX *ctx)
{
	return smb_register_vfs(SMB_VFS_INTERFACE_VERSION, "rdirect",
				&vfs_rdirect_fns);
}
