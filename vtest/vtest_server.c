#include <stdio.h>
#include <stdbool.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/un.h>

#include "vtest.h"
#include "vtest_protocol.h"

static int vtest_open_socket(const char *path)
{
    struct sockaddr_un un;
    int sock;

    sock = socket(PF_UNIX, SOCK_STREAM, 0);
    if (sock < 0) {
	return -1;
    }

    memset(&un, 0, sizeof(un));
    un.sun_family = AF_UNIX;
    
    snprintf(un.sun_path, sizeof(un.sun_path), "%s", path);

    unlink(un.sun_path);

    if (bind(sock, (struct sockaddr *)&un, sizeof(un)) < 0) {
	goto err;
    }
    
    if (listen(sock, 1) < 0){
	goto err;
    }

    return sock;
 err:
    close(sock);
    return -1;
}

int wait_for_socket_accept(int sock)
{
    fd_set read_fds;
    int new_fd;
    int ret;
    FD_ZERO(&read_fds);
    FD_SET(sock, &read_fds);

    ret = select(sock + 1, &read_fds, NULL, NULL, NULL);
    if (ret < 0)
	return ret;

    if (FD_ISSET(sock, &read_fds)) {	
	new_fd = accept(sock, NULL, NULL);
	return new_fd;
    }
    return -1;
}

int wait_for_socket_read(int sock)
{
    fd_set read_fds;

    int ret;
    FD_ZERO(&read_fds);
    FD_SET(sock, &read_fds);

    ret = select(sock + 1, &read_fds, NULL, NULL, NULL);
    if (ret < 0)
	return ret;

    if (FD_ISSET(sock, &read_fds)) {	
      return 0;
    }
    return -1;
}

int main(void)
{
    int sock, new_fd, ret;
    uint32_t header[VTEST_HDR_SIZE];
    static int fence_id = 1;
    bool do_fence;
    sock = vtest_open_socket("/tmp/.virgl_test");
restart:
    new_fd = wait_for_socket_accept(sock);

    vtest_create_renderer(new_fd);
again:
    ret = wait_for_socket_read(new_fd);
    if (ret < 0)
      goto err;

    ret = vtest_block_read(new_fd, &header, sizeof(header));

    if (ret == 8) {
      fprintf(stderr, "got length: %d cmd: %d\n", header[0], header[1]);
      vtest_poll();
      do_fence = false;
      switch (header[1]) {
      case VCMD_GET_CAPS:
	ret = vtest_send_caps();
	break;
      case VCMD_RESOURCE_CREATE:
	ret = vtest_create_resource();
	break;
      case VCMD_RESOURCE_UNREF:
	ret = vtest_resource_unref();
	break;
      case VCMD_SUBMIT_CMD:
	ret = vtest_submit_cmd(header[0]);
	do_fence = true;
	break;
      case VCMD_TRANSFER_GET:
	ret = vtest_transfer_get(header[0]);
	break;
      case VCMD_TRANSFER_PUT:
	ret = vtest_transfer_put(header[0]);
	do_fence = true;
	break;
      case VCMD_RESOURCE_BUSY_WAIT:
	ret = vtest_resource_busy_wait();
      default:
	break;
      }

      if (ret < 0) {
	fprintf(stderr, "socket failed - relistening\n");
	close(new_fd);
	vtest_destroy_renderer();
	goto restart;
      }

      if (do_fence)
	vtest_renderer_create_fence();
      goto again;
    }
    if (ret <= 0) {
      fprintf(stderr, "socket failed - relistening\n");
      close(new_fd);
      vtest_destroy_renderer();
      goto restart;
    }
err:
    close(new_fd);
    close(sock);
}
