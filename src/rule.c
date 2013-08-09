/*
 * (C) 2012-2013 by Pablo Neira Ayuso <pablo@netfilter.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published
 * by the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This code has been sponsored by Sophos Astaro <http://www.sophos.com>
 */
#include "internal.h"

#include <time.h>
#include <endian.h>
#include <stdint.h>
#include <stdlib.h>
#include <limits.h>
#include <string.h>
#include <netinet/in.h>
#include <errno.h>
#include <inttypes.h>

#include <libmnl/libmnl.h>
#include <linux/netfilter/nfnetlink.h>
#include <linux/netfilter/nf_tables.h>

#include <libnftables/rule.h>
#include <libnftables/expr.h>

#include "linux_list.h"
#include "expr_ops.h"

struct nft_rule {
	struct list_head head;

	uint32_t	flags;
	const char	*table;
	const char	*chain;
	uint8_t		family;
	uint32_t	rule_flags;
	uint64_t	handle;
	uint64_t	position;
	struct {
			uint32_t	flags;
			uint32_t	proto;
	} compat;

	struct list_head expr_list;
};

struct nft_rule *nft_rule_alloc(void)
{
	struct nft_rule *r;

	r = calloc(1, sizeof(struct nft_rule));
	if (r == NULL)
		return NULL;

	INIT_LIST_HEAD(&r->expr_list);

	return r;
}
EXPORT_SYMBOL(nft_rule_alloc);

void nft_rule_free(struct nft_rule *r)
{
	if (r->table != NULL)
		xfree(r->table);
	if (r->chain != NULL)
		xfree(r->chain);

	xfree(r);
}
EXPORT_SYMBOL(nft_rule_free);

bool nft_rule_attr_is_set(const struct nft_rule *r, uint16_t attr)
{
	return r->flags & (1 << attr);
}
EXPORT_SYMBOL(nft_rule_attr_is_set);

void nft_rule_attr_unset(struct nft_rule *r, uint16_t attr)
{
	if (!(r->flags & (1 << attr)))
		return;

	switch (attr) {
	case NFT_RULE_ATTR_TABLE:
		if (r->table) {
			xfree(r->table);
			r->table = NULL;
		}
		break;
	case NFT_RULE_ATTR_CHAIN:
		if (r->chain) {
			xfree(r->chain);
			r->chain = NULL;
		}
		break;
	case NFT_RULE_ATTR_HANDLE:
	case NFT_RULE_ATTR_FLAGS:
	case NFT_RULE_ATTR_COMPAT_PROTO:
	case NFT_RULE_ATTR_COMPAT_FLAGS:
	case NFT_RULE_ATTR_POSITION:
	case NFT_RULE_ATTR_FAMILY:
		break;
	}

	r->flags &= ~(1 << attr);
}
EXPORT_SYMBOL(nft_rule_attr_unset);

void nft_rule_attr_set(struct nft_rule *r, uint16_t attr, const void *data)
{
	switch(attr) {
	case NFT_RULE_ATTR_TABLE:
		if (r->table)
			xfree(r->table);

		r->table = strdup(data);
		break;
	case NFT_RULE_ATTR_CHAIN:
		if (r->chain)
			xfree(r->chain);

		r->chain = strdup(data);
		break;
	case NFT_RULE_ATTR_HANDLE:
		r->handle = *((uint64_t *)data);
		break;
	case NFT_RULE_ATTR_FLAGS:
		r->rule_flags = *((uint32_t *)data);
		break;
	case NFT_RULE_ATTR_COMPAT_PROTO:
		r->compat.proto = *((uint32_t *)data);
		break;
	case NFT_RULE_ATTR_COMPAT_FLAGS:
		r->compat.flags = *((uint32_t *)data);
		break;
	case NFT_RULE_ATTR_FAMILY:
		r->family = *((uint8_t *)data);
		break;
	case NFT_RULE_ATTR_POSITION:
		r->position = *((uint64_t *)data);
		break;
	default:
		return;
	}
	r->flags |= (1 << attr);
}
EXPORT_SYMBOL(nft_rule_attr_set);

void nft_rule_attr_set_u32(struct nft_rule *r, uint16_t attr, uint32_t val)
{
	nft_rule_attr_set(r, attr, &val);
}
EXPORT_SYMBOL(nft_rule_attr_set_u32);

void nft_rule_attr_set_u64(struct nft_rule *r, uint16_t attr, uint64_t val)
{
	nft_rule_attr_set(r, attr, &val);
}
EXPORT_SYMBOL(nft_rule_attr_set_u64);

void nft_rule_attr_set_str(struct nft_rule *r, uint16_t attr, const char *str)
{
	nft_rule_attr_set(r, attr, str);
}
EXPORT_SYMBOL(nft_rule_attr_set_str);

const void *nft_rule_attr_get(const struct nft_rule *r, uint16_t attr)
{
	if (!(r->flags & (1 << attr)))
		return NULL;

	switch(attr) {
	case NFT_RULE_ATTR_FAMILY:
		return &r->family;
	case NFT_RULE_ATTR_TABLE:
		return r->table;
	case NFT_RULE_ATTR_CHAIN:
		return r->chain;
	case NFT_RULE_ATTR_HANDLE:
		return &r->handle;
	case NFT_RULE_ATTR_FLAGS:
		return &r->rule_flags;
	case NFT_RULE_ATTR_COMPAT_PROTO:
		return &r->compat.proto;
	case NFT_RULE_ATTR_COMPAT_FLAGS:
		return &r->compat.flags;
	case NFT_RULE_ATTR_POSITION:
		return &r->position;
	}
	return NULL;
}
EXPORT_SYMBOL(nft_rule_attr_get);

const char *nft_rule_attr_get_str(const struct nft_rule *r, uint16_t attr)
{
	return nft_rule_attr_get(r, attr);
}
EXPORT_SYMBOL(nft_rule_attr_get_str);

uint32_t nft_rule_attr_get_u32(const struct nft_rule *r, uint16_t attr)
{
	uint32_t val = *((uint32_t *)nft_rule_attr_get(r, attr));
	return val;
}
EXPORT_SYMBOL(nft_rule_attr_get_u32);

uint64_t nft_rule_attr_get_u64(const struct nft_rule *r, uint16_t attr)
{
	uint64_t val = *((uint64_t *)nft_rule_attr_get(r, attr));
	return val;
}
EXPORT_SYMBOL(nft_rule_attr_get_u64);

uint8_t nft_rule_attr_get_u8(const struct nft_rule *r, uint16_t attr)
{
	uint8_t val = *((uint8_t *)nft_rule_attr_get(r, attr));
	return val;
}
EXPORT_SYMBOL(nft_rule_attr_get_u8);

struct nlmsghdr *
nft_rule_nlmsg_build_hdr(char *buf, uint16_t cmd, uint16_t family,
			  uint16_t type, uint32_t seq)
{
	struct nlmsghdr *nlh;
	struct nfgenmsg *nfh;

	nlh = mnl_nlmsg_put_header(buf);
	nlh->nlmsg_type = (NFNL_SUBSYS_NFTABLES << 8) | cmd;
	nlh->nlmsg_flags = NLM_F_REQUEST | type;
	nlh->nlmsg_seq = seq;

	nfh = mnl_nlmsg_put_extra_header(nlh, sizeof(struct nfgenmsg));
	nfh->nfgen_family = family;
	nfh->version = NFNETLINK_V0;
	nfh->res_id = 0;

	return nlh;
}
EXPORT_SYMBOL(nft_rule_nlmsg_build_hdr);

void nft_rule_nlmsg_build_payload(struct nlmsghdr *nlh, struct nft_rule *r)
{
	struct nft_rule_expr *expr;
	struct nlattr *nest;

	if (r->flags & (1 << NFT_RULE_ATTR_TABLE))
		mnl_attr_put_strz(nlh, NFTA_RULE_TABLE, r->table);
	if (r->flags & (1 << NFT_RULE_ATTR_CHAIN))
		mnl_attr_put_strz(nlh, NFTA_RULE_CHAIN, r->chain);
	if (r->flags & (1 << NFT_RULE_ATTR_HANDLE))
		mnl_attr_put_u64(nlh, NFTA_RULE_HANDLE, htobe64(r->handle));
	if (r->flags & (1 << NFT_RULE_ATTR_POSITION))
		mnl_attr_put_u64(nlh, NFTA_RULE_POSITION, htobe64(r->position));
	if (r->flags & (1 << NFT_RULE_ATTR_FLAGS))
		mnl_attr_put_u32(nlh, NFTA_RULE_FLAGS, htonl(r->rule_flags));

	nest = mnl_attr_nest_start(nlh, NFTA_RULE_EXPRESSIONS);
	list_for_each_entry(expr, &r->expr_list, head) {
		nft_rule_expr_build_payload(nlh, expr);
	}
	mnl_attr_nest_end(nlh, nest);

	if (r->flags & (1 << NFT_RULE_ATTR_COMPAT_PROTO) &&
	    r->flags & (1 << NFT_RULE_ATTR_COMPAT_FLAGS)) {

		nest = mnl_attr_nest_start(nlh, NFTA_RULE_COMPAT);
		mnl_attr_put_u32(nlh, NFTA_RULE_COMPAT_PROTO,
				 htonl(r->compat.proto));
		mnl_attr_put_u32(nlh, NFTA_RULE_COMPAT_FLAGS,
				 htonl(r->compat.flags));
		mnl_attr_nest_end(nlh, nest);
	}
}
EXPORT_SYMBOL(nft_rule_nlmsg_build_payload);

void nft_rule_add_expr(struct nft_rule *r, struct nft_rule_expr *expr)
{
	list_add_tail(&expr->head, &r->expr_list);
}
EXPORT_SYMBOL(nft_rule_add_expr);

static int nft_rule_parse_attr_cb(const struct nlattr *attr, void *data)
{
	const struct nlattr **tb = data;
	int type = mnl_attr_get_type(attr);

	if (mnl_attr_type_valid(attr, NFTA_RULE_MAX) < 0)
		return MNL_CB_OK;

	switch(type) {
	case NFTA_RULE_TABLE:
	case NFTA_RULE_CHAIN:
		if (mnl_attr_validate(attr, MNL_TYPE_STRING) < 0) {
			perror("mnl_attr_validate");
			return MNL_CB_ERROR;
		}
		break;
	case NFTA_RULE_HANDLE:
		if (mnl_attr_validate(attr, MNL_TYPE_U64) < 0) {
			perror("mnl_attr_validate");
			return MNL_CB_ERROR;
		}
		break;
	case NFTA_RULE_FLAGS:
		if (mnl_attr_validate(attr, MNL_TYPE_U32) < 0) {
			perror("mnl_attr_validate");
			return MNL_CB_ERROR;
		}
		break;
	case NFTA_RULE_COMPAT:
		if (mnl_attr_validate(attr, MNL_TYPE_NESTED) < 0) {
			perror("mnl_attr_validate");
			return MNL_CB_ERROR;
		}
		break;
	case NFTA_RULE_POSITION:
		if (mnl_attr_validate(attr, MNL_TYPE_U64) < 0) {
			perror("mnl_attr_validate");
			return MNL_CB_ERROR;
		}
		break;
	}

	tb[type] = attr;
	return MNL_CB_OK;
}

static int nft_rule_parse_expr_cb(const struct nlattr *attr, void *data)
{
	const struct nlattr **tb = data;
	int type = mnl_attr_get_type(attr);

	if (mnl_attr_type_valid(attr, NFTA_EXPR_MAX) < 0)
		return MNL_CB_OK;

	switch(type) {
	case NFTA_EXPR_NAME:
		if (mnl_attr_validate(attr, MNL_TYPE_STRING) < 0) {
			perror("mnl_attr_validate");
			return MNL_CB_ERROR;
		}
		break;
	case NFTA_EXPR_DATA:
		if (mnl_attr_validate(attr, MNL_TYPE_NESTED) < 0) {
			perror("mnl_attr_validate");
			return MNL_CB_ERROR;
		}
		break;
	}

	tb[type] = attr;
	return MNL_CB_OK;
}

static int nft_rule_parse_expr2(struct nlattr *attr, struct nft_rule *r)
{
	struct nlattr *tb[NFTA_EXPR_MAX+1] = {};
	struct nft_rule_expr *expr;

	if (mnl_attr_parse_nested(attr, nft_rule_parse_expr_cb, tb) < 0)
		return -1;

	expr = nft_rule_expr_alloc(mnl_attr_get_str(tb[NFTA_EXPR_NAME]));
	if (expr == NULL)
		return -1;

	if (tb[NFTA_EXPR_DATA]) {
		if (expr->ops->parse(expr, tb[NFTA_EXPR_DATA]) < 0) {
			xfree(expr);
			return -1;
		}
	}
	list_add_tail(&expr->head, &r->expr_list);

	return 0;
}

static int nft_rule_parse_expr(struct nlattr *nest, struct nft_rule *r)
{
	struct nlattr *attr;

	mnl_attr_for_each_nested(attr, nest) {
		if (mnl_attr_get_type(attr) != NFTA_LIST_ELEM)
			return -1;

		nft_rule_parse_expr2(attr, r);
	}
	return 0;
}

static int nft_rule_parse_compat_cb(const struct nlattr *attr, void *data)
{
	const struct nlattr **tb = data;
	int type = mnl_attr_get_type(attr);

	if (mnl_attr_type_valid(attr, NFTA_RULE_COMPAT_MAX) < 0)
		return MNL_CB_OK;

	switch(type) {
	case NFTA_RULE_COMPAT_PROTO:
	case NFTA_RULE_COMPAT_FLAGS:
		if (mnl_attr_validate(attr, MNL_TYPE_U32) < 0) {
			perror("mnl_attr_validate");
			return MNL_CB_ERROR;
		}
		break;
	}

	tb[type] = attr;
	return MNL_CB_OK;
}

static int nft_rule_parse_compat(struct nlattr *nest, struct nft_rule *r)
{
	struct nlattr *tb[NFTA_RULE_COMPAT_MAX+1] = {};

	if (mnl_attr_parse_nested(nest, nft_rule_parse_compat_cb, tb) < 0)
		return -1;

	if (tb[NFTA_RULE_COMPAT_PROTO]) {
		r->compat.proto =
			ntohl(mnl_attr_get_u32(tb[NFTA_RULE_COMPAT_PROTO]));
		r->flags |= (1 << NFT_RULE_ATTR_COMPAT_PROTO);
	}
	if (tb[NFTA_RULE_COMPAT_FLAGS]) {
		r->compat.flags =
			ntohl(mnl_attr_get_u32(tb[NFTA_RULE_COMPAT_FLAGS]));
		r->flags |= (1 << NFT_RULE_ATTR_COMPAT_FLAGS);
	}
	return 0;
}

int nft_rule_nlmsg_parse(const struct nlmsghdr *nlh, struct nft_rule *r)
{
	struct nlattr *tb[NFTA_RULE_MAX+1] = {};
	struct nfgenmsg *nfg = mnl_nlmsg_get_payload(nlh);
	int ret = 0;

	mnl_attr_parse(nlh, sizeof(*nfg), nft_rule_parse_attr_cb, tb);
	if (tb[NFTA_RULE_TABLE]) {
		r->table = strdup(mnl_attr_get_str(tb[NFTA_RULE_TABLE]));
		r->flags |= (1 << NFT_RULE_ATTR_TABLE);
	}
	if (tb[NFTA_RULE_CHAIN]) {
		r->chain = strdup(mnl_attr_get_str(tb[NFTA_RULE_CHAIN]));
		r->flags |= (1 << NFT_RULE_ATTR_CHAIN);
	}
	if (tb[NFTA_RULE_HANDLE]) {
		r->handle = be64toh(mnl_attr_get_u64(tb[NFTA_RULE_HANDLE]));
		r->flags |= (1 << NFT_RULE_ATTR_HANDLE);
	}
	if (tb[NFTA_RULE_EXPRESSIONS])
		ret = nft_rule_parse_expr(tb[NFTA_RULE_EXPRESSIONS], r);
	if (tb[NFTA_RULE_COMPAT])
		ret = nft_rule_parse_compat(tb[NFTA_RULE_COMPAT], r);
	if (tb[NFTA_RULE_POSITION]) {
		r->position = be64toh(mnl_attr_get_u64(tb[NFTA_RULE_POSITION]));
		r->flags |= (1 << NFT_RULE_ATTR_POSITION);
	}

	r->family = nfg->nfgen_family;
	r->flags |= (1 << NFT_RULE_ATTR_FAMILY);

	return ret;
}
EXPORT_SYMBOL(nft_rule_nlmsg_parse);

static int nft_rule_xml_parse(struct nft_rule *r, char *xml)
{
#ifdef XML_PARSING
	mxml_node_t *tree = NULL;
	mxml_node_t *node = NULL;
	mxml_node_t *save = NULL;
	struct nft_rule_expr *e;
	const char *table;
	const char *chain;
	int family;

	/* Load the tree */
	tree = mxmlLoadString(NULL, xml, MXML_OPAQUE_CALLBACK);
	if (tree == NULL)
		return -1;

	if (strcmp(tree->value.opaque, "rule") != 0) {
		mxmlDelete(tree);
		return -1;
	}

	family = nft_mxml_family_parse(tree, "family", MXML_DESCEND_FIRST);
	if (family < 0) {
		mxmlDelete(tree);
		return -1;
	}

	r->family = family;
	r->flags |= (1 << NFT_RULE_ATTR_FAMILY);

	table = nft_mxml_str_parse(tree, "table", MXML_DESCEND_FIRST);
	if (table == NULL) {
		mxmlDelete(tree);
		return -1;
	}

	if (r->table)
		xfree(r->table);

	r->table = table;
	r->flags |= (1 << NFT_RULE_ATTR_TABLE);

	chain = nft_mxml_str_parse(tree, "chain", MXML_DESCEND_FIRST);
	if (chain == NULL) {
		mxmlDelete(tree);
		return -1;
	}

	if (r->chain)
		xfree(r->chain);

	r->chain = chain;
	r->flags |= (1 << NFT_RULE_ATTR_CHAIN);

	if (nft_mxml_num_parse(tree, "handle", MXML_DESCEND_FIRST, BASE_DEC,
			       &r->handle, NFT_TYPE_U64) != 0) {
		mxmlDelete(tree);
		return -1;
	}

	r->flags |= (1 << NFT_RULE_ATTR_HANDLE);

	/* get and set <rule_flags> */
	if (nft_mxml_num_parse(tree, "flags", MXML_DESCEND_FIRST,
			       BASE_DEC, &r->rule_flags, NFT_TYPE_U32) != 0) {
		mxmlDelete(tree);
		return -1;
	}

	r->flags |= (1 << NFT_RULE_ATTR_FLAGS);

	/* <compat_proto> is optional */
	node = mxmlFindElement(tree, tree, "compat_proto", NULL, NULL,
			       MXML_DESCEND);
	if (node != NULL && node->child != NULL) {
		if (nft_strtoi(node->child->value.opaque, BASE_DEC,
			       &r->compat.proto, NFT_TYPE_U32) != 0) {
			mxmlDelete(tree);
			return -1;
		}

		r->flags |= (1 << NFT_RULE_ATTR_COMPAT_PROTO);
	}

	/* <compat_flags> is optional */
	node = mxmlFindElement(tree, tree, "compat_flags", NULL, NULL,
			       MXML_DESCEND);
	if (node != NULL && node->child != NULL) {
		if (nft_strtoi(node->child->value.opaque, BASE_DEC,
			       &r->compat.flags, NFT_TYPE_U32) != 0) {
			mxmlDelete(tree);
			return -1;
		}

		r->flags |= (1 << NFT_RULE_ATTR_COMPAT_FLAGS);
	}

	/* Iterating over <expr> */
	for (node = mxmlFindElement(tree, tree, "expr", "type",
				    NULL, MXML_DESCEND);
		node != NULL;
		node = mxmlFindElement(node, tree, "expr", "type",
				       NULL, MXML_DESCEND)) {

		/* This is a hack for mxml to print just the current node */
		save = node->next;
		node->next = NULL;

		e = nft_mxml_expr_parse(node);
		if (e == NULL) {
			mxmlDelete(tree);
			return -1;
		}

		nft_rule_add_expr(r, e);

		node->next = save;
		save = NULL;
	}

	mxmlDelete(tree);
	return 0;
#else
	errno = EOPNOTSUPP;
	return -1;
#endif
}

int nft_rule_parse(struct nft_rule *r, enum nft_rule_parse_type type, char *data)
{
	int ret;

	switch (type) {
	case NFT_RULE_PARSE_XML:
		ret = nft_rule_xml_parse(r, data);
		break;
	default:
		ret = -1;
		errno = EOPNOTSUPP;
		break;
	}

	return ret;
}
EXPORT_SYMBOL(nft_rule_parse);

static int nft_rule_snprintf_json(char *buf, size_t size, struct nft_rule *r,
					 uint32_t type, uint32_t flags)
{
	int ret, len = size, offset = 0;
	struct nft_rule_expr *expr;

	ret = snprintf(buf, size,
				"{ \"rule\": { \"family\" : \"%s\", \"table\" : \"%s\", "
				"\"chain\"  : \"%s\", \"handle\" : %llu,",
				nft_family2str(r->family), r->table, r->chain,
				(unsigned long long)r->handle);
	SNPRINTF_BUFFER_SIZE(ret, size, len, offset);

	ret = snprintf(buf+offset, len, "\"flags\" : %u, ", r->rule_flags);
	SNPRINTF_BUFFER_SIZE(ret, size, len, offset);

	if (r->flags & (1 << NFT_RULE_ATTR_COMPAT_PROTO) ||
	    r->flags & (1 << NFT_RULE_ATTR_COMPAT_FLAGS)) {
		ret = snprintf(buf+offset, len, "\"compat_flags\" : %u, "
					        "\"compat_proto\" : %u, ",
			       r->compat.flags, r->compat.proto);
		SNPRINTF_BUFFER_SIZE(ret, size, len, offset);
	}

	ret = snprintf(buf+offset, len, "\"expr\" : [");
		SNPRINTF_BUFFER_SIZE(ret, size, len, offset);

	list_for_each_entry(expr, &r->expr_list, head) {
		ret = snprintf(buf+offset, len,
				" { \"type\" : \"%s\", ", expr->ops->name);
		SNPRINTF_BUFFER_SIZE(ret, size, len, offset);

		ret = expr->ops->snprintf(buf+offset, len, type, flags, expr);
		SNPRINTF_BUFFER_SIZE(ret, size, len, offset);

		ret = snprintf(buf+offset, len, "},");
		SNPRINTF_BUFFER_SIZE(ret, size, len, offset);

	}
	ret = snprintf(buf+offset-1, len, "]}}");
		SNPRINTF_BUFFER_SIZE(ret, size, len, offset);

	return offset;
}

static int nft_rule_snprintf_xml(char *buf, size_t size, struct nft_rule *r,
				 uint32_t type, uint32_t flags)
{
	int ret, len = size, offset = 0;
	struct nft_rule_expr *expr;

	ret = snprintf(buf, size, "<rule><family>%s</family>"
		       "<table>%s</table><chain>%s</chain>"
		       "<handle>%llu</handle><flags>%u</flags>",
		       nft_family2str(r->family), r->table, r->chain,
		       (unsigned long long)r->handle, r->rule_flags);
	SNPRINTF_BUFFER_SIZE(ret, size, len, offset);

	if (r->compat.flags != 0 || r->compat.proto != 0) {
		ret = snprintf(buf+offset, len,
			       "<compat_flags>%u</compat_flags>"
			       "<compat_proto>%u</compat_proto>",
			       r->compat.flags, r->compat.proto);
		SNPRINTF_BUFFER_SIZE(ret, size, len, offset);
	}

	list_for_each_entry(expr, &r->expr_list, head) {
		ret = snprintf(buf+offset, len,
				"<expr type=\"%s\">", expr->ops->name);
		SNPRINTF_BUFFER_SIZE(ret, size, len, offset);

		ret = nft_rule_expr_snprintf(buf+offset, size, expr, type, flags);
		SNPRINTF_BUFFER_SIZE(ret, size, len, offset);

		ret = snprintf(buf+offset, len, "</expr>");
		SNPRINTF_BUFFER_SIZE(ret, size, len, offset);

	}
	ret = snprintf(buf+offset, len, "</rule>");
	SNPRINTF_BUFFER_SIZE(ret, size, len, offset);

	return offset;
}

static int nft_rule_snprintf_default(char *buf, size_t size, struct nft_rule *r, 
				     uint32_t type, uint32_t flags)
{
	struct nft_rule_expr *expr;
	int ret, len = size, offset = 0;

	ret = snprintf(buf, size, "%s %s %s %"PRIu64" %"PRIu64"\n",
			nft_family2str(r->family), r->table, r->chain,
			r->handle, r->position);
	SNPRINTF_BUFFER_SIZE(ret, size, len, offset);

	list_for_each_entry(expr, &r->expr_list, head) {
		ret = snprintf(buf+offset, len, "  [ %s ", expr->ops->name);
		SNPRINTF_BUFFER_SIZE(ret, size, len, offset);

		ret = nft_rule_expr_snprintf(buf+offset, size, expr, type, flags);
		SNPRINTF_BUFFER_SIZE(ret, size, len, offset);

		ret = snprintf(buf+offset, len, "]\n");
		SNPRINTF_BUFFER_SIZE(ret, size, len, offset);
	}

	return offset;
}

int nft_rule_snprintf(char *buf, size_t size, struct nft_rule *r,
		       uint32_t type, uint32_t flags)
{
	switch(type) {
	case NFT_RULE_O_DEFAULT:
		return nft_rule_snprintf_default(buf, size, r, type, flags);
	case NFT_RULE_O_XML:
		return nft_rule_snprintf_xml(buf, size, r, type, flags);
	case NFT_RULE_O_JSON:
		return nft_rule_snprintf_json(buf, size, r, type, flags);
	default:
		break;
	}
	return -1;
}
EXPORT_SYMBOL(nft_rule_snprintf);

int nft_rule_expr_foreach(struct nft_rule *r,
                          int (*cb)(struct nft_rule_expr *e, void *data),
                          void *data)
{
       struct nft_rule_expr *cur, *tmp;
       int ret;

       list_for_each_entry_safe(cur, tmp, &r->expr_list, head) {
               ret = cb(cur, data);
               if (ret < 0)
                       return ret;
       }
       return 0;
}
EXPORT_SYMBOL(nft_rule_expr_foreach);

struct nft_rule_expr_iter {
	struct nft_rule		*r;
	struct nft_rule_expr	*cur;
};

struct nft_rule_expr_iter *nft_rule_expr_iter_create(struct nft_rule *r)
{
	struct nft_rule_expr_iter *iter;

	iter = calloc(1, sizeof(struct nft_rule_expr_iter));
	if (iter == NULL)
		return NULL;

	iter->r = r;
	iter->cur = list_entry(r->expr_list.next, struct nft_rule_expr, head);

	return iter;
}
EXPORT_SYMBOL(nft_rule_expr_iter_create);

struct nft_rule_expr *nft_rule_expr_iter_next(struct nft_rule_expr_iter *iter)
{
	struct nft_rule_expr *expr = iter->cur;

	/* get next expression, if any */
	iter->cur = list_entry(iter->cur->head.next, struct nft_rule_expr, head);
	if (&iter->cur->head == iter->r->expr_list.next)
		return NULL;

	return expr;
}
EXPORT_SYMBOL(nft_rule_expr_iter_next);

void nft_rule_expr_iter_destroy(struct nft_rule_expr_iter *iter)
{
	xfree(iter);
}
EXPORT_SYMBOL(nft_rule_expr_iter_destroy);

struct nft_rule_list {
	struct list_head list;
};

struct nft_rule_list *nft_rule_list_alloc(void)
{
	struct nft_rule_list *list;

	list = calloc(1, sizeof(struct nft_rule_list));
	if (list == NULL)
		return NULL;

	INIT_LIST_HEAD(&list->list);

	return list;
}
EXPORT_SYMBOL(nft_rule_list_alloc);

void nft_rule_list_free(struct nft_rule_list *list)
{
	struct nft_rule *r, *tmp;

	list_for_each_entry_safe(r, tmp, &list->list, head) {
		list_del(&r->head);
		nft_rule_free(r);
	}
	xfree(list);
}
EXPORT_SYMBOL(nft_rule_list_free);

int nft_rule_list_is_empty(struct nft_rule_list *list)
{
	return list_empty(&list->list);
}
EXPORT_SYMBOL(nft_rule_list_is_empty);

void nft_rule_list_add(struct nft_rule *r, struct nft_rule_list *list)
{
	list_add(&r->head, &list->list);
}
EXPORT_SYMBOL(nft_rule_list_add);

void nft_rule_list_add_tail(struct nft_rule *r, struct nft_rule_list *list)
{
	list_add_tail(&r->head, &list->list);
}
EXPORT_SYMBOL(nft_rule_list_add_tail);

int nft_rule_list_foreach(struct nft_rule_list *rule_list,
			  int (*cb)(struct nft_rule *r, void *data),
			  void *data)
{
	struct nft_rule *cur, *tmp;
	int ret;

	list_for_each_entry_safe(cur, tmp, &rule_list->list, head) {
		ret = cb(cur, data);
		if (ret < 0)
			return ret;
	}
	return 0;
}
EXPORT_SYMBOL(nft_rule_list_foreach);

struct nft_rule_list_iter {
	struct nft_rule_list	*list;
	struct nft_rule		*cur;
};

struct nft_rule_list_iter *nft_rule_list_iter_create(struct nft_rule_list *l)
{
	struct nft_rule_list_iter *iter;

	iter = calloc(1, sizeof(struct nft_rule_list_iter));
	if (iter == NULL)
		return NULL;

	iter->list = l;
	iter->cur = list_entry(l->list.next, struct nft_rule, head);

	return iter;
}
EXPORT_SYMBOL(nft_rule_list_iter_create);

struct nft_rule *nft_rule_list_iter_cur(struct nft_rule_list_iter *iter)
{
	return iter->cur;
}
EXPORT_SYMBOL(nft_rule_list_iter_cur);

struct nft_rule *nft_rule_list_iter_next(struct nft_rule_list_iter *iter)
{
	struct nft_rule *r = iter->cur;

	/* get next rule, if any */
	iter->cur = list_entry(iter->cur->head.next, struct nft_rule, head);
	if (&iter->cur->head == iter->list->list.next)
		return NULL;

	return r;
}
EXPORT_SYMBOL(nft_rule_list_iter_next);

void nft_rule_list_iter_destroy(struct nft_rule_list_iter *iter)
{
	xfree(iter);
}
EXPORT_SYMBOL(nft_rule_list_iter_destroy);
