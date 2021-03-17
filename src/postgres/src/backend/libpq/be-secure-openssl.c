/*-------------------------------------------------------------------------
 *
 * be-secure-openssl.c
 *	  functions for OpenSSL support in the backend.
 *
 *
 * Portions Copyright (c) 1996-2018, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 * IDENTIFICATION
 *	  src/backend/libpq/be-secure-openssl.c
 *
 *-------------------------------------------------------------------------
 */

#include "postgres.h"

#include <sys/stat.h>
#include <signal.h>
#include <fcntl.h>
#include <ctype.h>
#include <sys/socket.h>
#include <unistd.h>
#include <netdb.h>
#include <netinet/in.h>
#ifdef HAVE_NETINET_TCP_H
#include <netinet/tcp.h>
#include <arpa/inet.h>
#endif

#include <openssl/ssl.h>
#include <openssl/dh.h>
#include <openssl/conf.h>
#ifndef OPENSSL_NO_ECDH
#include <openssl/ec.h>
#endif

#include "libpq/libpq.h"
#include "miscadmin.h"
#include "pgstat.h"
#include "storage/fd.h"
#include "storage/latch.h"
#include "tcop/tcopprot.h"
#include "utils/memutils.h"


static int	my_sock_read(BIO *h, char *buf, int size);
static int	my_sock_write(BIO *h, const char *buf, int size);
static BIO_METHOD *my_BIO_s_socket(void);
static int	my_SSL_set_fd(Port *port, int fd);

static DH  *load_dh_file(char *filename, bool isServerStart);
static DH  *load_dh_buffer(const char *, size_t);
static int	ssl_external_passwd_cb(char *buf, int size, int rwflag, void *userdata);
static int	dummy_ssl_passwd_cb(char *buf, int size, int rwflag, void *userdata);
static int	verify_cb(int, X509_STORE_CTX *);
static void info_cb(const SSL *ssl, int type, int args);
static bool initialize_dh(SSL_CTX *context, bool isServerStart);
static bool initialize_ecdh(SSL_CTX *context, bool isServerStart);
static const char *SSLerrmessage(unsigned long ecode);

static char *X509_NAME_to_cstring(X509_NAME *name);

static SSL_CTX *SSL_context = NULL;
static bool SSL_initialized = false;
static bool dummy_ssl_passwd_cb_called = false;
static bool ssl_is_server_start;

static int ssl_protocol_version_to_openssl(int v, const char *guc_name,
										   int loglevel);
#ifndef SSL_CTX_set_min_proto_version
static int SSL_CTX_set_min_proto_version(SSL_CTX *ctx, int version);
static int SSL_CTX_set_max_proto_version(SSL_CTX *ctx, int version);
#endif


/* ------------------------------------------------------------ */
/*						 Public interface						*/
/* ------------------------------------------------------------ */

int
be_tls_init(bool isServerStart)
{
	SSL_CTX    *context;

	/* This stuff need be done only once. */
	if (!SSL_initialized)
	{
#ifdef HAVE_OPENSSL_INIT_SSL
		OPENSSL_init_ssl(OPENSSL_INIT_LOAD_CONFIG, NULL);
#else
		OPENSSL_config(NULL);
		SSL_library_init();
		SSL_load_error_strings();
#endif
		SSL_initialized = true;
	}

	/*
	 * Create a new SSL context into which we'll load all the configuration
	 * settings.  If we fail partway through, we can avoid memory leakage by
	 * freeing this context; we don't install it as active until the end.
	 *
	 * We use SSLv23_method() because it can negotiate use of the highest
	 * mutually supported protocol version, while alternatives like
	 * TLSv1_2_method() permit only one specific version.  Note that we don't
	 * actually allow SSL v2 or v3, only TLS protocols (see below).
	 */
	context = SSL_CTX_new(SSLv23_method());
	if (!context)
	{
		ereport(isServerStart ? FATAL : LOG,
				(errmsg("could not create SSL context: %s",
						SSLerrmessage(ERR_get_error()))));
		goto error;
	}

	/*
	 * Disable OpenSSL's moving-write-buffer sanity check, because it causes
	 * unnecessary failures in nonblocking send cases.
	 */
	SSL_CTX_set_mode(context, SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER);

	/*
	 * Set password callback
	 */
	if (isServerStart)
	{
		if (ssl_passphrase_command[0])
			SSL_CTX_set_default_passwd_cb(context, ssl_external_passwd_cb);
	}
	else
	{
		if (ssl_passphrase_command[0] && ssl_passphrase_command_supports_reload)
			SSL_CTX_set_default_passwd_cb(context, ssl_external_passwd_cb);
		else

			/*
			 * If reloading and no external command is configured, override
			 * OpenSSL's default handling of passphrase-protected files,
			 * because we don't want to prompt for a passphrase in an
			 * already-running server.
			 */
			SSL_CTX_set_default_passwd_cb(context, dummy_ssl_passwd_cb);
	}
	/* used by the callback */
	ssl_is_server_start = isServerStart;

	/*
	 * Load and verify server's certificate and private key
	 */
	if (SSL_CTX_use_certificate_chain_file(context, ssl_cert_file) != 1)
	{
		ereport(isServerStart ? FATAL : LOG,
				(errcode(ERRCODE_CONFIG_FILE_ERROR),
				 errmsg("could not load server certificate file \"%s\": %s",
						ssl_cert_file, SSLerrmessage(ERR_get_error()))));
		goto error;
	}

	if (!check_ssl_key_file_permissions(ssl_key_file, isServerStart))
		goto error;

	/*
	 * OK, try to load the private key file.
	 */
	dummy_ssl_passwd_cb_called = false;

	if (SSL_CTX_use_PrivateKey_file(context,
									ssl_key_file,
									SSL_FILETYPE_PEM) != 1)
	{
		if (dummy_ssl_passwd_cb_called)
			ereport(isServerStart ? FATAL : LOG,
					(errcode(ERRCODE_CONFIG_FILE_ERROR),
					 errmsg("private key file \"%s\" cannot be reloaded because it requires a passphrase",
							ssl_key_file)));
		else
			ereport(isServerStart ? FATAL : LOG,
					(errcode(ERRCODE_CONFIG_FILE_ERROR),
					 errmsg("could not load private key file \"%s\": %s",
							ssl_key_file, SSLerrmessage(ERR_get_error()))));
		goto error;
	}

	if (SSL_CTX_check_private_key(context) != 1)
	{
		ereport(isServerStart ? FATAL : LOG,
				(errcode(ERRCODE_CONFIG_FILE_ERROR),
				 errmsg("check of private key failed: %s",
						SSLerrmessage(ERR_get_error()))));
		goto error;
	}

	if (ssl_min_protocol_version)
	{
		int ssl_ver = ssl_protocol_version_to_openssl(ssl_min_protocol_version,
													  "ssl_min_protocol_version",
													  isServerStart ? FATAL : LOG);
		if (ssl_ver == -1)
			goto error;
		if (!SSL_CTX_set_min_proto_version(context, ssl_ver))
		{
			ereport(isServerStart ? FATAL : LOG,
					(errmsg("could not set minimum SSL protocol version")));
			goto error;
		}
	}

	if (ssl_max_protocol_version)
	{
		int ssl_ver = ssl_protocol_version_to_openssl(ssl_max_protocol_version,
													  "ssl_max_protocol_version",
													  isServerStart ? FATAL : LOG);
		if (ssl_ver == -1)
			goto error;
		if (!SSL_CTX_set_max_proto_version(context, ssl_ver))
		{
			ereport(isServerStart ? FATAL : LOG,
					(errmsg("could not set maximum SSL protocol version")));
			goto error;
		}
	}

	/* disallow SSL session tickets */
#ifdef SSL_OP_NO_TICKET			/* added in OpenSSL 0.9.8f */
	SSL_CTX_set_options(context, SSL_OP_NO_TICKET);
#endif

	/* disallow SSL session caching, too */
	SSL_CTX_set_session_cache_mode(context, SSL_SESS_CACHE_OFF);

	/* set up ephemeral DH and ECDH keys */
	if (!initialize_dh(context, isServerStart))
		goto error;
	if (!initialize_ecdh(context, isServerStart))
		goto error;

	/* set up the allowed cipher list */
	if (SSL_CTX_set_cipher_list(context, SSLCipherSuites) != 1)
	{
		ereport(isServerStart ? FATAL : LOG,
				(errcode(ERRCODE_CONFIG_FILE_ERROR),
				 errmsg("could not set the cipher list (no valid ciphers available)")));
		goto error;
	}

	/* Let server choose order */
	if (SSLPreferServerCiphers)
		SSL_CTX_set_options(context, SSL_OP_CIPHER_SERVER_PREFERENCE);

	/*
	 * Load CA store, so we can verify client certificates if needed.
	 */
	if (ssl_ca_file[0])
	{
		STACK_OF(X509_NAME) * root_cert_list;

		if (SSL_CTX_load_verify_locations(context, ssl_ca_file, NULL) != 1 ||
			(root_cert_list = SSL_load_client_CA_file(ssl_ca_file)) == NULL)
		{
			ereport(isServerStart ? FATAL : LOG,
					(errcode(ERRCODE_CONFIG_FILE_ERROR),
					 errmsg("could not load root certificate file \"%s\": %s",
							ssl_ca_file, SSLerrmessage(ERR_get_error()))));
			goto error;
		}

		/*
		 * Tell OpenSSL to send the list of root certs we trust to clients in
		 * CertificateRequests.  This lets a client with a keystore select the
		 * appropriate client certificate to send to us.  Also, this ensures
		 * that the SSL context will "own" the root_cert_list and remember to
		 * free it when no longer needed.
		 */
		SSL_CTX_set_client_CA_list(context, root_cert_list);

		/*
		 * Always ask for SSL client cert, but don't fail if it's not
		 * presented.  We might fail such connections later, depending on what
		 * we find in pg_hba.conf.
		 */
		SSL_CTX_set_verify(context,
						   (SSL_VERIFY_PEER |
							SSL_VERIFY_CLIENT_ONCE),
						   verify_cb);
	}

	/*----------
	 * Load the Certificate Revocation List (CRL).
	 * http://searchsecurity.techtarget.com/sDefinition/0,,sid14_gci803160,00.html
	 *----------
	 */
	if (ssl_crl_file[0])
	{
		X509_STORE *cvstore = SSL_CTX_get_cert_store(context);

		if (cvstore)
		{
			/* Set the flags to check against the complete CRL chain */
			if (X509_STORE_load_locations(cvstore, ssl_crl_file, NULL) == 1)
			{
				X509_STORE_set_flags(cvstore,
									 X509_V_FLAG_CRL_CHECK | X509_V_FLAG_CRL_CHECK_ALL);
			}
			else
			{
				ereport(isServerStart ? FATAL : LOG,
						(errcode(ERRCODE_CONFIG_FILE_ERROR),
						 errmsg("could not load SSL certificate revocation list file \"%s\": %s",
								ssl_crl_file, SSLerrmessage(ERR_get_error()))));
				goto error;
			}
		}
	}

	/*
	 * Success!  Replace any existing SSL_context.
	 */
	if (SSL_context)
		SSL_CTX_free(SSL_context);

	SSL_context = context;

	/*
	 * Set flag to remember whether CA store has been loaded into SSL_context.
	 */
	if (ssl_ca_file[0])
		ssl_loaded_verify_locations = true;
	else
		ssl_loaded_verify_locations = false;

	return 0;

	/* Clean up by releasing working context. */
error:
	if (context)
		SSL_CTX_free(context);
	return -1;
}

void
be_tls_destroy(void)
{
	if (SSL_context)
		SSL_CTX_free(SSL_context);
	SSL_context = NULL;
	ssl_loaded_verify_locations = false;
}

int
be_tls_open_server(Port *port)
{
	int			r;
	int			err;
	int			waitfor;
	unsigned long ecode;

	Assert(!port->ssl);
	Assert(!port->peer);

	if (!SSL_context)
	{
		ereport(COMMERROR,
				(errcode(ERRCODE_PROTOCOL_VIOLATION),
				 errmsg("could not initialize SSL connection: SSL context not set up")));
		return -1;
	}

	if (!(port->ssl = SSL_new(SSL_context)))
	{
		ereport(COMMERROR,
				(errcode(ERRCODE_PROTOCOL_VIOLATION),
				 errmsg("could not initialize SSL connection: %s",
						SSLerrmessage(ERR_get_error()))));
		return -1;
	}
	if (!my_SSL_set_fd(port, port->sock))
	{
		ereport(COMMERROR,
				(errcode(ERRCODE_PROTOCOL_VIOLATION),
				 errmsg("could not set SSL socket: %s",
						SSLerrmessage(ERR_get_error()))));
		return -1;
	}
	port->ssl_in_use = true;

aloop:

	/*
	 * Prepare to call SSL_get_error() by clearing thread's OpenSSL error
	 * queue.  In general, the current thread's error queue must be empty
	 * before the TLS/SSL I/O operation is attempted, or SSL_get_error() will
	 * not work reliably.  An extension may have failed to clear the
	 * per-thread error queue following another call to an OpenSSL I/O
	 * routine.
	 */
	ERR_clear_error();
	r = SSL_accept(port->ssl);
	if (r <= 0)
	{
		err = SSL_get_error(port->ssl, r);

		/*
		 * Other clients of OpenSSL in the backend may fail to call
		 * ERR_get_error(), but we always do, so as to not cause problems for
		 * OpenSSL clients that don't call ERR_clear_error() defensively.  Be
		 * sure that this happens by calling now. SSL_get_error() relies on
		 * the OpenSSL per-thread error queue being intact, so this is the
		 * earliest possible point ERR_get_error() may be called.
		 */
		ecode = ERR_get_error();
		switch (err)
		{
			case SSL_ERROR_WANT_READ:
			case SSL_ERROR_WANT_WRITE:
				/* not allowed during connection establishment */
				Assert(!port->noblock);

				/*
				 * No need to care about timeouts/interrupts here. At this
				 * point authentication_timeout still employs
				 * StartupPacketTimeoutHandler() which directly exits.
				 */
				if (err == SSL_ERROR_WANT_READ)
					waitfor = WL_SOCKET_READABLE;
				else
					waitfor = WL_SOCKET_WRITEABLE;

				WaitLatchOrSocket(MyLatch, waitfor, port->sock, 0,
								  WAIT_EVENT_SSL_OPEN_SERVER);
				goto aloop;
			case SSL_ERROR_SYSCALL:
				if (r < 0)
					ereport(COMMERROR,
							(errcode_for_socket_access(),
							 errmsg("could not accept SSL connection: %m")));
				else
					ereport(COMMERROR,
							(errcode(ERRCODE_PROTOCOL_VIOLATION),
							 errmsg("could not accept SSL connection: EOF detected")));
				break;
			case SSL_ERROR_SSL:
				ereport(COMMERROR,
						(errcode(ERRCODE_PROTOCOL_VIOLATION),
						 errmsg("could not accept SSL connection: %s",
								SSLerrmessage(ecode))));
				break;
			case SSL_ERROR_ZERO_RETURN:
				ereport(COMMERROR,
						(errcode(ERRCODE_PROTOCOL_VIOLATION),
						 errmsg("could not accept SSL connection: EOF detected")));
				break;
			default:
				ereport(COMMERROR,
						(errcode(ERRCODE_PROTOCOL_VIOLATION),
						 errmsg("unrecognized SSL error code: %d",
								err)));
				break;
		}
		return -1;
	}

	/* Get client certificate, if available. */
	port->peer = SSL_get_peer_certificate(port->ssl);

	/* and extract the Common Name from it. */
	port->peer_cn = NULL;
	port->peer_cert_valid = false;
	if (port->peer != NULL)
	{
		int			len;

		len = X509_NAME_get_text_by_NID(X509_get_subject_name(port->peer),
										NID_commonName, NULL, 0);
		if (len != -1)
		{
			char	   *peer_cn;

			peer_cn = MemoryContextAlloc(TopMemoryContext, len + 1);
			r = X509_NAME_get_text_by_NID(X509_get_subject_name(port->peer),
										  NID_commonName, peer_cn, len + 1);
			peer_cn[len] = '\0';
			if (r != len)
			{
				/* shouldn't happen */
				pfree(peer_cn);
				return -1;
			}

			/*
			 * Reject embedded NULLs in certificate common name to prevent
			 * attacks like CVE-2009-4034.
			 */
			if (len != strlen(peer_cn))
			{
				ereport(COMMERROR,
						(errcode(ERRCODE_PROTOCOL_VIOLATION),
						 errmsg("SSL certificate's common name contains embedded null")));
				pfree(peer_cn);
				return -1;
			}

			port->peer_cn = peer_cn;
		}
		port->peer_cert_valid = true;
	}

	/* set up debugging/info callback */
	SSL_CTX_set_info_callback(SSL_context, info_cb);

	return 0;
}

void
be_tls_close(Port *port)
{
	if (port->ssl)
	{
		SSL_shutdown(port->ssl);
		SSL_free(port->ssl);
		port->ssl = NULL;
		port->ssl_in_use = false;
	}

	if (port->peer)
	{
		X509_free(port->peer);
		port->peer = NULL;
	}

	if (port->peer_cn)
	{
		pfree(port->peer_cn);
		port->peer_cn = NULL;
	}
}

ssize_t
be_tls_read(Port *port, void *ptr, size_t len, int *waitfor)
{
	ssize_t		n;
	int			err;
	unsigned long ecode;

	errno = 0;
	ERR_clear_error();
	n = SSL_read(port->ssl, ptr, len);
	err = SSL_get_error(port->ssl, n);
	ecode = (err != SSL_ERROR_NONE || n < 0) ? ERR_get_error() : 0;
	switch (err)
	{
		case SSL_ERROR_NONE:
			/* a-ok */
			break;
		case SSL_ERROR_WANT_READ:
			*waitfor = WL_SOCKET_READABLE;
			errno = EWOULDBLOCK;
			n = -1;
			break;
		case SSL_ERROR_WANT_WRITE:
			*waitfor = WL_SOCKET_WRITEABLE;
			errno = EWOULDBLOCK;
			n = -1;
			break;
		case SSL_ERROR_SYSCALL:
			/* leave it to caller to ereport the value of errno */
			if (n != -1)
			{
				errno = ECONNRESET;
				n = -1;
			}
			break;
		case SSL_ERROR_SSL:
			ereport(COMMERROR,
					(errcode(ERRCODE_PROTOCOL_VIOLATION),
					 errmsg("SSL error: %s", SSLerrmessage(ecode))));
			errno = ECONNRESET;
			n = -1;
			break;
		case SSL_ERROR_ZERO_RETURN:
			/* connection was cleanly shut down by peer */
			n = 0;
			break;
		default:
			ereport(COMMERROR,
					(errcode(ERRCODE_PROTOCOL_VIOLATION),
					 errmsg("unrecognized SSL error code: %d",
							err)));
			errno = ECONNRESET;
			n = -1;
			break;
	}

	return n;
}

ssize_t
be_tls_write(Port *port, void *ptr, size_t len, int *waitfor)
{
	ssize_t		n;
	int			err;
	unsigned long ecode;

	errno = 0;
	ERR_clear_error();
	n = SSL_write(port->ssl, ptr, len);
	err = SSL_get_error(port->ssl, n);
	ecode = (err != SSL_ERROR_NONE || n < 0) ? ERR_get_error() : 0;
	switch (err)
	{
		case SSL_ERROR_NONE:
			/* a-ok */
			break;
		case SSL_ERROR_WANT_READ:
			*waitfor = WL_SOCKET_READABLE;
			errno = EWOULDBLOCK;
			n = -1;
			break;
		case SSL_ERROR_WANT_WRITE:
			*waitfor = WL_SOCKET_WRITEABLE;
			errno = EWOULDBLOCK;
			n = -1;
			break;
		case SSL_ERROR_SYSCALL:
			/* leave it to caller to ereport the value of errno */
			if (n != -1)
			{
				errno = ECONNRESET;
				n = -1;
			}
			break;
		case SSL_ERROR_SSL:
			ereport(COMMERROR,
					(errcode(ERRCODE_PROTOCOL_VIOLATION),
					 errmsg("SSL error: %s", SSLerrmessage(ecode))));
			errno = ECONNRESET;
			n = -1;
			break;
		case SSL_ERROR_ZERO_RETURN:

			/*
			 * the SSL connection was closed, leave it to the caller to
			 * ereport it
			 */
			errno = ECONNRESET;
			n = -1;
			break;
		default:
			ereport(COMMERROR,
					(errcode(ERRCODE_PROTOCOL_VIOLATION),
					 errmsg("unrecognized SSL error code: %d",
							err)));
			errno = ECONNRESET;
			n = -1;
			break;
	}

	return n;
}

/* ------------------------------------------------------------ */
/*						Internal functions						*/
/* ------------------------------------------------------------ */

/*
 * Private substitute BIO: this does the sending and receiving using send() and
 * recv() instead. This is so that we can enable and disable interrupts
 * just while calling recv(). We cannot have interrupts occurring while
 * the bulk of OpenSSL runs, because it uses malloc() and possibly other
 * non-reentrant libc facilities. We also need to call send() and recv()
 * directly so it gets passed through the socket/signals layer on Win32.
 *
 * These functions are closely modelled on the standard socket BIO in OpenSSL;
 * see sock_read() and sock_write() in OpenSSL's crypto/bio/bss_sock.c.
 * XXX OpenSSL 1.0.1e considers many more errcodes than just EINTR as reasons
 * to retry; do we need to adopt their logic for that?
 */

#ifndef HAVE_BIO_GET_DATA
#define BIO_get_data(bio) (bio->ptr)
#define BIO_set_data(bio, data) (bio->ptr = data)
#endif

static BIO_METHOD *my_bio_methods = NULL;

static int
my_sock_read(BIO *h, char *buf, int size)
{
	int			res = 0;

	if (buf != NULL)
	{
		res = secure_raw_read(((Port *) BIO_get_data(h)), buf, size);
		BIO_clear_retry_flags(h);
		if (res <= 0)
		{
			/* If we were interrupted, tell caller to retry */
			if (errno == EINTR || errno == EWOULDBLOCK || errno == EAGAIN)
			{
				BIO_set_retry_read(h);
			}
		}
	}

	return res;
}

static int
my_sock_write(BIO *h, const char *buf, int size)
{
	int			res = 0;

	res = secure_raw_write(((Port *) BIO_get_data(h)), buf, size);
	BIO_clear_retry_flags(h);
	if (res <= 0)
	{
		/* If we were interrupted, tell caller to retry */
		if (errno == EINTR || errno == EWOULDBLOCK || errno == EAGAIN)
		{
			BIO_set_retry_write(h);
		}
	}

	return res;
}

static BIO_METHOD *
my_BIO_s_socket(void)
{
	if (!my_bio_methods)
	{
		BIO_METHOD *biom = (BIO_METHOD *) BIO_s_socket();
#ifdef HAVE_BIO_METH_NEW
		int			my_bio_index;

		my_bio_index = BIO_get_new_index();
		if (my_bio_index == -1)
			return NULL;
		my_bio_methods = BIO_meth_new(my_bio_index, "PostgreSQL backend socket");
		if (!my_bio_methods)
			return NULL;
		if (!BIO_meth_set_write(my_bio_methods, my_sock_write) ||
			!BIO_meth_set_read(my_bio_methods, my_sock_read) ||
			!BIO_meth_set_gets(my_bio_methods, BIO_meth_get_gets(biom)) ||
			!BIO_meth_set_puts(my_bio_methods, BIO_meth_get_puts(biom)) ||
			!BIO_meth_set_ctrl(my_bio_methods, BIO_meth_get_ctrl(biom)) ||
			!BIO_meth_set_create(my_bio_methods, BIO_meth_get_create(biom)) ||
			!BIO_meth_set_destroy(my_bio_methods, BIO_meth_get_destroy(biom)) ||
			!BIO_meth_set_callback_ctrl(my_bio_methods, BIO_meth_get_callback_ctrl(biom)))
		{
			BIO_meth_free(my_bio_methods);
			my_bio_methods = NULL;
			return NULL;
		}
#else
		my_bio_methods = malloc(sizeof(BIO_METHOD));
		if (!my_bio_methods)
			return NULL;
		memcpy(my_bio_methods, biom, sizeof(BIO_METHOD));
		my_bio_methods->bread = my_sock_read;
		my_bio_methods->bwrite = my_sock_write;
#endif
	}
	return my_bio_methods;
}

/* This should exactly match OpenSSL's SSL_set_fd except for using my BIO */
static int
my_SSL_set_fd(Port *port, int fd)
{
	int			ret = 0;
	BIO		   *bio;
	BIO_METHOD *bio_method;

	bio_method = my_BIO_s_socket();
	if (bio_method == NULL)
	{
		SSLerr(SSL_F_SSL_SET_FD, ERR_R_BUF_LIB);
		goto err;
	}
	bio = BIO_new(bio_method);

	if (bio == NULL)
	{
		SSLerr(SSL_F_SSL_SET_FD, ERR_R_BUF_LIB);
		goto err;
	}
	BIO_set_data(bio, port);

	BIO_set_fd(bio, fd, BIO_NOCLOSE);
	SSL_set_bio(port->ssl, bio, bio);
	ret = 1;
err:
	return ret;
}

/*
 *	Load precomputed DH parameters.
 *
 *	To prevent "downgrade" attacks, we perform a number of checks
 *	to verify that the DBA-generated DH parameters file contains
 *	what we expect it to contain.
 */
static DH  *
load_dh_file(char *filename, bool isServerStart)
{
	FILE	   *fp;
	DH		   *dh = NULL;
	int			codes;

	/* attempt to open file.  It's not an error if it doesn't exist. */
	if ((fp = AllocateFile(filename, "r")) == NULL)
	{
		ereport(isServerStart ? FATAL : LOG,
				(errcode_for_file_access(),
				 errmsg("could not open DH parameters file \"%s\": %m",
						filename)));
		return NULL;
	}

	dh = PEM_read_DHparams(fp, NULL, NULL, NULL);
	FreeFile(fp);

	if (dh == NULL)
	{
		ereport(isServerStart ? FATAL : LOG,
				(errcode(ERRCODE_CONFIG_FILE_ERROR),
				 errmsg("could not load DH parameters file: %s",
						SSLerrmessage(ERR_get_error()))));
		return NULL;
	}

	/* make sure the DH parameters are usable */
	if (DH_check(dh, &codes) == 0)
	{
		ereport(isServerStart ? FATAL : LOG,
				(errcode(ERRCODE_CONFIG_FILE_ERROR),
				 errmsg("invalid DH parameters: %s",
						SSLerrmessage(ERR_get_error()))));
		return NULL;
	}
	if (codes & DH_CHECK_P_NOT_PRIME)
	{
		ereport(isServerStart ? FATAL : LOG,
				(errcode(ERRCODE_CONFIG_FILE_ERROR),
				 errmsg("invalid DH parameters: p is not prime")));
		return NULL;
	}
	if ((codes & DH_NOT_SUITABLE_GENERATOR) &&
		(codes & DH_CHECK_P_NOT_SAFE_PRIME))
	{
		ereport(isServerStart ? FATAL : LOG,
				(errcode(ERRCODE_CONFIG_FILE_ERROR),
				 errmsg("invalid DH parameters: neither suitable generator or safe prime")));
		return NULL;
	}

	return dh;
}

/*
 *	Load hardcoded DH parameters.
 *
 *	To prevent problems if the DH parameters files don't even
 *	exist, we can load DH parameters hardcoded into this file.
 */
static DH  *
load_dh_buffer(const char *buffer, size_t len)
{
	BIO		   *bio;
	DH		   *dh = NULL;

	bio = BIO_new_mem_buf((char *) buffer, len);
	if (bio == NULL)
		return NULL;
	dh = PEM_read_bio_DHparams(bio, NULL, NULL, NULL);
	if (dh == NULL)
		ereport(DEBUG2,
				(errmsg_internal("DH load buffer: %s",
								 SSLerrmessage(ERR_get_error()))));
	BIO_free(bio);

	return dh;
}

/*
 *	Passphrase collection callback using ssl_passphrase_command
 */
static int
ssl_external_passwd_cb(char *buf, int size, int rwflag, void *userdata)
{
	/* same prompt as OpenSSL uses internally */
	const char *prompt = "Enter PEM pass phrase:";

	Assert(rwflag == 0);

	return run_ssl_passphrase_command(prompt, ssl_is_server_start, buf, size);
}

/*
 * Dummy passphrase callback
 *
 * If OpenSSL is told to use a passphrase-protected server key, by default
 * it will issue a prompt on /dev/tty and try to read a key from there.
 * That's no good during a postmaster SIGHUP cycle, not to mention SSL context
 * reload in an EXEC_BACKEND postmaster child.  So override it with this dummy
 * function that just returns an empty passphrase, guaranteeing failure.
 */
static int
dummy_ssl_passwd_cb(char *buf, int size, int rwflag, void *userdata)
{
	/* Set flag to change the error message we'll report */
	dummy_ssl_passwd_cb_called = true;
	/* And return empty string */
	Assert(size > 0);
	buf[0] = '\0';
	return 0;
}

/*
 *	Certificate verification callback
 *
 *	This callback allows us to log intermediate problems during
 *	verification, but for now we'll see if the final error message
 *	contains enough information.
 *
 *	This callback also allows us to override the default acceptance
 *	criteria (e.g., accepting self-signed or expired certs), but
 *	for now we accept the default checks.
 */
static int
verify_cb(int ok, X509_STORE_CTX *ctx)
{
	return ok;
}

/*
 *	This callback is used to copy SSL information messages
 *	into the PostgreSQL log.
 */
static void
info_cb(const SSL *ssl, int type, int args)
{
	switch (type)
	{
		case SSL_CB_HANDSHAKE_START:
			ereport(DEBUG4,
					(errmsg_internal("SSL: handshake start")));
			break;
		case SSL_CB_HANDSHAKE_DONE:
			ereport(DEBUG4,
					(errmsg_internal("SSL: handshake done")));
			break;
		case SSL_CB_ACCEPT_LOOP:
			ereport(DEBUG4,
					(errmsg_internal("SSL: accept loop")));
			break;
		case SSL_CB_ACCEPT_EXIT:
			ereport(DEBUG4,
					(errmsg_internal("SSL: accept exit (%d)", args)));
			break;
		case SSL_CB_CONNECT_LOOP:
			ereport(DEBUG4,
					(errmsg_internal("SSL: connect loop")));
			break;
		case SSL_CB_CONNECT_EXIT:
			ereport(DEBUG4,
					(errmsg_internal("SSL: connect exit (%d)", args)));
			break;
		case SSL_CB_READ_ALERT:
			ereport(DEBUG4,
					(errmsg_internal("SSL: read alert (0x%04x)", args)));
			break;
		case SSL_CB_WRITE_ALERT:
			ereport(DEBUG4,
					(errmsg_internal("SSL: write alert (0x%04x)", args)));
			break;
	}
}

/*
 * Set DH parameters for generating ephemeral DH keys.  The
 * DH parameters can take a long time to compute, so they must be
 * precomputed.
 *
 * Since few sites will bother to create a parameter file, we also
 * also provide a fallback to the parameters provided by the
 * OpenSSL project.
 *
 * These values can be static (once loaded or computed) since the
 * OpenSSL library can efficiently generate random keys from the
 * information provided.
 */
static bool
initialize_dh(SSL_CTX *context, bool isServerStart)
{
	DH		   *dh = NULL;

	SSL_CTX_set_options(context, SSL_OP_SINGLE_DH_USE);

	if (ssl_dh_params_file[0])
		dh = load_dh_file(ssl_dh_params_file, isServerStart);
	if (!dh)
		dh = load_dh_buffer(FILE_DH2048, sizeof(FILE_DH2048));
	if (!dh)
	{
		ereport(isServerStart ? FATAL : LOG,
				(errcode(ERRCODE_CONFIG_FILE_ERROR),
				 (errmsg("DH: could not load DH parameters"))));
		return false;
	}

	if (SSL_CTX_set_tmp_dh(context, dh) != 1)
	{
		ereport(isServerStart ? FATAL : LOG,
				(errcode(ERRCODE_CONFIG_FILE_ERROR),
				 (errmsg("DH: could not set DH parameters: %s",
						 SSLerrmessage(ERR_get_error())))));
		DH_free(dh);
		return false;
	}

	DH_free(dh);
	return true;
}

/*
 * Set ECDH parameters for generating ephemeral Elliptic Curve DH
 * keys.  This is much simpler than the DH parameters, as we just
 * need to provide the name of the curve to OpenSSL.
 */
static bool
initialize_ecdh(SSL_CTX *context, bool isServerStart)
{
#ifndef OPENSSL_NO_ECDH
	EC_KEY	   *ecdh;
	int			nid;

	nid = OBJ_sn2nid(SSLECDHCurve);
	if (!nid)
	{
		ereport(isServerStart ? FATAL : LOG,
				(errcode(ERRCODE_CONFIG_FILE_ERROR),
				 errmsg("ECDH: unrecognized curve name: %s", SSLECDHCurve)));
		return false;
	}

	ecdh = EC_KEY_new_by_curve_name(nid);
	if (!ecdh)
	{
		ereport(isServerStart ? FATAL : LOG,
				(errcode(ERRCODE_CONFIG_FILE_ERROR),
				 errmsg("ECDH: could not create key")));
		return false;
	}

	SSL_CTX_set_options(context, SSL_OP_SINGLE_ECDH_USE);
	SSL_CTX_set_tmp_ecdh(context, ecdh);
	EC_KEY_free(ecdh);
#endif

	return true;
}

/*
 * Obtain reason string for passed SSL errcode
 *
 * ERR_get_error() is used by caller to get errcode to pass here.
 *
 * Some caution is needed here since ERR_reason_error_string will
 * return NULL if it doesn't recognize the error code.  We don't
 * want to return NULL ever.
 */
static const char *
SSLerrmessage(unsigned long ecode)
{
	const char *errreason;
	static char errbuf[36];

	if (ecode == 0)
		return _("no SSL error reported");
	errreason = ERR_reason_error_string(ecode);
	if (errreason != NULL)
		return errreason;
	snprintf(errbuf, sizeof(errbuf), _("SSL error code %lu"), ecode);
	return errbuf;
}

int
be_tls_get_cipher_bits(Port *port)
{
	int			bits;

	if (port->ssl)
	{
		SSL_get_cipher_bits(port->ssl, &bits);
		return bits;
	}
	else
		return 0;
}

bool
be_tls_get_compression(Port *port)
{
	if (port->ssl)
		return (SSL_get_current_compression(port->ssl) != NULL);
	else
		return false;
}

const char *
be_tls_get_version(Port *port)
{
	if (port->ssl)
		return SSL_get_version(port->ssl);
	else
		return NULL;
}

const char *
be_tls_get_cipher(Port *port)
{
	if (port->ssl)
		return SSL_get_cipher(port->ssl);
	else
		return NULL;
}

void
be_tls_get_peerdn_name(Port *port, char *ptr, size_t len)
{
	if (port->peer)
		strlcpy(ptr, X509_NAME_to_cstring(X509_get_subject_name(port->peer)), len);
	else
		ptr[0] = '\0';
}

#ifdef HAVE_X509_GET_SIGNATURE_NID
char *
be_tls_get_certificate_hash(Port *port, size_t *len)
{
	X509	   *server_cert;
	char	   *cert_hash;
	const EVP_MD *algo_type = NULL;
	unsigned char hash[EVP_MAX_MD_SIZE];	/* size for SHA-512 */
	unsigned int hash_size;
	int			algo_nid;

	*len = 0;
	server_cert = SSL_get_certificate(port->ssl);
	if (server_cert == NULL)
		return NULL;

	/*
	 * Get the signature algorithm of the certificate to determine the hash
	 * algorithm to use for the result.
	 */
	if (!OBJ_find_sigid_algs(X509_get_signature_nid(server_cert),
							 &algo_nid, NULL))
		elog(ERROR, "could not determine server certificate signature algorithm");

	/*
	 * The TLS server's certificate bytes need to be hashed with SHA-256 if
	 * its signature algorithm is MD5 or SHA-1 as per RFC 5929
	 * (https://tools.ietf.org/html/rfc5929#section-4.1).  If something else
	 * is used, the same hash as the signature algorithm is used.
	 */
	switch (algo_nid)
	{
		case NID_md5:
		case NID_sha1:
			algo_type = EVP_sha256();
			break;
		default:
			algo_type = EVP_get_digestbynid(algo_nid);
			if (algo_type == NULL)
				elog(ERROR, "could not find digest for NID %s",
					 OBJ_nid2sn(algo_nid));
			break;
	}

	/* generate and save the certificate hash */
	if (!X509_digest(server_cert, algo_type, hash, &hash_size))
		elog(ERROR, "could not generate server certificate hash");

	cert_hash = palloc(hash_size);
	memcpy(cert_hash, hash, hash_size);
	*len = hash_size;

	return cert_hash;
}
#endif

/*
 * Convert an X509 subject name to a cstring.
 *
 */
static char *
X509_NAME_to_cstring(X509_NAME *name)
{
	BIO		   *membuf = BIO_new(BIO_s_mem());
	int			i,
				nid,
				count = X509_NAME_entry_count(name);
	X509_NAME_ENTRY *e;
	ASN1_STRING *v;
	const char *field_name;
	size_t		size;
	char		nullterm;
	char	   *sp;
	char	   *dp;
	char	   *result;

	(void) BIO_set_close(membuf, BIO_CLOSE);
	for (i = 0; i < count; i++)
	{
		e = X509_NAME_get_entry(name, i);
		nid = OBJ_obj2nid(X509_NAME_ENTRY_get_object(e));
		v = X509_NAME_ENTRY_get_data(e);
		field_name = OBJ_nid2sn(nid);
		if (!field_name)
			field_name = OBJ_nid2ln(nid);
		BIO_printf(membuf, "/%s=", field_name);
		ASN1_STRING_print_ex(membuf, v,
							 ((ASN1_STRFLGS_RFC2253 & ~ASN1_STRFLGS_ESC_MSB)
							  | ASN1_STRFLGS_UTF8_CONVERT));
	}

	/* ensure null termination of the BIO's content */
	nullterm = '\0';
	BIO_write(membuf, &nullterm, 1);
	size = BIO_get_mem_data(membuf, &sp);
	dp = pg_any_to_server(sp, size - 1, PG_UTF8);

	result = pstrdup(dp);
	if (dp != sp)
		pfree(dp);
	BIO_free(membuf);

	return result;
}

/*
 * Convert TLS protocol version GUC enum to OpenSSL values
 *
 * This is a straightforward one-to-one mapping, but doing it this way makes
 * guc.c independent of OpenSSL availability and version.
 *
 * If a version is passed that is not supported by the current OpenSSL
 * version, then we log with the given loglevel and return (if we return) -1.
 * If a nonnegative value is returned, subsequent code can assume it's working
 * with a supported version.
 */
static int
ssl_protocol_version_to_openssl(int v, const char *guc_name, int loglevel)
{
	switch (v)
	{
		case PG_TLS_ANY:
			return 0;
		case PG_TLS1_VERSION:
			return TLS1_VERSION;
		case PG_TLS1_1_VERSION:
#ifdef TLS1_1_VERSION
			return TLS1_1_VERSION;
#else
			goto error;
#endif
		case PG_TLS1_2_VERSION:
#ifdef TLS1_2_VERSION
			return TLS1_2_VERSION;
#else
			goto error;
#endif
		case PG_TLS1_3_VERSION:
#ifdef TLS1_3_VERSION
			return TLS1_3_VERSION;
#else
			goto error;
#endif
	}

error:
	pg_attribute_unused();
	ereport(loglevel,
			(errmsg("%s setting %s not supported by this build",
					guc_name,
					GetConfigOption(guc_name, false, false))));
	return -1;
}

/*
 * Replacements for APIs present in newer versions of OpenSSL
 */
#ifndef SSL_CTX_set_min_proto_version

/*
 * OpenSSL versions that support TLS 1.3 shouldn't get here because they
 * already have these functions.  So we don't have to keep updating the below
 * code for every new TLS version, and eventually it can go away.  But let's
 * just check this to make sure ...
 */
#ifdef TLS1_3_VERSION
#error OpenSSL version mismatch
#endif

static int
SSL_CTX_set_min_proto_version(SSL_CTX *ctx, int version)
{
	int			ssl_options = SSL_OP_NO_SSLv2 | SSL_OP_NO_SSLv3;

	if (version > TLS1_VERSION)
		ssl_options |= SSL_OP_NO_TLSv1;
	/*
	 * Some OpenSSL versions define TLS*_VERSION macros but not the
	 * corresponding SSL_OP_NO_* macro, so in those cases we have to return
	 * unsuccessfully here.
	 */
#ifdef TLS1_1_VERSION
	if (version > TLS1_1_VERSION)
	{
#ifdef SSL_OP_NO_TLSv1_1
		ssl_options |= SSL_OP_NO_TLSv1_1;
#else
		return 0;
#endif
	}
#endif
#ifdef TLS1_2_VERSION
	if (version > TLS1_2_VERSION)
	{
#ifdef SSL_OP_NO_TLSv1_2
		ssl_options |= SSL_OP_NO_TLSv1_2;
#else
		return 0;
#endif
	}
#endif

	SSL_CTX_set_options(ctx, ssl_options);

	return 1;					/* success */
}

static int
SSL_CTX_set_max_proto_version(SSL_CTX *ctx, int version)
{
	int			ssl_options = 0;

	AssertArg(version != 0);

	/*
	 * Some OpenSSL versions define TLS*_VERSION macros but not the
	 * corresponding SSL_OP_NO_* macro, so in those cases we have to return
	 * unsuccessfully here.
	 */
#ifdef TLS1_1_VERSION
	if (version < TLS1_1_VERSION)
	{
#ifdef SSL_OP_NO_TLSv1_1
		ssl_options |= SSL_OP_NO_TLSv1_1;
#else
		return 0;
#endif
	}
#endif
#ifdef TLS1_2_VERSION
	if (version < TLS1_2_VERSION)
	{
#ifdef SSL_OP_NO_TLSv1_2
		ssl_options |= SSL_OP_NO_TLSv1_2;
#else
		return 0;
#endif
	}
#endif

	SSL_CTX_set_options(ctx, ssl_options);

	return 1;					/* success */
}

#endif							/* !SSL_CTX_set_min_proto_version */
