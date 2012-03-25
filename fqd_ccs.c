#include "fqd.h"

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <netinet/in.h>

static int
fqd_ccs_auth(remote_client *client) {
  uint16_t cmd, method;
  fq_rk queue_name;

  if(fq_read_uint16(client->fd, &cmd) ||
     ntohs(cmd) != FQ_PROTO_AUTH_CMD) return -1;
  if(fq_read_uint16(client->fd, &method))
    return -2;
  method = ntohs(method);
  if(method == 0) {
    char buf[40];
    unsigned char pass[10240];
    int len, i;
    len = fq_read_short_cmd(client->fd, sizeof(client->user.name),
                            client->user.name);
    if(len < 0 || len > (int)sizeof(client->user.name)) return -3;
    client->user.len = len & 0xff;
    len = fq_read_short_cmd(client->fd, sizeof(queue_name.name),
                            queue_name.name);
    if(len < 0 || len > (int)sizeof(queue_name.name)) return -4;
    queue_name.len = len & 0xff;
    len = fq_read_short_cmd(client->fd, sizeof(pass), pass);
    if(len < 0 || len > (int)sizeof(queue_name.name)) return -5;

    /* do AUTH */

    client->key.len = sizeof(client->key.name);
    for(i=0;i<client->key.len;i++) client->key.name[i] = random() & 0xf;

    client->queue = fqd_queue_get(&queue_name);
    if(fqd_queue_register_client(client->queue, client)) {
      ERRTOFD(client->fd, "can't add you to queue");
      return -6;
    }

    if(fq_write_uint16(client->fd, FQ_PROTO_AUTH_RESP) ||
       fq_write_short_cmd(client->fd,
                          client->key.len, client->key.name) < 0) {
      return -7;
    }

    buf[0] = '\0';
    inet_ntop(AF_INET, &client->remote.sin_addr, buf, sizeof(buf));
    snprintf(client->pretty, sizeof(client->pretty), "%.*s/%.*s@%s:%d",
             client->user.len, client->user.name,
             queue_name.len, queue_name.name,
             buf, ntohs(client->remote.sin_port));
    return 0;
  }
  return -1;
}

static int
fqd_ccs_loop(remote_client *client) {
  uint16_t cmd;
  while(fq_read_uint16(client->fd, &cmd) == 0) {
    cmd = ntohs(cmd);
    switch(cmd) {
      case FQ_PROTO_HBREQ:
      {
        uint16_t ms;
        fq_read_uint16(client->fd, &ms);
#ifdef DEBUG
        fprintf(stderr, "setting client(%p) heartbeat to %d\n",
                (void *)client, ms);
#endif
        client->heartbeat_ms = ntohs(ms);
        break;
      }
      default:
        return -1;
    }
  }
  return -1;
}

extern void
fqd_command_and_control_server(remote_client *client) {
  /* auth */
  int rv;
  if((rv = fqd_ccs_auth(client)) != 0) {
#ifdef DEBUG
    fprintf(stderr, "client auth failed: %d\n", rv);
#endif
    return;
  }
  if(fqd_config_register_client(client)) {
#ifdef DEBUG
    fprintf(stderr, "client registration failed\n");
#endif
    return;
  }
  fqd_ccs_loop(client);
  fqd_config_deregister_client(client);
}