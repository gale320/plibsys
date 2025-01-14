/*
 * The MIT License
 *
 * Copyright (C) 2013-2019 Alexander Saprykin <saprykin.spb@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * 'Software'), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED 'AS IS', WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include "plibsys.h"
#include "ptestmacros.h"

#include <string.h>

P_TEST_MODULE_INIT ();

static pchar             socket_data[]       = "This is a socket test data!";
volatile static pboolean is_sender_working   = FALSE;
volatile static pboolean is_receiver_working = FALSE;

typedef struct _SocketTestData {
	puint16		sender_port;
	puint16		receiver_port;
	pboolean	shutdown_channel;
} SocketTestData;

extern "C" ppointer pmem_alloc (psize nbytes)
{
	P_UNUSED (nbytes);
	return (ppointer) NULL;
}

extern "C" ppointer pmem_realloc (ppointer block, psize nbytes)
{
	P_UNUSED (block);
	P_UNUSED (nbytes);
	return (ppointer) NULL;
}

extern "C" void pmem_free (ppointer block)
{
	P_UNUSED (block);
}

static void clean_error (PError **error)
{
	if (error == NULL || *error == NULL)
		return;

	zerror_free (*error);
	*error = NULL;
}

static pboolean test_socket_address_directly (const PSocketAddress *addr, puint16 port)
{
	if (addr == NULL)
		return FALSE;

	pchar *addr_str = zsocket_address_get_address (addr);
	PSocketFamily remote_family = zsocket_address_get_family (addr);
	puint16 remote_port = zsocket_address_get_port (addr);
	psize remote_size = zsocket_address_get_native_size (addr);

	pboolean ret = (strcmp (addr_str, "127.0.0.1") == 0 && remote_family == P_SOCKET_FAMILY_INET &&
			remote_port == port && remote_size > 0) ? TRUE : FALSE;

	zfree (addr_str);

	return ret;
}

static pboolean test_socket_address (PSocket *socket, puint16 port)
{
	/* Test remote address */
	PSocketAddress *remote_addr = zsocket_get_remote_address (socket, NULL);

	if (remote_addr == NULL)
		return FALSE;

	pboolean ret = test_socket_address_directly (remote_addr, port);

	zsocket_address_free (remote_addr);

	return ret;
}

static pboolean compare_socket_addresses (const PSocketAddress *addr1, const PSocketAddress *addr2)
{
	if (addr1 == NULL || addr2 == NULL)
		return FALSE;

	pchar *addr_str1 = zsocket_address_get_address (addr1);
	pchar *addr_str2 = zsocket_address_get_address (addr2);

	if (addr_str1 == NULL || addr_str2 == NULL) {
		zfree (addr_str1);
		zfree (addr_str2);

		return FALSE;
	}

	pboolean addr_cmp = (strcmp (addr_str1, addr_str2) == 0 ? TRUE : FALSE);

	zfree (addr_str1);
	zfree (addr_str2);

	if (addr_cmp == FALSE)
		return FALSE;

	if (zsocket_address_get_family (addr1) != zsocket_address_get_family (addr2))
		return FALSE;

	if (zsocket_address_get_native_size (addr1) != zsocket_address_get_native_size (addr2))
		return FALSE;

	return TRUE;
}

static void * udzsocket_sender_thread (void *arg)
{
	pint send_counter = 0;

	if (arg == NULL)
		zuthread_exit (-1);

	SocketTestData *data = (SocketTestData *) (arg);

	/* Create sender socket */
	PSocket *skt_sender = zsocket_new (P_SOCKET_FAMILY_INET,
					    P_SOCKET_TYPE_DATAGRAM,
					    P_SOCKET_PROTOCOL_UDP,
					    NULL);

	if (skt_sender == NULL)
		zuthread_exit (-1);

	PSocketAddress *addr_sender = zsocket_address_new ("127.0.0.1", data->sender_port);

	if (addr_sender == NULL) {
		zsocket_free (skt_sender);
		zuthread_exit (-1);
	}

	if (zsocket_bind (skt_sender, addr_sender, FALSE, NULL) == FALSE) {
		zsocket_free (skt_sender);
		zsocket_address_free (addr_sender);
		zuthread_exit (-1);
	} else {
		zsocket_address_free (addr_sender);

		PSocketAddress *local_addr = zsocket_get_local_address (skt_sender, NULL);

		if (local_addr == NULL) {
			zsocket_free (skt_sender);
			zuthread_exit (-1);
		}

		data->sender_port = zsocket_address_get_port (local_addr);

		zsocket_address_free (local_addr);
	}

	zsocket_set_timeout (skt_sender, 50);

	/* Test that remote address is NULL */
	PSocketAddress *remote_addr = zsocket_get_remote_address (skt_sender, NULL);

	if (remote_addr != NULL) {
		if (zsocket_address_is_any (remote_addr) == FALSE) {
			zsocket_address_free (remote_addr);
			zsocket_free (skt_sender);
			zuthread_exit (-1);
		} else {
			zsocket_address_free (remote_addr);
			remote_addr = NULL;
		}
	}

	/* Test that we are not connected */
	if (zsocket_is_connected (skt_sender) == TRUE) {
		zsocket_free (skt_sender);
		zuthread_exit (-1);
	}

	while (is_sender_working == TRUE && data->receiver_port == 0) {
		zuthread_sleep (1);
		continue;
	}

	PSocketAddress *addr_receiver = NULL;

	if (data->receiver_port != 0)
		addr_receiver = zsocket_address_new ("127.0.0.1", data->receiver_port);

	while (is_sender_working == TRUE) {
		if (data->receiver_port == 0)
			break;

		if (zsocket_send_to (skt_sender,
				      addr_receiver,
				      socket_data,
				      sizeof (socket_data),
				      NULL) == sizeof (socket_data))
			++send_counter;

		zuthread_sleep (1);
	}

	zsocket_address_free (addr_receiver);
	zsocket_free (skt_sender);
	zuthread_exit (send_counter);

	return NULL;
}

static void * udzsocket_receiver_thread (void *arg)
{
	pchar	recv_buffer[sizeof (socket_data) * 3];
	pint	recv_counter = 0;

	if (arg == NULL)
		zuthread_exit (-1);

	SocketTestData *data = (SocketTestData *) (arg);

	/* Create receiving socket */
	PSocket *skt_receiver = zsocket_new (P_SOCKET_FAMILY_INET,
					      P_SOCKET_TYPE_DATAGRAM,
					      P_SOCKET_PROTOCOL_UDP,
					      NULL);

	if (skt_receiver == NULL)
		zuthread_exit (-1);

	zsocket_set_blocking (skt_receiver, FALSE);

	PSocketAddress *addr_receiver = zsocket_address_new ("127.0.0.1", data->receiver_port);

	if (addr_receiver == NULL) {
		zsocket_free (skt_receiver);
		zuthread_exit (-1);
	}

	if (zsocket_bind (skt_receiver, addr_receiver, TRUE, NULL) == FALSE) {
		zsocket_free (skt_receiver);
		zsocket_address_free (addr_receiver);
		zuthread_exit (-1);
	} else {
		zsocket_address_free (addr_receiver);

		PSocketAddress *local_addr = zsocket_get_local_address (skt_receiver, NULL);

		if (local_addr == NULL) {
			zsocket_free (skt_receiver);
			zuthread_exit (-1);
		}

		data->receiver_port = zsocket_address_get_port (local_addr);

		zsocket_address_free (local_addr);
	}

	zsocket_set_timeout (skt_receiver, 50);

	/* Test that remote address is NULL */
	PSocketAddress *remote_addr = zsocket_get_remote_address (skt_receiver, NULL);

	if (remote_addr != NULL) {
		if (zsocket_address_is_any (remote_addr) == FALSE) {
			zsocket_address_free (remote_addr);
			zsocket_free (skt_receiver);
			zuthread_exit (-1);
		} else {
			zsocket_address_free (remote_addr);
			remote_addr = NULL;
		}
	}

	/* Test that we are not connected */
	if (zsocket_is_connected (skt_receiver) == TRUE) {
		zsocket_free (skt_receiver);
		zuthread_exit (-1);
	}

	while (is_receiver_working == TRUE) {
		PSocketAddress *remote_addr = NULL;

		pssize received = zsocket_receive_from (skt_receiver,
							 &remote_addr,
							 recv_buffer,
							 sizeof (recv_buffer),
							 NULL);

		if (remote_addr != NULL && test_socket_address_directly (remote_addr, data->sender_port) == FALSE) {
			zsocket_address_free (remote_addr);
			break;
		}

		zsocket_address_free (remote_addr);

		if (received == sizeof (socket_data))
			++recv_counter;
		else if (received > 0) {
			zsocket_free (skt_receiver);
			zuthread_exit (-1);
		}

		zuthread_sleep (1);
	}

	zsocket_free (skt_receiver);
	zuthread_exit (recv_counter);

	return NULL;
}

static void * tczsocket_sender_thread (void *arg)
{
	pint		send_counter = 0;
	psize		send_total;
	pssize		send_now;
	pboolean	is_connected = FALSE;

	if (arg == NULL)
		zuthread_exit (-1);

	SocketTestData *data = (SocketTestData *) (arg);

	/* Create sender socket */
	PSocket *skt_sender = zsocket_new (P_SOCKET_FAMILY_INET,
					    P_SOCKET_TYPE_STREAM,
					    P_SOCKET_PROTOCOL_DEFAULT,
					    NULL);

	if (skt_sender == NULL)
		zuthread_exit (-1);

	zsocket_set_timeout (skt_sender, 2000);

	if (zsocket_get_fd (skt_sender) < 0) {
		zsocket_free (skt_sender);
		zuthread_exit (-1);
	}

	while (is_sender_working == TRUE && data->receiver_port == 0) {
		zuthread_sleep (1);
		continue;
	}

	PSocketAddress *addr_sender = zsocket_address_new ("127.0.0.1", data->sender_port);

	if (addr_sender == NULL) {
		zsocket_free (skt_sender);
		zuthread_exit (-1);
	}

	if (zsocket_bind (skt_sender, addr_sender, FALSE, NULL) == FALSE) {
		zsocket_free (skt_sender);
		zsocket_address_free (addr_sender);
		zuthread_exit (-1);
	} else {
		zsocket_address_free (addr_sender);

		PSocketAddress *local_addr = zsocket_get_local_address (skt_sender, NULL);

		if (local_addr == NULL) {
			zsocket_free (skt_sender);
			zuthread_exit (-1);
		}

		data->sender_port = zsocket_address_get_port (local_addr);

		zsocket_address_free (local_addr);
	}

	send_total = 0;
	send_now = 0;

	while (is_sender_working == TRUE && data->receiver_port == 0) {
		zuthread_sleep (1);
		continue;
	}

	PSocketAddress *addr_receiver = NULL;

	/* Try to connect in non-blocking mode */
	zsocket_set_blocking (skt_sender, FALSE);

	if (data->receiver_port != 0) {
		addr_receiver = zsocket_address_new ("127.0.0.1", data->receiver_port);
		is_connected = zsocket_connect (skt_sender, addr_receiver, NULL);

		if (is_connected == FALSE) {
			if (zsocket_io_condition_wait (skt_sender, P_SOCKET_IO_CONDITION_POLLOUT, NULL) == TRUE &&
			    zsocket_check_connect_result (skt_sender, NULL) == FALSE) {
				zsocket_address_free (addr_receiver);
				zsocket_free (skt_sender);
				zuthread_exit (-1);
			}
		}

		is_connected = zsocket_is_connected (skt_sender);

		if (is_connected == TRUE && zsocket_shutdown (skt_sender,
							       FALSE,
							       data->shutdown_channel,
							       NULL) == FALSE)
			is_connected = FALSE;
	}

	if (data->shutdown_channel == TRUE && zsocket_is_closed (skt_sender) == TRUE) {
		zsocket_address_free (addr_receiver);
		zsocket_free (skt_sender);
		zuthread_exit (-1);
	}

	zsocket_set_blocking (skt_sender, TRUE);

	while (is_sender_working == TRUE) {
		if (data->receiver_port == 0 || is_connected == FALSE)
			break;

		if (test_socket_address (skt_sender, data->receiver_port) == FALSE)
			break;

		if (data->shutdown_channel == FALSE && zsocket_is_connected (skt_sender) == FALSE) {
			zsocket_address_free (addr_receiver);
			zsocket_free (skt_sender);
			zuthread_exit (-1);
		}

		send_now = zsocket_send (skt_sender,
					  socket_data + send_total,
					  sizeof (socket_data) - send_total,
					  NULL);

		if (send_now > 0)
			send_total += (psize) send_now;

		if (send_total == sizeof (socket_data)) {
			send_total = 0;
			++send_counter;
		}

		zuthread_sleep (1);
	}

	if (zsocket_close (skt_sender, NULL) == FALSE)
		send_counter = -1;

	zsocket_address_free (addr_receiver);
	zsocket_free (skt_sender);
	zuthread_exit (send_counter);

	return NULL;
}

static void * tczsocket_receiver_thread (void *arg)
{
	pchar		recv_buffer[sizeof (socket_data)];
	pint		recv_counter = 0;
	psize		recv_total;
	pssize		recv_now;

	if (arg == NULL)
		zuthread_exit (-1);

	SocketTestData *data = (SocketTestData *) (arg);

	/* Create receiving socket */
	PSocket *skt_receiver = zsocket_new (P_SOCKET_FAMILY_INET,
					      P_SOCKET_TYPE_STREAM,
					      P_SOCKET_PROTOCOL_TCP,
					      NULL);

	if (skt_receiver == NULL)
		zuthread_exit (-1);

	PSocketAddress *addr_receiver = zsocket_address_new ("127.0.0.1", data->receiver_port);

	if (addr_receiver == NULL) {
		zsocket_free (skt_receiver);
		zuthread_exit (-1);
	}

	zsocket_set_timeout (skt_receiver, 2000);

	if (zsocket_bind (skt_receiver, addr_receiver, TRUE, NULL) == FALSE ||
	    zsocket_listen (skt_receiver, NULL) == FALSE) {
		zsocket_free (skt_receiver);
		zsocket_address_free (addr_receiver);
		zuthread_exit (-1);
	} else {
		zsocket_address_free (addr_receiver);

		PSocketAddress *local_addr = zsocket_get_local_address (skt_receiver, NULL);

		if (local_addr == NULL) {
			zsocket_free (skt_receiver);
			zuthread_exit (-1);
		}

		data->receiver_port = zsocket_address_get_port (local_addr);

		zsocket_address_free (local_addr);
	}

	PSocket *conn_socket = NULL;
	recv_total = 0;
	recv_now = 0;

	while (is_receiver_working == TRUE) {
		if (conn_socket == NULL) {
			conn_socket = zsocket_accept (skt_receiver, NULL);

			if (conn_socket == NULL) {
				zuthread_sleep (1);
				continue;
			} else {
				/* On Syllable there is a bug in TCP which changes a local port
				 * of the client socket which connects to a server */
#ifndef P_OS_SYLLABLE
				if (test_socket_address (conn_socket, data->sender_port) == FALSE)
					break;
#endif

				if (zsocket_shutdown (conn_socket, data->shutdown_channel, FALSE, NULL) == FALSE)
					break;

				zsocket_set_timeout (conn_socket, 2000);
			}
		}

		if ((data->shutdown_channel == FALSE && zsocket_is_connected (conn_socket) == FALSE) ||
		    (data->shutdown_channel == TRUE && zsocket_is_closed (conn_socket) == TRUE)) {
			zsocket_free (conn_socket);
			zsocket_free (skt_receiver);
			zuthread_exit (-1);
		}

		recv_now = zsocket_receive (conn_socket,
					     recv_buffer + recv_total,
					     sizeof (recv_buffer) - recv_total,
					     NULL);

		if (recv_now > 0)
			recv_total += (psize) recv_now;

		if (recv_total == sizeof (recv_buffer)) {
			recv_total = 0;

			if (strncmp (recv_buffer, socket_data, sizeof (recv_buffer)) == 0)
				++recv_counter;

			memset (recv_buffer, 0, sizeof (recv_buffer));
		}

		zuthread_sleep (1);
	}

	if (zsocket_close (skt_receiver, NULL) == FALSE)
		recv_counter = -1;

	zsocket_free (conn_socket);
	zsocket_free (skt_receiver);

	zuthread_exit (recv_counter);

	return NULL;
}

P_TEST_CASE_BEGIN (psocket_nomem_test)
{
	zlibsys_init ();

	PSocket *socket = zsocket_new (P_SOCKET_FAMILY_INET,
					P_SOCKET_TYPE_DATAGRAM,
					P_SOCKET_PROTOCOL_UDP,
					NULL);
	P_TEST_CHECK (socket != NULL);

	PSocketAddress *sock_addr = zsocket_address_new ("127.0.0.1", 32211);

	P_TEST_CHECK (sock_addr != NULL);
	P_TEST_CHECK (zsocket_bind (socket, sock_addr, TRUE, NULL) == TRUE);

	zsocket_address_free (sock_addr);

	zsocket_set_timeout (socket, 1000);
	sock_addr = zsocket_address_new ("127.0.0.1", 32215);
	P_TEST_CHECK (sock_addr != NULL);
	P_TEST_CHECK (zsocket_connect (socket, sock_addr, NULL) == TRUE);

	zsocket_address_free (sock_addr);

	PMemVTable vtable;

	vtable.free    = pmem_free;
	vtable.malloc  = pmem_alloc;
	vtable.realloc = pmem_realloc;

	P_TEST_CHECK (zmem_set_vtable (&vtable) == TRUE);

	P_TEST_CHECK (zsocket_new (P_SOCKET_FAMILY_INET,
				   P_SOCKET_TYPE_DATAGRAM,
				   P_SOCKET_PROTOCOL_UDP,
				   NULL) == NULL);
	P_TEST_CHECK (zsocket_new_from_fd (zsocket_get_fd (socket), NULL) == NULL);
	P_TEST_CHECK (zsocket_get_local_address (socket, NULL) == NULL);
	P_TEST_CHECK (zsocket_get_remote_address (socket, NULL) == NULL);

	zmem_restore_vtable ();

	zsocket_close (socket, NULL);
	zsocket_free (socket);

	zlibsys_shutdown ();
}
P_TEST_CASE_END ()

P_TEST_CASE_BEGIN (psocket_bad_input_test)
{
	zlibsys_init ();

	PError *error = NULL;

	P_TEST_CHECK (zsocket_new_from_fd (-1, &error) == NULL);
	P_TEST_CHECK (error != NULL);
	clean_error (&error);

	P_TEST_CHECK (zsocket_new (P_SOCKET_FAMILY_INET,
				   (PSocketType) -1,
				   P_SOCKET_PROTOCOL_TCP,
				   NULL) == NULL);
	/* Syllable doesn't validate socket family */
#ifndef P_OS_SYLLABLE
	P_TEST_CHECK (zsocket_new ((PSocketFamily) -1,
				   P_SOCKET_TYPE_SEQPACKET,
				   P_SOCKET_PROTOCOL_TCP,
				   NULL) == NULL);
#endif
	P_TEST_CHECK (zsocket_new (P_SOCKET_FAMILY_UNKNOWN,
				   P_SOCKET_TYPE_UNKNOWN,
				   P_SOCKET_PROTOCOL_UNKNOWN,
				   &error) == NULL);
	P_TEST_CHECK (zsocket_new_from_fd (1, NULL) == NULL);
	P_TEST_CHECK (error != NULL);
	clean_error (&error);

	P_TEST_CHECK (zsocket_get_fd (NULL) == -1);
	P_TEST_CHECK (zsocket_get_family (NULL) == P_SOCKET_FAMILY_UNKNOWN);
	P_TEST_CHECK (zsocket_get_type (NULL) == P_SOCKET_TYPE_UNKNOWN);
	P_TEST_CHECK (zsocket_get_protocol (NULL) == P_SOCKET_PROTOCOL_UNKNOWN);
	P_TEST_CHECK (zsocket_get_keepalive (NULL) == FALSE);
	P_TEST_CHECK (zsocket_get_blocking (NULL) == FALSE);
	P_TEST_CHECK (zsocket_get_timeout (NULL) == -1);
	P_TEST_CHECK (zsocket_get_listen_backlog (NULL) == -1);
	P_TEST_CHECK (zsocket_io_condition_wait (NULL, P_SOCKET_IO_CONDITION_POLLIN, NULL) == FALSE);
	P_TEST_CHECK (zsocket_io_condition_wait (NULL, P_SOCKET_IO_CONDITION_POLLOUT, NULL) == FALSE);

	P_TEST_CHECK (zsocket_get_local_address (NULL, &error) == NULL);
	P_TEST_CHECK (error != NULL);
	clean_error (&error);

	P_TEST_CHECK (zsocket_get_remote_address (NULL, &error) == NULL);
	P_TEST_CHECK (error != NULL);
	clean_error (&error);

	P_TEST_CHECK (zsocket_is_connected (NULL) == FALSE);
	P_TEST_CHECK (zsocket_is_closed (NULL) == TRUE);

	P_TEST_CHECK (zsocket_check_connect_result (NULL, &error) == FALSE);
	P_TEST_CHECK (error != NULL);
	clean_error (&error);

	zsocket_set_keepalive (NULL, FALSE);
	zsocket_set_blocking (NULL, FALSE);
	zsocket_set_timeout (NULL, 0);
	zsocket_set_listen_backlog (NULL, 0);

	P_TEST_CHECK (zsocket_bind (NULL, NULL, FALSE, &error) == FALSE);
	P_TEST_CHECK (error != NULL);
	clean_error (&error);

	P_TEST_CHECK (zsocket_connect (NULL, NULL, &error) == FALSE);
	P_TEST_CHECK (error != NULL);
	clean_error (&error);

	P_TEST_CHECK (zsocket_listen (NULL, &error) == FALSE);
	P_TEST_CHECK (error != NULL);
	clean_error (&error);

	P_TEST_CHECK (zsocket_accept (NULL, &error) == NULL);
	P_TEST_CHECK (error != NULL);
	clean_error (&error);

	P_TEST_CHECK (zsocket_receive (NULL, NULL, 0, &error) == -1);
	P_TEST_CHECK (error != NULL);
	clean_error (&error);

	P_TEST_CHECK (zsocket_receive_from (NULL, NULL, NULL, 0, &error) == -1);
	P_TEST_CHECK (error != NULL);
	clean_error (&error);

	P_TEST_CHECK (zsocket_send (NULL, NULL, 0, &error) == -1);
	P_TEST_CHECK (error != NULL);
	clean_error (&error);

	P_TEST_CHECK (zsocket_send_to (NULL, NULL, NULL, 0, &error) == -1);
	P_TEST_CHECK (error != NULL);
	clean_error (&error);

	P_TEST_CHECK (zsocket_close (NULL, &error) == FALSE);
	P_TEST_CHECK (error != NULL);
	clean_error (&error);

	P_TEST_CHECK (zsocket_shutdown (NULL, FALSE, FALSE, &error) == FALSE);
	P_TEST_CHECK (error != NULL);
	clean_error (&error);

	P_TEST_CHECK (zsocket_set_buffer_size (NULL, P_SOCKET_DIRECTION_RCV, 0, &error) == FALSE);
	P_TEST_CHECK (error != NULL);
	clean_error (&error);

	P_TEST_CHECK (zsocket_set_buffer_size (NULL, P_SOCKET_DIRECTION_SND, 0, &error) == FALSE);
	P_TEST_CHECK (error != NULL);
	clean_error (&error);

	zsocket_free (NULL);

	zlibsys_shutdown ();
}
P_TEST_CASE_END ()

P_TEST_CASE_BEGIN (psocket_general_udztest)
{
	zlibsys_init ();

	/* Test UDP socket */
	PSocket *socket = zsocket_new (P_SOCKET_FAMILY_INET,
					P_SOCKET_TYPE_DATAGRAM,
					P_SOCKET_PROTOCOL_UDP,
					NULL);

	P_TEST_CHECK (socket != NULL);
	P_TEST_CHECK (zsocket_get_family (socket) == P_SOCKET_FAMILY_INET);
	P_TEST_CHECK (zsocket_get_fd (socket) >= 0);
	P_TEST_CHECK (zsocket_get_listen_backlog (socket) == 5);
	P_TEST_CHECK (zsocket_get_timeout (socket) == 0);

	/* On some operating systems (i.e. OpenVMS) remote address is not NULL */
	PSocketAddress *remote_addr = zsocket_get_remote_address (socket, NULL);

	if (remote_addr != NULL) {
		P_TEST_CHECK (zsocket_address_is_any (remote_addr) == TRUE);
		zsocket_address_free (remote_addr);
		remote_addr = NULL;
	}

	P_TEST_CHECK (zsocket_get_protocol (socket) == P_SOCKET_PROTOCOL_UDP);
	P_TEST_CHECK (zsocket_get_blocking (socket) == TRUE);
	P_TEST_CHECK (zsocket_get_type (socket) == P_SOCKET_TYPE_DATAGRAM);
	P_TEST_CHECK (zsocket_get_keepalive (socket) == FALSE);
	P_TEST_CHECK (zsocket_is_closed (socket) == FALSE);

	zsocket_set_listen_backlog (socket, 12);
	zsocket_set_timeout (socket, -10);
	P_TEST_CHECK (zsocket_get_timeout (socket) == 0);
	zsocket_set_timeout (socket, 10);

	P_TEST_CHECK (zsocket_get_listen_backlog (socket) == 12);
	P_TEST_CHECK (zsocket_get_timeout (socket) == 10);

	PSocketAddress *sock_addr = zsocket_address_new ("127.0.0.1", 32111);
	P_TEST_CHECK (sock_addr != NULL);

	P_TEST_CHECK (zsocket_bind (socket, sock_addr, TRUE, NULL) == TRUE);

	/* Test creating socket from descriptor */
	PSocket *fd_socket = zsocket_new_from_fd (zsocket_get_fd (socket), NULL);
	P_TEST_CHECK (fd_socket != NULL);
	P_TEST_CHECK (zsocket_get_family (fd_socket) == P_SOCKET_FAMILY_INET);
	P_TEST_CHECK (zsocket_get_fd (fd_socket) >= 0);
	P_TEST_CHECK (zsocket_get_listen_backlog (fd_socket) == 5);
	P_TEST_CHECK (zsocket_get_timeout (fd_socket) == 0);

	remote_addr = zsocket_get_remote_address (fd_socket, NULL);

	if (remote_addr != NULL) {
		P_TEST_CHECK (zsocket_address_is_any (remote_addr) == TRUE);
		zsocket_address_free (remote_addr);
		remote_addr = NULL;
	}

	P_TEST_CHECK (zsocket_get_protocol (fd_socket) == P_SOCKET_PROTOCOL_UDP);
	P_TEST_CHECK (zsocket_get_blocking (fd_socket) == TRUE);
	P_TEST_CHECK (zsocket_get_type (fd_socket) == P_SOCKET_TYPE_DATAGRAM);
	P_TEST_CHECK (zsocket_get_keepalive (fd_socket) == FALSE);
	P_TEST_CHECK (zsocket_is_closed (fd_socket) == FALSE);

	zsocket_set_keepalive (fd_socket, FALSE);
	P_TEST_CHECK (zsocket_get_keepalive (fd_socket) == FALSE);

	zsocket_set_keepalive (fd_socket, TRUE);
	zsocket_set_keepalive (fd_socket, FALSE);
	P_TEST_CHECK (zsocket_get_keepalive (fd_socket) == FALSE);

	/* Test UDP local address */
	PSocketAddress *addr = zsocket_get_local_address (socket, NULL);
	P_TEST_CHECK (addr != NULL);

	P_TEST_CHECK (compare_socket_addresses (sock_addr, addr) == TRUE);

	zsocket_address_free (sock_addr);
	zsocket_address_free (addr);

	/* Test UDP connecting to remote address */
	zsocket_set_timeout (socket, 1000);
	addr = zsocket_address_new ("127.0.0.1", 32115);
	P_TEST_CHECK (addr != NULL);
	P_TEST_CHECK (zsocket_connect (socket, addr, NULL) == TRUE);

	P_TEST_CHECK (zsocket_io_condition_wait (socket, P_SOCKET_IO_CONDITION_POLLIN, NULL) == FALSE);
	P_TEST_CHECK (zsocket_io_condition_wait (socket, P_SOCKET_IO_CONDITION_POLLOUT, NULL) == TRUE);

	sock_addr = zsocket_get_remote_address (socket, NULL);

	/* Syllable doesn't support getpeername() for UDP sockets */
#ifdef P_OS_SYLLABLE
	P_TEST_CHECK (sock_addr == NULL);
	sock_addr = zsocket_address_new ("127.0.0.1", 32115);
	P_TEST_CHECK (addr != NULL);
#else
	P_TEST_CHECK (sock_addr != NULL);
	P_TEST_CHECK (compare_socket_addresses (sock_addr, addr) == TRUE);
#endif

	/* Not supported on Syllable */
#ifndef P_OS_SYLLABLE
	P_TEST_CHECK (zsocket_set_buffer_size (socket, P_SOCKET_DIRECTION_RCV, 72 * 1024, NULL) == TRUE);
	P_TEST_CHECK (zsocket_set_buffer_size (socket, P_SOCKET_DIRECTION_SND, 72 * 1024, NULL) == TRUE);
	P_TEST_CHECK (zsocket_check_connect_result (socket, NULL) == TRUE);
#endif

	P_TEST_CHECK (zsocket_is_connected (socket) == TRUE);
	P_TEST_CHECK (zsocket_close (socket, NULL) == TRUE);

	pchar sock_buf[10];

	P_TEST_CHECK (zsocket_bind (socket, sock_addr, TRUE, NULL) == FALSE);
	P_TEST_CHECK (zsocket_connect (socket, addr, NULL) == FALSE);
	P_TEST_CHECK (zsocket_listen (socket, NULL) == FALSE);
	P_TEST_CHECK (zsocket_accept (socket, NULL) == FALSE);
	P_TEST_CHECK (zsocket_receive (socket, sock_buf, sizeof (sock_buf), NULL) == -1);
	P_TEST_CHECK (zsocket_receive_from (socket, NULL, sock_buf, sizeof (sock_buf), NULL) == -1);
	P_TEST_CHECK (zsocket_send (socket, sock_buf, sizeof (sock_buf), NULL) == -1);
	P_TEST_CHECK (zsocket_send_to (socket, addr, sock_buf, sizeof (sock_buf), NULL) == -1);
	P_TEST_CHECK (zsocket_shutdown (socket, TRUE, TRUE, NULL) == FALSE);
	P_TEST_CHECK (zsocket_get_local_address (socket, NULL) == NULL);
	P_TEST_CHECK (zsocket_check_connect_result (socket, NULL) == FALSE);
	P_TEST_CHECK (zsocket_get_fd (socket) == -1);
	P_TEST_CHECK (zsocket_is_connected (socket) == FALSE);
	P_TEST_CHECK (zsocket_is_closed (socket) == TRUE);

	zsocket_set_keepalive (socket, TRUE);
	P_TEST_CHECK (zsocket_get_keepalive (socket) == FALSE);

	P_TEST_CHECK (zsocket_io_condition_wait (socket, P_SOCKET_IO_CONDITION_POLLIN, NULL) == FALSE);
	P_TEST_CHECK (zsocket_io_condition_wait (socket, P_SOCKET_IO_CONDITION_POLLOUT, NULL) == FALSE);

	P_TEST_CHECK (zsocket_set_buffer_size (socket, P_SOCKET_DIRECTION_RCV, 72 * 1024, NULL) == FALSE);
	P_TEST_CHECK (zsocket_set_buffer_size (socket, P_SOCKET_DIRECTION_SND, 72 * 1024, NULL) == FALSE);

	zsocket_address_free (sock_addr);
	zsocket_address_free (addr);
	zsocket_free (socket);
	zsocket_free (fd_socket);

	zlibsys_shutdown ();
}
P_TEST_CASE_END ()

P_TEST_CASE_BEGIN (psocket_general_tcztest)
{
	zlibsys_init ();

	/* Test TCP socket */
	PSocket *socket = zsocket_new (P_SOCKET_FAMILY_INET,
					P_SOCKET_TYPE_STREAM,
					P_SOCKET_PROTOCOL_TCP,
					NULL);
	zsocket_set_blocking (socket, FALSE);
	zsocket_set_listen_backlog (socket, 11);

	zsocket_set_timeout (socket, -12);
	P_TEST_CHECK (zsocket_get_timeout (socket) == 0);
	zsocket_set_timeout (socket, 12);

	P_TEST_CHECK (socket != NULL);
	P_TEST_CHECK (zsocket_get_family (socket) == P_SOCKET_FAMILY_INET);
	P_TEST_CHECK (zsocket_get_fd (socket) >= 0);
	P_TEST_CHECK (zsocket_get_listen_backlog (socket) == 11);
	P_TEST_CHECK (zsocket_get_timeout (socket) == 12);
	P_TEST_CHECK (zsocket_get_remote_address (socket, NULL) == NULL);
	P_TEST_CHECK (zsocket_get_protocol (socket) == P_SOCKET_PROTOCOL_TCP);
	P_TEST_CHECK (zsocket_get_blocking (socket) == FALSE);
	P_TEST_CHECK (zsocket_get_type (socket) == P_SOCKET_TYPE_STREAM);
	P_TEST_CHECK (zsocket_get_keepalive (socket) == FALSE);
	P_TEST_CHECK (zsocket_is_closed (socket) == FALSE);

	zsocket_set_keepalive (socket, FALSE);
	P_TEST_CHECK (zsocket_get_keepalive (socket) == FALSE);

	zsocket_set_keepalive (socket, TRUE);
	zsocket_set_keepalive (socket, FALSE);
	P_TEST_CHECK (zsocket_get_keepalive (socket) == FALSE);

	PSocketAddress *sock_addr = zsocket_address_new ("127.0.0.1", 0);
	P_TEST_CHECK (sock_addr != NULL);

	P_TEST_CHECK (zsocket_bind (socket, sock_addr, TRUE, NULL) == TRUE);

	PSocketAddress *addr = zsocket_get_local_address (socket, NULL);
	P_TEST_CHECK (addr != NULL);

	P_TEST_CHECK (compare_socket_addresses (sock_addr, addr) == TRUE);

	P_TEST_CHECK (zsocket_set_buffer_size (socket, P_SOCKET_DIRECTION_RCV, 72 * 1024, NULL) == TRUE);
	P_TEST_CHECK (zsocket_set_buffer_size (socket, P_SOCKET_DIRECTION_SND, 72 * 1024, NULL) == TRUE);

	/* In case of success zsocket_check_connect_result() marks socket as connected */
	P_TEST_CHECK (zsocket_is_connected (socket) == FALSE);
	P_TEST_CHECK (zsocket_check_connect_result (socket, NULL) == TRUE);
	P_TEST_CHECK (zsocket_close (socket, NULL) == TRUE);

	pchar sock_buf[10];

	P_TEST_CHECK (zsocket_bind (socket, sock_addr, TRUE, NULL) == FALSE);
	P_TEST_CHECK (zsocket_connect (socket, addr, NULL) == FALSE);
	P_TEST_CHECK (zsocket_listen (socket, NULL) == FALSE);
	P_TEST_CHECK (zsocket_accept (socket, NULL) == FALSE);
	P_TEST_CHECK (zsocket_receive (socket, sock_buf, sizeof (sock_buf), NULL) == -1);
	P_TEST_CHECK (zsocket_receive_from (socket, NULL, sock_buf, sizeof (sock_buf), NULL) == -1);
	P_TEST_CHECK (zsocket_send (socket, sock_buf, sizeof (sock_buf), NULL) == -1);
	P_TEST_CHECK (zsocket_send_to (socket, addr, sock_buf, sizeof (sock_buf), NULL) == -1);
	P_TEST_CHECK (zsocket_shutdown (socket, TRUE, TRUE, NULL) == FALSE);
	P_TEST_CHECK (zsocket_get_local_address (socket, NULL) == NULL);
	P_TEST_CHECK (zsocket_check_connect_result (socket, NULL) == FALSE);
	P_TEST_CHECK (zsocket_is_closed (socket) == TRUE);
	P_TEST_CHECK (zsocket_get_fd (socket) == -1);

	zsocket_set_keepalive (socket, TRUE);
	P_TEST_CHECK (zsocket_get_keepalive (socket) == FALSE);

	P_TEST_CHECK (zsocket_io_condition_wait (socket, P_SOCKET_IO_CONDITION_POLLIN, NULL) == FALSE);
	P_TEST_CHECK (zsocket_io_condition_wait (socket, P_SOCKET_IO_CONDITION_POLLOUT, NULL) == FALSE);

	P_TEST_CHECK (zsocket_set_buffer_size (socket, P_SOCKET_DIRECTION_RCV, 72 * 1024, NULL) == FALSE);
	P_TEST_CHECK (zsocket_set_buffer_size (socket, P_SOCKET_DIRECTION_SND, 72 * 1024, NULL) == FALSE);

	zsocket_address_free (sock_addr);
	zsocket_address_free (addr);

	zsocket_free (socket);

	zlibsys_shutdown ();
}
P_TEST_CASE_END ()

P_TEST_CASE_BEGIN (psocket_udztest)
{
	zlibsys_init ();

	is_sender_working   = TRUE;
	is_receiver_working = TRUE;

	SocketTestData data;
	data.receiver_port    = 0;
	data.sender_port      = 0;
	data.shutdown_channel = FALSE;

	PUThread *receiver_thr = zuthread_create ((PUThreadFunc) udzsocket_receiver_thread,
						   (ppointer) &data,
						   TRUE,
						   NULL);

	PUThread *sender_thr = zuthread_create ((PUThreadFunc) udzsocket_sender_thread,
						 (ppointer) &data,
						 TRUE,
						 NULL);

	P_TEST_CHECK (sender_thr != NULL);
	P_TEST_CHECK (receiver_thr != NULL);

	zuthread_sleep (8000);

	is_sender_working = FALSE;
	pint send_counter = zuthread_join (sender_thr);

	zuthread_sleep (2000);

	is_receiver_working = FALSE;
	pint recv_counter = zuthread_join (receiver_thr);

	P_TEST_CHECK (send_counter > 0);
	P_TEST_CHECK (recv_counter > 0);

	zuthread_unref (sender_thr);
	zuthread_unref (receiver_thr);

	zlibsys_shutdown ();
}
P_TEST_CASE_END ()

P_TEST_CASE_BEGIN (psocket_tcztest)
{
	zlibsys_init ();

	is_sender_working   = TRUE;
	is_receiver_working = TRUE;

	SocketTestData data;
	data.receiver_port    = 0;
	data.sender_port      = 0;
	data.shutdown_channel = FALSE;

	PUThread *receiver_thr = zuthread_create ((PUThreadFunc) tczsocket_receiver_thread,
						   (ppointer) &data,
						   TRUE,
						   NULL);

	PUThread *sender_thr = zuthread_create ((PUThreadFunc) tczsocket_sender_thread,
						 (ppointer) &data,
						 TRUE,
						 NULL);

	P_TEST_CHECK (receiver_thr != NULL);
	P_TEST_CHECK (sender_thr != NULL);

	zuthread_sleep (8000);

	is_sender_working = FALSE;
	pint send_counter = zuthread_join (sender_thr);

	zuthread_sleep (2000);

	is_receiver_working = FALSE;
	pint recv_counter = zuthread_join (receiver_thr);

	P_TEST_CHECK (send_counter > 0);
	P_TEST_CHECK (recv_counter > 0);

	zuthread_unref (sender_thr);
	zuthread_unref (receiver_thr);

	zlibsys_shutdown ();
}
P_TEST_CASE_END ()

P_TEST_CASE_BEGIN (psocket_shutdown_test)
{
	zlibsys_init ();

	is_sender_working   = TRUE;
	is_receiver_working = TRUE;

	SocketTestData data;
	data.receiver_port    = 0;
	data.sender_port      = 0;
	data.shutdown_channel = TRUE;

	PUThread *receiver_thr = zuthread_create ((PUThreadFunc) tczsocket_receiver_thread,
						   (ppointer) &data,
						   TRUE,
						   NULL);

	PUThread *sender_thr = zuthread_create ((PUThreadFunc) tczsocket_sender_thread,
						 (ppointer) &data,
						 TRUE,
						 NULL);

	P_TEST_CHECK (receiver_thr != NULL);
	P_TEST_CHECK (sender_thr != NULL);

	zuthread_sleep (8000);

	is_sender_working = FALSE;
	pint send_counter = zuthread_join (sender_thr);

	zuthread_sleep (2000);

	is_receiver_working = FALSE;
	pint recv_counter = zuthread_join (receiver_thr);

	P_TEST_CHECK (send_counter == 0);
	P_TEST_CHECK (recv_counter == 0);

	zuthread_unref (sender_thr);
	zuthread_unref (receiver_thr);

	zlibsys_shutdown ();
}
P_TEST_CASE_END ()

P_TEST_SUITE_BEGIN()
{
	P_TEST_SUITE_RUN_CASE (psocket_nomem_test);
	P_TEST_SUITE_RUN_CASE (psocket_bad_input_test);
	P_TEST_SUITE_RUN_CASE (psocket_general_udztest);
	P_TEST_SUITE_RUN_CASE (psocket_general_tcztest);
	P_TEST_SUITE_RUN_CASE (psocket_udztest);
	P_TEST_SUITE_RUN_CASE (psocket_tcztest);
	P_TEST_SUITE_RUN_CASE (psocket_shutdown_test);
}
P_TEST_SUITE_END()
