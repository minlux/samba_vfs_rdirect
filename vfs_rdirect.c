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

#include <stdio.h>
#include <unistd.h>
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




static ssize_t rdirect_pread(vfs_handle_struct *const handle, files_struct * const fsp, void * const data,
         size_t n, const off_t offset)
{
   DEBUG(10, ("vfs_rdirect:pread file %s, data=%p, n=%lu, offset=%ld\n",
          fsp_str_dbg(fsp), data, n, offset));

   //ATTENTION: This simple implementation doesn't support fragmented read.
   //Instead, everything must be read at once. Subsequent reads (with an offset > 0) will return immediattly and imply "end of file" to the caller!
   //Thus, if the file is bigger than the buffer (given by {data, n}) the content will be truncated.
   if (offset > 0)
   {
      return 0; //end of file
   }

   //ensure minimum buffer size
   if (n < 512)
   {
      return -1;
   }

   //do read
   //get path to the file (from file descriptor)
   char filePath[256];
   snprintf(filePath, sizeof(filePath), "/proc/self/fd/%d", fsp_get_io_fd(fsp));
   // DEBUG(10, ("vfs_rdirect:pread try to get filename of %s\n", filePath));
   int len = readlink(filePath, filePath, sizeof(filePath) - 1); //You can use readlink on /proc/self/fd/NNN where NNN is the file descriptor. This will give you the name of the file as it was when it was opened
   if (len <= 0)
   {
      DEBUG(10, ("vfs_rdirect:pread Failed to read filename.\n"));
      return -1;
   }
   filePath[len] = 0;


   //open file (for direct read)
   int fd = open(filePath, O_RDONLY | O_DIRECT); //here we open the file for read with O_DIRECT flag set!!!
   if (fd <= 0)
   {
      DEBUG(10, ("vfs_rdirect:pread Failed to open file %s. Code %d\n",
            filePath, fd));
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
   n = n & ~((size_t)(512 - 1)); //round down to multiple of 512

   //read file
   ssize_t count = read(fd, (void *)buffer, n);
   close(fd); //close file
   if (rndup != 0)
   {
      //move the read bytes by the number of bytes I rounded up - back to the position the have to be!
      for (size_t i = 0; i < (size_t)count; ++i)
      {
         ((uint8_t *)data)[i] = ((const uint8_t *)buffer)[i];
      }
   }
   ((uint8_t *)data)[count] = 0; //add zero termination
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
