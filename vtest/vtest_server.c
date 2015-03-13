#include <stdio.h>
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
    sock = vtest_open_socket("/tmp/.virgl_test");

    new_fd = wait_for_socket_accept(sock);

    vtest_create_renderer(new_fd);
again:
    ret = wait_for_socket_read(new_fd);
    if (ret < 0)
      goto err;

    ret = read(new_fd, &header, sizeof(header));

    if (ret == 8) {
      fprintf(stderr, "got length: %d cmd: %d\n", header[0], header[1]);

      switch (header[1]) {
      case VCMD_GET_CAPS:
	vtest_send_caps();
	break;
      case VCMD_RESOURCE_CREATE:
	vtest_create_resource();
	break;
      case VCMD_RESOURCE_UNREF:
	vtest_resource_unref();
	break;
      case VCMD_SUBMIT_CMD:
	vtest_submit_cmd(header[0]);
	break;
      default:
	break;
      }
      goto again;
    }
err:
    close(new_fd);
    close(sock);
}
