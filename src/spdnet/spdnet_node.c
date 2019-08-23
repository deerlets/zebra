#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include "spdnet-inl.h"

static struct spdnet_node *__spdnet_node_new(struct spdnet_ctx *ctx, int type)
{
	struct spdnet_node *snode = malloc(sizeof(*snode));
	if (!snode) return NULL;
	memset(snode, 0, sizeof(*snode));

	snode->ctx = ctx;
	memset(snode->id, 0, sizeof(snode->id));
	snode->id_len = 0;

	snode->type = type;
	snode->alive_interval = 0;
	snode->alive_timeout = 0;

	snode->is_bind = 0;
	snode->is_connect = 0;
	memset(snode->bind_addr, 0, sizeof(snode->bind_addr));
	memset(snode->connect_addr, 0, sizeof(snode->connect_addr));
	snode->socket = zmq_socket(ctx->zmq_ctx, type);
	if (snode->socket == NULL) {
		free(snode);
		return NULL;
	}
	int linger = 1000;
	zmq_setsockopt(snode->socket, ZMQ_LINGER, &linger, sizeof(linger));

	snode->user_data = NULL;

	/* mainly used by spdnet_nodepool */
	snode->used = 0;
	snode->recvmsg_cb = NULL;
	snode->recvmsg_timeout = 0;
	INIT_LIST_HEAD(&snode->node);
	INIT_LIST_HEAD(&snode->pollin_node);
	INIT_LIST_HEAD(&snode->pollout_node);
	INIT_LIST_HEAD(&snode->pollerr_node);
	INIT_LIST_HEAD(&snode->recvmsg_timeout_node);

	return snode;
}

struct spdnet_node *spdnet_node_new(struct spdnet_ctx *ctx, int type)
{
	struct spdnet_node *snode = spdnet_nodepool_get(ctx->pool, type);
	if (snode) return snode;

	snode = __spdnet_node_new(ctx, type);
	assert(snode);

	spdnet_nodepool_add(ctx->pool, snode);
	return snode;
}

static void __spdnet_node_destroy(struct spdnet_node *snode)
{
	assert(snode != NULL);
	if (snode->is_bind)
		spdnet_unbind(snode);
	if (snode->is_connect)
		spdnet_disconnect(snode);
	assert(zmq_close(snode->socket) == 0);
	free(snode);
}

void spdnet_node_destroy(struct spdnet_node *snode)
{
	if (snode->used)
		spdnet_nodepool_put(snode->ctx->pool, snode);
	else
		__spdnet_node_destroy(snode);
}

void *spdnet_get_socket(struct spdnet_node *snode)
{
	return snode->socket;
}

void spdnet_get_id(struct spdnet_node *snode, void *id, size_t *len)
{
	assert(id);
	assert(len);

	*len = snode->id_len;
	memcpy(id, snode->id, *len);
}

void spdnet_set_id(struct spdnet_node *snode, const void *id, size_t len)
{
	assert(id);
	assert(len <= SPDNET_SOCKID_SIZE);

	memcpy(snode->id, id, len);
	snode->id_len = len;

	assert(zmq_setsockopt(snode->socket, ZMQ_IDENTITY, id, len) == 0);
}

void spdnet_set_alive(struct spdnet_node *snode, int64_t alive)
{
	assert(snode->type == SPDNET_NODE);

	if (alive < SPDNET_MIN_ALIVE_INTERVAL)
		snode->alive_interval = SPDNET_MIN_ALIVE_INTERVAL;
	else
		snode->alive_interval = alive;

	snode->alive_timeout = time(NULL) + snode->alive_interval;
}

void spdnet_set_filter(struct spdnet_node *snode, const void *prefix, size_t len)
{
	assert(snode->type == SPDNET_SUB);
	zmq_setsockopt(snode->socket, ZMQ_SUBSCRIBE, prefix, len);
}

void *spdnet_get_user_data(struct spdnet_node *snode)
{
	return snode->user_data;
}

void spdnet_set_user_data(struct spdnet_node *snode, void *user_data)
{
	snode->user_data = user_data;
}

int spdnet_bind(struct spdnet_node *snode, const char *addr)
{
	assert(snode->is_bind == 0);

	if (zmq_bind(snode->socket, addr))
		return -1;

	if (addr != snode->bind_addr)
		snprintf(snode->bind_addr, sizeof(snode->bind_addr), "%s", addr);
	snode->is_bind = 1;
	return 0;
}

void spdnet_unbind(struct spdnet_node *snode)
{
	assert(snode->is_bind == 1);

	assert(zmq_unbind(snode->socket, snode->bind_addr) == 0);
	snode->is_bind = 0;
}

int spdnet_connect(struct spdnet_node *snode, const char *addr)
{
	assert(snode->is_connect == 0);

	if (zmq_connect(snode->socket, addr))
		return -1;

	if (snode->type == SPDNET_NODE) {
		if (spdnet_register(snode)) {
			zmq_disconnect(snode->socket, addr);
			return -1;
		}

		snode->alive_interval = SPDNET_ALIVE_INTERVAL;
		snode->alive_timeout = time(NULL) + SPDNET_ALIVE_INTERVAL;
	}

	if (addr != snode->connect_addr)
		snprintf(snode->connect_addr,
		         sizeof(snode->connect_addr),
		         "%s", addr);
	snode->is_connect = 1;
	return 0;
}

void spdnet_disconnect(struct spdnet_node *snode)
{
	assert(snode->is_connect == 1);

	if (snode->type == SPDNET_NODE) {
		spdnet_unregister(snode);
		snode->alive_interval = 0;
		snode->alive_timeout = 0;
	}

	assert(zmq_disconnect(snode->socket, snode->connect_addr) == 0);
	snode->is_connect = 0;
}

int spdnet_register(struct spdnet_node *snode)
{
	int rc;

	struct spdnet_msg msg;
	spdnet_msg_init_data(&msg, SPDNET_SOCKID_NONE, SPDNET_SOCKID_NONE_LEN,
	                     SPDNET_REGISTER_MSG, SPDNET_REGISTER_MSG_LEN,
	                     NULL, 0);
	rc = spdnet_sendmsg(snode, &msg);
	spdnet_msg_close(&msg);

	if (rc == -1) return -1;
	return 0;
}

int spdnet_unregister(struct spdnet_node *snode)
{
	int rc;

	struct spdnet_msg msg;
	spdnet_msg_init_data(&msg, SPDNET_SOCKID_NONE, SPDNET_SOCKID_NONE_LEN,
	                     SPDNET_UNREGISTER_MSG, SPDNET_UNREGISTER_MSG_LEN,
	                     NULL, 0);
	rc = spdnet_sendmsg(snode, &msg);
	spdnet_msg_close(&msg);

	if (rc == -1) return -1;
	return 0;
}

int spdnet_expose(struct spdnet_node *snode)
{
	assert(snode->id_len);
	int rc;

	struct spdnet_msg msg;
	spdnet_msg_init_data(&msg, SPDNET_SOCKID_NONE, SPDNET_SOCKID_NONE_LEN,
	                     SPDNET_EXPOSE_MSG, SPDNET_EXPOSE_MSG_LEN,
	                     NULL, 0);
	rc = spdnet_sendmsg(snode, &msg);
	spdnet_msg_close(&msg);

	if (rc == -1) return -1;
	return 0;
}

int spdnet_alive(struct spdnet_node *snode)
{
	assert(snode->type == SPDNET_NODE);
	int rc;

	struct spdnet_msg msg;
	spdnet_msg_init_data(&msg, SPDNET_SOCKID_NONE, SPDNET_SOCKID_NONE_LEN,
	                     SPDNET_ALIVE_MSG, SPDNET_ALIVE_MSG_LEN, NULL, 0);
	rc = spdnet_sendmsg(snode, &msg);
	spdnet_msg_close(&msg);

	if (rc == -1) return -1;
	return 0;
}

int spdnet_recv(struct spdnet_node *snode, void *buf, size_t size, int flags)
{
	return zmq_recv(snode->socket, buf, size, flags);
}

int spdnet_send(struct spdnet_node *snode, const void *buf, size_t size, int flags)
{
	return zmq_send(snode->socket, buf, size, flags);
}

int spdnet_recvmsg(struct spdnet_node *snode, struct spdnet_msg *msg, int flags)
{
	int rc = 0;

	// sockid
	if (snode->type == SPDNET_NODE) {
		rc = z_recv_more(snode->socket, MSG_SOCKID(msg), flags);
		if (rc == -1) return -1;
	}
	rc = z_recv_more(snode->socket, MSG_SOCKID(msg), flags);
	if (rc == -1) return -1;

	// header
	rc = z_recv_more(snode->socket, MSG_HEADER(msg), flags);
	if (rc == -1) return -1;
	rc = z_recv_more(snode->socket, MSG_HEADER(msg), flags);
	if (rc == -1) return -1;

	// content
	rc = z_recv_more(snode->socket, MSG_CONTENT(msg), flags);
	if (rc == -1) return -1;
	rc = z_recv_more(snode->socket, MSG_CONTENT(msg), flags);
	if (rc == -1) return -1;

	// meta
	zmq_msg_t meta_msg;
	zmq_msg_init(&meta_msg);
	rc = z_recv_more(snode->socket, &meta_msg, flags);
	if (rc == -1) {
		zmq_msg_close(&meta_msg);
		return -1;
	}
	rc = z_recv_not_more(snode->socket, &meta_msg, flags);
	if (rc == -1) {
		z_clear(snode->socket);
		zmq_msg_close(&meta_msg);
		return -1;
	}

	if (msg->__meta) free(msg->__meta);
	msg->__meta = malloc(zmq_msg_size(&meta_msg));
	memcpy(msg->__meta, zmq_msg_data(&meta_msg), zmq_msg_size(&meta_msg));
	assert(zmq_msg_size(&meta_msg) == sizeof(*(msg->__meta)));
	zmq_msg_close(&meta_msg);

	return 0;
}

int spdnet_recvmsg_timeout(struct spdnet_node *snode, struct spdnet_msg *msg,
                           int flags, int timeout)
{
	zmq_pollitem_t item;
	item.socket = spdnet_get_socket(snode);
	item.fd = 0;
	item.events = ZMQ_POLLIN;
	item.revents = 0;
	if (zmq_poll(&item, 1, timeout) != 1)
		return -1;

	return spdnet_recvmsg(snode, msg, flags);
}

void spdnet_recvmsg_async(struct spdnet_node *snode, spdnet_recvmsg_cb cb,
                          void *arg, long timeout)
{
	assert(cb);

	snode->recvmsg_cb = cb;
	snode->recvmsg_arg = arg;
	if (timeout) snode->recvmsg_timeout = time(NULL) + timeout/1000;
	else snode->recvmsg_timeout = 0;
}

int spdnet_sendmsg(struct spdnet_node *snode, struct spdnet_msg *msg)
{
#ifdef HAVE_ZMQ_BUG
	usleep(10000);
#endif

	int rc = 0;

	// sockid
	if (snode->type == SPDNET_NODE) {
		rc = zmq_send(snode->socket, &snode->type, 1,
		              ZMQ_SNDMORE | ZMQ_DONTWAIT);
		if (rc == -1) return -1;
	}
	rc = zmq_msg_send(MSG_SOCKID(msg), snode->socket, ZMQ_SNDMORE);
	if (rc == -1) return -1;

	// header
	rc = zmq_send(snode->socket, "", 0, ZMQ_SNDMORE);
	if (rc == -1) return -1;
	rc = zmq_msg_send(MSG_HEADER(msg), snode->socket, ZMQ_SNDMORE);
	if (rc == -1) return -1;

	// content
	rc = zmq_send(snode->socket, "", 0, ZMQ_SNDMORE);
	if (rc == -1) return -1;
	rc = zmq_msg_send(MSG_CONTENT(msg), snode->socket, ZMQ_SNDMORE);
	if (rc == -1) return -1;

	// meta
	rc = zmq_send(snode->socket, "", 0, ZMQ_SNDMORE);
	if (rc == -1) return -1;

	spdnet_meta_t meta;
	meta.node_type = snode->type;
	meta.ttl = 10;

	zmq_msg_t meta_msg;
	zmq_msg_init_size(&meta_msg, sizeof(meta));
	memcpy(zmq_msg_data(&meta_msg), &meta, sizeof(meta));
	rc = zmq_msg_send(&meta_msg, snode->socket, 0);
	zmq_msg_close(&meta_msg);
	if (rc == -1) return -1;

	return 0;
}
