/*
 * (C) 2013 by Pablo Neira Ayuso <pablo@netfilter.org>
 * (C) 2014 by Arturo Borrero Gonzalez <arturo.borrero.glez@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This code has been sponsored by Sophos Astaro <http://www.sophos.com>
 */

#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>

#include <linux/netfilter.h>
#include <linux/netfilter/nf_tables.h>

#include <libmnl/libmnl.h>
#include <libnftnl/chain.h>
#include <libnftnl/rule.h>

static struct nft_chain *chain_parse_file(const char *file, uint16_t format)
{
	int fd;
	struct nft_chain *c;
	struct nft_parse_err *err;
	char data[4096];

	c = nft_chain_alloc();
	if (c == NULL) {
		perror("OOM");
		return NULL;
	}

	fd = open(file, O_RDONLY);
	if (fd < 0) {
		perror("open");
		return NULL;
	}

	if (read(fd, data, sizeof(data)) < 0) {
		perror("read");
		close(fd);
		return NULL;
	}

	close(fd);

	err = nft_parse_err_alloc();
	if (err == NULL) {
		perror("OOM");
		return NULL;
	}

	if (nft_chain_parse(c, format, data, err) < 0) {
		nft_parse_perror("Unable to parse file", err);
		nft_parse_err_free(err);
		return NULL;
	}

	nft_parse_err_free(err);
	return c;
}

int main(int argc, char *argv[])
{
	struct mnl_socket *nl;
	char buf[MNL_SOCKET_BUFFER_SIZE];
	struct nlmsghdr *nlh;
	uint32_t portid, seq;
	struct nft_chain *c;
	uint16_t family, format, outformat;
	int ret;

	if (argc < 3) {
		printf("Usage: %s {xml|json} <file>\n", argv[0]);
		exit(EXIT_FAILURE);
	}

	if (strcmp(argv[1], "xml") == 0) {
		format = NFT_PARSE_XML;
		outformat = NFT_OUTPUT_XML;
	} else if (strcmp(argv[1], "json") == 0) {
		format = NFT_PARSE_JSON;
		outformat = NFT_OUTPUT_JSON;
	} else {
		printf("Unknown format: xml, json\n");
		exit(EXIT_FAILURE);
	}

	c = chain_parse_file(argv[2], format);
	if (c == NULL)
		exit(EXIT_FAILURE);

	nft_chain_fprintf(stdout, c, outformat, 0);
	fprintf(stdout, "\n");

	nft_chain_attr_unset(c, NFT_CHAIN_ATTR_HANDLE);
	family = nft_chain_attr_get_u32(c, NFT_CHAIN_ATTR_FAMILY);

	seq = time(NULL);
	nlh = nft_chain_nlmsg_build_hdr(buf, NFT_MSG_NEWCHAIN, family,
					NLM_F_CREATE|NLM_F_ACK, seq);
	nft_chain_nlmsg_build_payload(nlh, c);

	nft_chain_free(c);

	nl = mnl_socket_open(NETLINK_NETFILTER);
	if (nl == NULL) {
		perror("mnl_socket_open");
		exit(EXIT_FAILURE);
	}

	if (mnl_socket_bind(nl, 0, MNL_SOCKET_AUTOPID) < 0) {
		perror("mnl_socket_bind");
		exit(EXIT_FAILURE);
	}

	portid = mnl_socket_get_portid(nl);

	if (mnl_socket_sendto(nl, nlh, nlh->nlmsg_len) < 0) {
		perror("mnl_socket_send");
		exit(EXIT_FAILURE);
	}

	ret = mnl_socket_recvfrom(nl, buf, sizeof(buf));
	while (ret > 0) {
		ret = mnl_cb_run(buf, ret, seq, portid, NULL, NULL);
		if (ret <= 0)
			break;
		ret = mnl_socket_recvfrom(nl, buf, sizeof(buf));

	}
	if (ret == -1) {
		perror("error");
		exit(EXIT_FAILURE);
	}


	mnl_socket_close(nl);
	return EXIT_SUCCESS;
}