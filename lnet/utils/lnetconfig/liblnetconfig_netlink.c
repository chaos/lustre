/*
 * LGPL HEADER START
 *
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the GNU Lesser General Public License
 * LGPL version 2.1 or (at your discretion) any later version.
 * LGPL version 2.1 accompanies this distribution, and is available at
 * http://www.gnu.org/licenses/lgpl-2.1.html
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * LGPL HEADER END
 */
/**
 * Netlink handling.
 *
 * Copyright (c) 2021  UT-Battelle, LLC
 *
 * Author: James Simmons <jsimmons@infradead.org>
 */

#include <ctype.h>
#include <unistd.h>
#include <fcntl.h>
#include <yaml.h>

#include <linux/lnet/lnet-nl.h>
#include "liblnetconfig.h"

#ifndef SOL_NETLINK /* for glibc < 2.24 */
# define SOL_NETLINK 270
#endif

#ifndef NETLINK_EXT_ACK
#define NETLINK_EXT_ACK	11
#endif

#ifndef NLM_F_ACK_TLVS
#define NLM_F_ACK_TLVS	0x200	/* extended ACK TVLs were included */
#endif

#ifndef NLA_NUL_STRING
# define NLA_NUL_STRING 10
#endif

#ifndef NLA_S16
# define NLA_S16 13
#endif

#ifndef HAVE_NLA_GET_S32

#define NLA_S32	14

/**
 * Return payload of 32 bit signed integer attribute.
 *
 * @arg nla		32 bit integer attribute.
 *
 * @return Payload as 32 bit integer.
 */
int32_t nla_get_s32(const struct nlattr *nla)
{
	return *(const int32_t *) nla_data(nla);
}
#endif /* ! HAVE_NLA_GET_S32 */

#ifndef HAVE_NLA_GET_S64

#define NLA_S64	15

/**
 * Return payload of s64 attribute
 *
 * @arg nla	s64 netlink attribute
 *
 * @return Payload as 64 bit integer.
 */
int64_t nla_get_s64(const struct nlattr *nla)
{
	int64_t tmp = 0;

	if (nla && nla_len(nla) >= sizeof(tmp))
		memcpy(&tmp, nla_data(nla), sizeof(tmp));

	return tmp;
}
#endif /* ! HAVE_NLA_GET_S64 */

/**
 * Set NETLINK_BROADCAST_ERROR flags on socket to report ENOBUFS errors.
 *
 * @sk		Socket to change the flags.
 *
 * Return	0 on success or a Netlink error code.
 */
int nl_socket_enable_broadcast_error(struct nl_sock *sk)
{
	const int state = 1; /* enable errors */
	int err;

	if (nl_socket_get_fd(sk) < 0)
		return -NLE_BAD_SOCK;

	err = setsockopt(nl_socket_get_fd(sk), SOL_NETLINK,
			 NETLINK_BROADCAST_ERROR, &state, sizeof(state));
	if (err < 0)
		return -nl_syserr2nlerr(errno);

	return 0;
}

/**
 * Enable/disable extending ACK for netlink socket. Used for
 * sending extra debugging information.
 *
 * @arg sk              Netlink socket.
 * @arg state           New state (0 - disabled, 1 - enabled)
 *
 * @return 0 on success or a negative error code
 */
int nl_socket_set_ext_ack(struct nl_sock *sk, int state)
{
	int err;

	if (nl_socket_get_fd(sk) < 0)
		return -NLE_BAD_SOCK;

	err = setsockopt(nl_socket_get_fd(sk), SOL_NETLINK,
			 NETLINK_EXT_ACK, &state, sizeof(state));
	if (err < 0 && errno != ENOPROTOOPT)
		return -nl_syserr2nlerr(errno);

	return 0;
}

/**
 * Create a Netlink socket
 *
 * @sk			The nl_sock which we used to handle the Netlink
 *			connection.
 * @async_events	tell the Netlink socket this will receive asynchronous
 *			data
 *
 * Return		0 on success or a negative error code.
 */
int lustre_netlink_register(struct nl_sock *sk, bool async_events)
{
	int rc;

	rc = genl_connect(sk);
	if (rc < 0)
		return rc;

	rc = nl_socket_enable_broadcast_error(sk);
	if (rc < 0)
		return rc;

	rc = nl_socket_set_ext_ack(sk, true);
	if (rc < 0)
		return rc;

	if (async_events) {
		/* Required to receive async netlink event notifications */
		nl_socket_disable_seq_check(sk);
		/* Don't need ACK for events generated by kernel */
		nl_socket_disable_auto_ack(sk);
	}

	return rc;
}

/**
 * Filter Netlink socket by groups
 *
 * @nl		Netlink socket
 * @family	The family name of the Netlink socket.
 * @group	Netlink messages will only been sent if they belong to this
 *		group
 *
 * Return	0 on success or a negative error code.
 */
int lustre_netlink_add_group(struct nl_sock *nl, const char *family,
			     const char *group)
{
	int group_id;

	/* Get group ID */
	group_id = genl_ctrl_resolve_grp(nl, family, group);
	if (group_id < 0)
		return group_id;

	/* subscribe to generic netlink multicast group */
	return nl_socket_add_membership(nl, group_id);
}

/* A YAML file is used to describe data. In a YAML document the content is
 * all about a collection of scalars used to create new data types such as
 * key-value pairs. This allows complex documents to represent anything from
 * a string to a tree.
 *
 * Scalar:
 * ---------
 * YAML scalars are a simple value which can be a string, number or Boolean.
 * They are the simplest data types. They can exist in a YAML document but
 * are typically used to build more complex data formats.
 *
 * Collections:
 * ------------
 * In YAML collections are scalar elements presented in the form of
 * an array, called a sequence, or mappings (hashes) that are scalar
 * key value pairs. All elements belonging to the same collection are
 * the lines that begin at the same indentation level
 *
 * Sequences use a dash followed by a space.
 * Mappings use a colon followed by a space (: ) to mark each key/value pair:
 *
 * Collections can be represented in two forms, flow and block.
 * Note they are equivalent. Example of block sequence is;
 *
 * - string
 * - integer
 * - boolean
 *
 * and a block mapping example is:
 *
 * string: hello
 * integer: 5
 * boolean: False
 *
 * YAML flow styles for collections uses explicit indicators rather than
 * indentation to denote scope.
 *
 * A sequence can be written as a comma separated list within
 * square brackets ([]):
 *
 * [ PHP, Perl, Python ]
 *
 * A mapping can be written as a comma separated list of key/values within
 * curly braces ({}):
 *
 * { PHP: 5.2, MySQL: 5.1, Apache: 2.2.20 }
 *
 * NOTE!! flow and block are equivalent.
 *
 * List:
 * ------
 * A list is a defined array of data which can be either an flow or block
 * sequence. Lists can be nested. Example
 *
 * numbers: [ 1, 2, 3, 4 ]
 *
 * numbers:
 *  - 1
 *  - 2
 *  - 3
 *  - 4
 *
 * Dictionaries:
 * --------------
 * Are comprised of a key: value format with contents indented. This is
 * built on top of the flow or block mapping. Like lists they can be nested.
 *
 * ports:
 * - port: 8080
 *     targetPort: 8080
 *       nodePort: 30012
 */

/* In YAML you have the concept of parsers and emitters. Parser
 * consume YAML input from a file, character buffer, or in our
 * case Netlink and emitters take data from some source and
 * present it in a YAML format.
 *
 * In this section of the code we are handling the parsing of the
 * Netlink packets coming in and using them to piece together a
 * YAML document. We could in theory just dump a YAML document
 * one line at a time over Netlink but the amount of data could
 * become very large and impact performance. Additionally, having
 * pseudo-YAML code in the kernel would be frowned on. We can
 * optimize the network traffic by taking advantage of the fact
 * that for key/value pairs the keys rarely change. We can
 * break up the data into keys and the values. The first Netlink
 * data packets received will be a nested keys table which we
 * can cache locally. As we receive the value pairs we can then
 * reconstruct the key : value pair by looking up the the key
 * in the stored table. In effect we end up with a one key to
 * many values stream of data.
 *
 * The data structures below are used to create a tree data
 * structure which is the natural flow of both YAML and
 * Netlink.
 */
struct yaml_nl_node {
	struct nl_list_head	list;
	struct nl_list_head	children;
	struct ln_key_list	keys;
};

struct yaml_netlink_input {
	yaml_parser_t		*parser;
	void			*buffer;
	const char		*errmsg;
	struct nl_sock		*nl;
	bool			complete;
	unsigned int		indent;
	struct yaml_nl_node	*cur;
	struct yaml_nl_node	*root;
};

/* Sadly this is not exported out of libyaml. We want to
 * give descent error message to help people track down
 * issues. This is internal only to this code. The end
 * user will never need to use this.
 */
static int
yaml_parser_set_reader_error(yaml_parser_t *parser, const char *problem,
			     size_t offset, int value)
{
	parser->error = YAML_READER_ERROR;
	parser->problem = problem;
	parser->problem_offset = offset;
	parser->problem_value = value;

	return 0;
}

/* This is used to handle all the Netlink packets containing the keys
 * for the key/value pairs. Instead of creating unique code to handle
 * every type of Netlink attributes possible we create a generic
 * abstract so the same code be used with everything. To make this
 * work the key table trasmitted must report the tree structure and
 * state of the keys. We use nested attributes as a way to notify libyaml
 * we have a new collection. This is used to create the tree structure
 * of the YAML document. Each collection of attributes define the following:
 *
 * LN_SCALAR_ATTR_INDEX:
 *	enum XXX_ATTR that defines which value we are dealing with. This
 *	varies greatly depending on the subsystem we have developed for.
 *
 * LN_SCALAR_ATTR_NLA_TYPE:
 *	The Netlink attribute type (NLA_STRING, NLA_U32, etc..) the coming
 *	value will be.
 *
 * LN_SCALAR_ATTR_VALUE:
 *	The key's actually scalar value.
 *
 * LN_SCALAR_ATTR_KEY_TYPE:
 *	What YAML format is it? block or flow. Only useful for
 *	LN_SCALAR_ATTR_NLA_TYPE of type NLA_NESTED or NLA_NUL_STRING
 *
 * LN_SCALAR_ATTR_LIST + CFS_SCALAR_LIST_SIZE:
 *	Defined the next collection which is a collection of nested
 *	attributes of the above.
 */
static struct nla_policy scalar_attr_policy[LN_SCALAR_CNT + 1] = {
	[LN_SCALAR_ATTR_LIST]		= { .type = NLA_NESTED },
	[LN_SCALAR_ATTR_LIST_SIZE]	= { .type = NLA_U16 },
	[LN_SCALAR_ATTR_INDEX]		= { .type = NLA_U16 },
	[LN_SCALAR_ATTR_NLA_TYPE]	= { .type = NLA_U16 },
	[LN_SCALAR_ATTR_VALUE]		= { .type = NLA_STRING },
	[LN_SCALAR_ATTR_KEY_FORMAT]	= { .type = NLA_U16 },
};

static int yaml_parse_key_list(struct yaml_netlink_input *data,
			       struct yaml_nl_node *parent,
			       struct nlattr *list)
{
	struct nlattr *tbl_info[LN_SCALAR_CNT + 1];
	struct yaml_nl_node *node = NULL;
	struct nlattr *attr;
	int rem;

	nla_for_each_nested(attr, list, rem) {
		uint16_t index = 0;

		if (nla_parse_nested(tbl_info, LN_SCALAR_CNT, attr,
				     scalar_attr_policy))
			break;

		if (tbl_info[LN_SCALAR_ATTR_LIST_SIZE]) {
			size_t cnt;

			cnt = nla_get_u16(tbl_info[LN_SCALAR_ATTR_LIST_SIZE]) + 1;
			if (!node) {
				size_t len = sizeof(struct nl_list_head) * 2;

				len += sizeof(struct ln_key_props) * cnt;
				node = calloc(1, len);
				if (!node)
					return NL_STOP;

				node->keys.lkl_maxattr = cnt;
				NL_INIT_LIST_HEAD(&node->children);
				nl_init_list_head(&node->list);

				if (!data->root)
					data->root = node;
				if (!data->cur)
					data->cur = node;
				if (parent)
					nl_list_add_tail(&node->list,
							 &parent->children);
			}
		}

		if (tbl_info[LN_SCALAR_ATTR_INDEX])
			index = nla_get_u16(tbl_info[LN_SCALAR_ATTR_INDEX]);

		if (!node || index == 0)
			return NL_STOP;

		if (tbl_info[LN_SCALAR_ATTR_KEY_FORMAT]) {
			uint16_t format;

			format = nla_get_u16(tbl_info[LN_SCALAR_ATTR_KEY_FORMAT]);
			node->keys.lkl_list[index].lkp_key_format = format;
		}

		if (tbl_info[LN_SCALAR_ATTR_NLA_TYPE]) {
			uint16_t type;

			type = nla_get_u16(tbl_info[LN_SCALAR_ATTR_NLA_TYPE]);
			node->keys.lkl_list[index].lkp_data_type = type;
		}

		if (tbl_info[LN_SCALAR_ATTR_VALUE]) {
			char *name;

			name = nla_strdup(tbl_info[LN_SCALAR_ATTR_VALUE]);
			if (!name)
				return NL_STOP;
			node->keys.lkl_list[index].lkp_values = name;
		}

		if (tbl_info[LN_SCALAR_ATTR_LIST]) {
			int rc =  yaml_parse_key_list(data, node,
						      tbl_info[LN_SCALAR_ATTR_LIST]);
			if (rc != NL_OK)
				return rc;
		}
	}
	return NL_OK;
}

static struct yaml_nl_node *get_next_child(struct yaml_nl_node *node,
					   unsigned int idx)
{
	struct yaml_nl_node *child;
	unsigned int i = 0;

	nl_list_for_each_entry(child, &node->children, list)
		if (idx == i++)
			return child;

	return NULL;
}

/**
 * In the YAML C implementation the scanner transforms the input stream
 * (Netlink in this case) into a sequence of keys. First we need to
 * examine the potential keys involved to see the mapping to Netlink.
 * We have chosen to examine the YAML stack with keys since they are
 * more detailed when compared to yaml_document_t / yaml_nodes and
 * yaml_event_t.
 *
 *	STREAM-START(encoding)		# The stream start.
 *	STREAM-END			# The stream end.
 *      VERSION-DIRECTIVE(major,minor)	# The '%YAML' directive.
 *      TAG-DIRECTIVE(handle,prefix)	# The '%TAG' directive.
 *      DOCUMENT-START			# '---'
 *      DOCUMENT-END			# '...'
 *      BLOCK-SEQUENCE-START		# Indentation increase denoting a block
 *      BLOCK-MAPPING-START		# sequence or a block mapping.
 *      BLOCK-END			# Indentation decrease.
 *      FLOW-SEQUENCE-START		# '['
 *      FLOW-SEQUENCE-END		# ']'
 *      FLOW-MAPPING-START		# '{'
 *      FLOW-MAPPING-END		# '}'
 *      BLOCK-ENTRY			# '-'
 *      FLOW-ENTRY			# ','
 *      KEY				# '?' or nothing (simple keys).
 *      VALUE				# ':'
 *      ALIAS(anchor)			# '*anchor'
 *      ANCHOR(anchor)			# '&anchor'
 *      TAG(handle,suffix)		# '!handle!suffix'
 *      SCALAR(value,style)		# A scalar.
 *
 * For our read_handler / write_handler STREAM-START / STREAM-END,
 * VERSION-DIRECTIVE, and TAG-DIRECTIVE are hanndler by the libyaml
 * internal scanner so we don't need to deal with it. Normally for
 * LNet / Lustre DOCUMENT-START / DOCUMENT-END are not needed but it
 * could be easily handled. In the case of multiplex streams we could
 * see these used to differentiate data coming in.
 *
 * It is here we handle any simple scalars or values of the key /value
 * pair. How the YAML document is formated is dependent on the key
 * table's data.
 */
static void yaml_parse_value_list(struct yaml_netlink_input *data, int *size,
				  struct nlattr *attr_array[],
				  struct ln_key_props *parent)
{
	struct yaml_nl_node *node = data->cur;
	struct ln_key_props *keys = node->keys.lkl_list;
	int mapping = parent->lkp_key_format;
	int child_idx = 0, len = 0, i;

	for (i = 1; i < node->keys.lkl_maxattr; i++) {
		struct nlattr *attr;

		attr = attr_array[i];
		if (!attr && !keys[i].lkp_values)
			continue;

		if (keys[i].lkp_data_type != NLA_NUL_STRING &&
		    keys[i].lkp_data_type != NLA_NESTED) {
			if (!attr)
				continue;

			if (!(mapping & LNKF_FLOW)) {
				unsigned int indent = data->indent ?
						      data->indent : 2;

				memset(data->buffer, ' ', indent);
				if (mapping & LNKF_SEQUENCE) {
					((char *)data->buffer)[indent - 2] = '-';
					if (mapping & LNKF_MAPPING)
						mapping &= ~LNKF_SEQUENCE;
				}
				data->buffer += indent;
				*size -= indent;
			}

			if (mapping & LNKF_MAPPING) {
				len = snprintf(data->buffer, *size, "%s: ",
					       keys[i].lkp_values);
				if (len < 0)
					goto unwind;
				data->buffer += len;
				*size -= len;
			}
		}

		switch (keys[i].lkp_data_type) {
		case NLA_NESTED: {
			struct yaml_nl_node *next = get_next_child(node,
								   child_idx++);
			int num = next->keys.lkl_maxattr;
			struct nla_policy nest_policy[num];
			struct yaml_nl_node *old;
			struct nlattr *cnt_attr;
			int rem, j;

			if (!attr)
				continue;

			memset(nest_policy, 0, sizeof(struct nla_policy) * num);
			for (j = 1; j < num; j++)
				nest_policy[j].type = next->keys.lkl_list[j].lkp_data_type;

			old = data->cur;
			data->cur = next;
			nla_for_each_nested(cnt_attr, attr, rem) {
				struct nlattr *nest_info[num];
				uint16_t indent = 0;

				if (nla_parse_nested(nest_info, num, cnt_attr,
						     nest_policy))
					break;

				if (keys[i].lkp_key_format & LNKF_FLOW) {
					char brace = '{';

					if (keys[i].lkp_key_format &
					    LNKF_SEQUENCE)
						brace = '[';

					len = snprintf(data->buffer, *size,
						       "%*s%s: %c ",
						       data->indent, "",
						       keys[i].lkp_values,
						       brace);
				} else {
					if (keys[i].lkp_key_format &
					    LNKF_MAPPING)
						indent += 2;
					if (keys[i].lkp_key_format &
					    LNKF_SEQUENCE)
						indent += 2;

					len = snprintf(data->buffer, *size,
						       "%*s%s:\n",
						       data->indent, "",
						       keys[i].lkp_values);
				}
				if (len < 0)
					goto unwind;
				data->buffer += len;
				*size -= len;
				len = 0;

				data->indent += indent;
				yaml_parse_value_list(data, size, nest_info,
						      &keys[i]);
				data->indent -= indent;

				if (keys[i].lkp_key_format & LNKF_FLOW) {
					char *tmp = (char *)data->buffer - 2;
					char *brace = " }\n";

					if (keys[i].lkp_key_format &
					    LNKF_SEQUENCE)
						brace = " ]\n";

					memcpy(tmp, brace, strlen(brace));
					data->buffer++;
					*size -= 1;
				}
			}
			data->cur = old;
			break;
		}

		case NLA_NUL_STRING:
			if (i == 1) {
				if (data->cur != data->root)
					goto not_first;

				/* The top level is special so only print
				 * once
				 */
				if (strlen(keys[i].lkp_values)) {
					len = snprintf(data->buffer,
						       *size, "%s:\n",
						       keys[i].lkp_values);
					if (len < 0)
						goto unwind;
					data->buffer += len;
					*size -= len;
					len = 0;
				}
				data->indent = 0;
				if (!(mapping & LNKF_FLOW)) {
					if (mapping & LNKF_SEQUENCE)
						data->indent += 2;
					else if (mapping & LNKF_MAPPING)
						data->indent += 2;
				}
not_first:
				if (attr && parent->lkp_values) {
					free(parent->lkp_values);
					parent->lkp_values = nla_strdup(attr);
				}
			}
			break;

		case NLA_STRING:
			len = snprintf(data->buffer, *size, "%s",
				       nla_get_string(attr));
			break;

		case NLA_U16:
			len = snprintf(data->buffer, *size, "%hu",
				       nla_get_u16(attr));
			break;

		case NLA_U32:
			len = snprintf(data->buffer, *size, "%u",
				       nla_get_u32(attr));
			break;

		case NLA_U64:
			len = snprintf(data->buffer, *size, "%ju",
				       nla_get_u64(attr));
			break;

		case NLA_S16:
			len = snprintf(data->buffer, *size, "%hd",
				       nla_get_u16(attr));
			break;

		case NLA_S32:
			len = snprintf(data->buffer, *size, "%d",
				       nla_get_s32(attr));
			break;

		case NLA_S64:
			len = snprintf(data->buffer, *size, "%jd",
				       nla_get_s64(attr));
		/* fallthrough */
		default:
			break;
		}

		if (len) {
			if (mapping & LNKF_FLOW) {
				strcat((char *)data->buffer, ", ");
				len += 2;
			} else {
				((char *)data->buffer)[len++] = '\n';
			}
			data->buffer += len;
			*size += len;
		} else if (len < 0) {
unwind:
			data->buffer -= data->indent + 2;
			*size -= data->indent + 2;
		}
	}
}

/* This is the CB_VALID callback for the Netlink library that we
 * have hooked into. Any successful Netlink message is passed to
 * this function which handles both the incoming key tables and
 * the values of the key/value pairs being received. We use
 * the NLM_F_CREATE flag to determine if the incoming Netlink
 * message is a key table or a packet containing value pairs.
 */
static int yaml_netlink_msg_parse(struct nl_msg *msg, void *arg)
{
	struct yaml_netlink_input *data = arg;
	struct nlmsghdr *nlh = nlmsg_hdr(msg);

	if (nlh->nlmsg_flags & NLM_F_CREATE) {
		struct nlattr *attrs[LN_SCALAR_CNT + 1];

		if (genlmsg_parse(nlh, 0, attrs, LN_SCALAR_CNT + 1,
				  scalar_attr_policy))
			return NL_SKIP;

		if (attrs[LN_SCALAR_ATTR_LIST]) {
			int rc = yaml_parse_key_list(data, NULL,
						     attrs[LN_SCALAR_ATTR_LIST]);
			if (rc != NL_OK)
				return rc;

			/* reset to root node */
			data->cur = data->root;
		}
	} else {
		uint16_t maxtype = data->cur->keys.lkl_maxattr;
		struct nla_policy policy[maxtype];
		struct nlattr *attrs[maxtype];
		int size, i;

		memset(policy, 0, sizeof(struct nla_policy) * maxtype);
		for (i = 1; i < maxtype; i++)
			policy[i].type = data->cur->keys.lkl_list[i].lkp_data_type;

		if (genlmsg_parse(nlh, 0, attrs, maxtype, policy))
			return NL_SKIP;

		size = data->parser->raw_buffer.end -
		       (unsigned char *)data->buffer;
		yaml_parse_value_list(data, &size, attrs,
				      &data->cur->keys.lkl_list[1]);
	}

	if (nlh->nlmsg_flags & NLM_F_MULTI && nlh->nlmsg_type != NLMSG_DONE)
		return NL_OK;

	return NL_STOP;
}

static bool cleanup_children(struct yaml_nl_node *parent)
{
	struct yaml_nl_node *child;

	if (nl_list_empty(&parent->children)) {
		struct ln_key_props *keys = parent->keys.lkl_list;
		int i;

		for (i = 1; i < parent->keys.lkl_maxattr; i++)
			if (keys[i].lkp_values)
				free(keys[i].lkp_values);
		nl_list_del(&parent->list);
		return true;
	}

	while ((child = get_next_child(parent, 0)) != NULL) {
		if (cleanup_children(child))
			free(child);
	}

	return false;
}

/* This is the libnl callback for when the last Netlink packet
 * is finished being parsed or its called right away in case
 * the Linux kernel reports back an error from the Netlink layer.
 */
static int yaml_netlink_msg_complete(struct nl_msg *msg, void *arg)
{
	struct yaml_netlink_input *data = arg;
	struct nlmsghdr *nlh = nlmsg_hdr(msg);
	struct nlmsgerr *errmsg = nlmsg_data(nlh);

	if ((nlh->nlmsg_type == NLMSG_ERROR ||
	     nlh->nlmsg_flags & NLM_F_ACK_TLVS) && errmsg->error) {
		/* libyaml stomps on the reader error so we need to
		 * cache the source of the error.
		 */
		data->errmsg = nl_geterror(nl_syserr2nlerr(errmsg->error));
#ifdef HAVE_USRSPC_NLMSGERR
		/* Newer kernels support NLM_F_ACK_TLVS in nlmsg_flags
		 * which gives greater detail why we failed.
		 */
		if (nlh->nlmsg_flags & NLM_F_ACK_TLVS) {
			struct nla_policy extack_policy[NLMSGERR_ATTR_MAX + 1] = {
				[NLMSGERR_ATTR_MSG]	= { .type = NLA_STRING },
				[NLMSGERR_ATTR_OFFS]	= { .type = NLA_U32 },
			};
			struct nlattr *tb[NLMSGERR_ATTR_MAX + 1];

			if (nlmsg_parse(nlh, 0, tb, sizeof(extack_policy),
					extack_policy) == 0) {
				if (tb[NLMSGERR_ATTR_MSG])
					data->errmsg = nla_get_string(tb[NLMSGERR_ATTR_MSG]);
			}
		}
#endif /* HAVE_USRSPC_NLMSGERR */
		data->parser->error = YAML_READER_ERROR;
	} else {
		cleanup_children(data->root);
		free(data->root);
	}

	data->complete = true;
	return NL_STOP;
}

/**
 * In order for yaml_parser_set_input_netlink() to work we have to
 * register a yaml_read_handler_t callback. This is that call back
 * which listens for Netlink packets. Internally nl_recvmsg_report()
 * calls the various callbacks discussed above.
 */
static int yaml_netlink_read_handler(void *arg, unsigned char *buffer,
				     size_t size, size_t *size_read)
{
	struct yaml_netlink_input *data = arg;
	struct nl_sock *nl = data->nl;
	struct nl_cb *cb;
	int rc = 0;

	if (data->complete) {
		*size_read = 0;
		return 1;
	}

	data->buffer = buffer;

	cb = nl_socket_get_cb(nl);
	rc = nl_recvmsgs_report(nl, cb);
	if (rc == -NLE_INTR) {
		*size_read = 0;
		return 1;
	} else if (rc < 0) {
		data->errmsg = nl_geterror(rc);
		return 0;
	} else if (data->parser->error) {
		/* data->errmsg is set in NL_CB_FINISH */
		return 0;
	}

	rc = (unsigned char *)data->buffer - buffer;
	if ((int)size > rc)
		size = rc;

	*size_read = size;
	return 1;
}

/* libyaml by default just reports "input error" for parser read_handler_t
 * issues which is not useful. This provides away to get better debugging
 * info.
 */
YAML_DECLARE(const char *)
yaml_parser_get_reader_error(yaml_parser_t *parser)
{
	struct yaml_netlink_input *buf = parser->read_handler_data;

	if (!buf)
		return NULL;

	return buf->errmsg;
}

/* yaml_parser_set_input_netlink() mirrors the libyaml function
 * yaml_parser_set_input_file(). Internally it does setup of the
 * libnl socket callbacks to parse the Netlink messages received
 * as well as register the special yaml_read_handler_t for libyaml.
 * This is exposed for public use.
 */
YAML_DECLARE(int)
yaml_parser_set_input_netlink(yaml_parser_t *reply, struct nl_sock *nl,
			      bool stream)
{
	struct yaml_netlink_input *buf;
	int rc;

	buf = calloc(1, sizeof(*buf));
	if (!buf) {
		reply->error = YAML_MEMORY_ERROR;
		return false;
	}

	rc = lustre_netlink_register(nl, stream);
	if (rc < 0) {
		yaml_parser_set_reader_error(reply,
					     "netlink setup failed", 0,
					     -rc);
		goto failed;
	}

	buf->nl = nl;
	buf->parser = reply;
	yaml_parser_set_input(buf->parser, yaml_netlink_read_handler, buf);

	rc = nl_socket_modify_cb(buf->nl, NL_CB_VALID, NL_CB_CUSTOM,
				 yaml_netlink_msg_parse, buf);
	if (rc < 0) {
		yaml_parser_set_reader_error(reply,
					     "netlink msg recv setup failed",
					     0, -rc);
		goto failed;
	}

	rc = nl_socket_modify_cb(buf->nl, NL_CB_FINISH, NL_CB_CUSTOM,
				 yaml_netlink_msg_complete, buf);
	if (rc < 0) {
		yaml_parser_set_reader_error(reply,
					     "netlink msg cleanup setup failed",
					     0, -rc);
failed:
		free(buf);
	}
	return rc < 0 ? false : true;
}

/* The role of the YAML emitter for us is to take a YAML document and
 * change into a Netlink stream to send to the kernel to be processed.
 * This provides the infrastructure to do this.
 */
struct yaml_netlink_output {
	yaml_emitter_t		*emitter;
	struct nl_sock		*nl;
	char			*family;
	int			family_id;
	int			version;
	int			cmd;
	int			pid;
	int			flags;
};

/* Internal use for this file only. We fill in details of why creating
 * a Netlink packet to send failed. The end user will be able to debug
 * what went wrong.
 */
static int
yaml_emitter_set_writer_error(yaml_emitter_t *emitter, const char *problem)
{
	emitter->error = YAML_WRITER_ERROR;
	emitter->problem = problem;

	return 0;
}

static unsigned int indent_level(const char *str)
{
	char *tmp = (char *)str;

	while (isspace(*tmp))
		++tmp;
	return tmp - str;
}

#define LNKF_END 8

static enum lnet_nl_key_format yaml_format_type(yaml_emitter_t *emitter,
						char *line,
						unsigned int *offset,
						enum lnet_nl_key_format prev)
{
	unsigned int indent = *offset;
	unsigned int new_indent = 0;

	if (strchr(line, '{') || strchr(line, '['))
		return LNKF_FLOW;

	new_indent = indent_level(line);
	if (new_indent < indent) {
		*offset = indent - emitter->best_indent;
		return LNKF_END;
	}

	if (strncmp(line + new_indent, "- ", 2) == 0) {
		*offset = new_indent + emitter->best_indent;
		return LNKF_SEQUENCE;
	}

	if (indent != new_indent) {
		*offset = new_indent;
		if (prev != LNKF_MAPPING)
			return LNKF_MAPPING;
	}

	return 0;
}

static int yaml_create_nested_list(struct yaml_netlink_output *out,
				   struct nl_msg *msg, char **hdr,
				   char **entry, unsigned int *indent,
				   enum lnet_nl_key_format fmt)
{
	struct nlattr *list = NULL;
	char *line;
	int rc = 0;

	list = nla_nest_start(msg, LN_SCALAR_ATTR_LIST);
	if (!list) {
		yaml_emitter_set_writer_error(out->emitter,
					      "Emmitter netlink list creation failed");
		nlmsg_free(msg);
		rc = -EINVAL;
		goto nla_put_failure;
	}

	if (fmt & LNKF_FLOW) {
		while ((line = strsep(hdr, ",")) != NULL) {
			char *tmp = NULL;

			if (strchr(line, '{') ||
			    strchr(line, '[') ||
			    strchr(line, ' '))
				line++;

			tmp = strchr(line, '}');
			if (!tmp)
				tmp = strchr(line, ']');
			if (tmp)
				*tmp = '\0';

			NLA_PUT_STRING(msg,
				       LN_SCALAR_ATTR_VALUE,
				       line);
		}
		nla_nest_end(msg, list);
		return 0;
	}

	NLA_PUT_STRING(msg, LN_SCALAR_ATTR_VALUE, *hdr + *indent);
	do {
		line = strsep(entry, "\n");
have_next_line:
		if (!line || !strlen(line) || strcmp(line, "...") == 0)
			break;

		fmt = yaml_format_type(out->emitter, line, indent, fmt);
		if (fmt == LNKF_END)
			break;

		if (fmt) {
			rc = yaml_create_nested_list(out, msg, &line, entry,
						     indent, fmt);
			if (rc)
				goto nla_put_failure;

			goto have_next_line;
		} else {
			NLA_PUT_STRING(msg, LN_SCALAR_ATTR_VALUE,
				       line + *indent);
		}
	} while (strcmp(line, ""));

	nla_nest_end(msg, list);
	/* strsep in the above loop moves entry to a value pass the end of the
	 * nested list. So to avoid losing this value we replace hdr with line
	 */
	*hdr = line;
nla_put_failure:
	return rc;
}

/* YAML allows ' and " in its documents but those characters really
 * confuse libc string handling. The workaround is to replace
 * ' and " with another reserved character for YAML '%' which is
 * for tags which shouldn't matter if we send in a Netlink packet.
 * The kernel side will need to handle % in a special way.
 */
static void yaml_quotation_handling(char *buf)
{
	char *tmp = buf, *line;

	while ((line = strchr(tmp, '\"')) != NULL) {
		line[0] = '%';
		line[1] = ' ';
		tmp = strchr(line, '\"') - 1;
		tmp[0] = ' ';
		tmp[1] = '%';
	}

	while ((line = strchr(tmp, '\'')) != NULL) {
		line[0] = '%';
		line[1] = ' ';
		tmp = strchr(line, '\'') - 1;
		tmp[0] = ' ';
		tmp[1] = '%';
	}
}

/* libyaml takes the YAML documents and places the data into an
 * internal buffer to the library. We take each line and turn it
 * into a Netlink message using the same format as the key table.
 * The reason for this approach is that we can do filters at the
 * key level or the key + value level.
 */
static int yaml_netlink_write_handler(void *data, unsigned char *buffer,
				      size_t size)
{
	struct yaml_netlink_output *out = data;
	char *buf = strndup((char *)buffer, size);
	char *entry = buf, *tmp = buf, *line;
	enum lnet_nl_key_format fmt = 0;
	struct nl_msg *msg = NULL;
	unsigned int indent = 0;
	bool nogroups = true;
	int rc = 0;

	yaml_quotation_handling(entry);

	while (entry && strcmp(line = strsep(&entry, "\n"), "")) {
already_have_line:
		if (strcmp(line, "---") == 0 || strcmp(line, "...") == 0)
			continue;

		/* In theory we could have a sequence of groups but a bug in
		 * libyaml prevents this from happing
		 */
		if (line[0] != ' ' && line[0] != '-') {
			tmp = strchr(line, ':');
			if (!tmp)
				continue;
			*tmp = '\0';

			rc = lustre_netlink_add_group(out->nl, out->family,
						      line);
			if (rc < 0) {
				yaml_emitter_set_writer_error(out->emitter,
							      "Netlink group does not exist");
				goto nla_put_failure;
			}
			nogroups = false;

			/* Handle case first line contains more than a
			 * simple key
			 */
			line = tmp + 2;
			if (strchr(line, '{') || strchr(line, '['))
				goto complicated;
		} else {
complicated:
			if (!msg) {
				void *usr_hdr;

				msg = nlmsg_alloc();
				if (!msg) {
					out->emitter->error = YAML_MEMORY_ERROR;
					goto nla_put_failure;
				}

				usr_hdr = genlmsg_put(msg, out->pid,
						      NL_AUTO_SEQ,
						      out->family_id, 0,
						      out->flags, out->cmd,
						      out->version);
				if (!usr_hdr) {
					out->emitter->error = YAML_MEMORY_ERROR;
					nlmsg_free(msg);
					goto nla_put_failure;
				}
			}

			fmt = yaml_format_type(out->emitter, line, &indent,
					       fmt);
			if (fmt) {
				rc = yaml_create_nested_list(out, msg, &line,
							     &entry, &indent,
							     fmt);
				if (rc) {
					yaml_emitter_set_writer_error(out->emitter,
								      nl_geterror(rc));
					nlmsg_free(msg);
					goto nla_put_failure;
				}
				/* yaml_created_nested_list set line to the next
				 * entry. We can just add it to the msg directly.
				 */
				if (line)
					goto already_have_line;
			} else {
				NLA_PUT_STRING(msg, LN_SCALAR_ATTR_VALUE,
					       line + indent);
			}
		}
	}

	/* Don't success if no valid groups found */
	if (nogroups) {
		yaml_emitter_set_writer_error(out->emitter,
					      "Emitter contains no valid Netlink groups");
		goto nla_put_failure;
	}

	if (msg) {
		rc = nl_send_auto(out->nl, msg);
		nlmsg_free(msg);
	} else {
		rc = genl_send_simple(out->nl, out->family_id, out->cmd,
				      out->version, out->flags);
	}
	if (rc < 0)
		yaml_emitter_set_writer_error(out->emitter,
					      nl_geterror(rc));
nla_put_failure:
	free(buf);
	return out->emitter->error == YAML_NO_ERROR ? 1 : 0;
}

/* This function is used by external utilities to use Netlink with
 * libyaml so we can turn YAML documentations into Netlink message
 * to send. This behavior mirrors yaml_emitter_set_output_file()
 * which is used to write out a YAML document to a file.
 */
YAML_DECLARE(int)
yaml_emitter_set_output_netlink(yaml_emitter_t *sender, struct nl_sock *nl,
				char *family, int version, int cmd, int flags)
{
	struct yaml_netlink_output *out;

	out = calloc(1, sizeof(*out));
	if (!out) {
		sender->error = YAML_MEMORY_ERROR;
		return false;
	}

	/* Get family ID */
	out->family_id = genl_ctrl_resolve(nl, family);
	if (out->family_id < 0) {
		yaml_emitter_set_writer_error(sender,
					      "failed to resolve Netlink family id");
		free(out);
		return false;
	}
	out->emitter = sender;
	out->nl = nl;
	out->family = family;
	out->version = version;
	out->cmd = cmd;
	out->flags = flags;
	out->pid = nl_socket_get_local_port(nl);
	yaml_emitter_set_output(sender, yaml_netlink_write_handler, out);
	return true;
}

/* Error handling helpers */
void yaml_emitter_log_error(yaml_emitter_t *emitter, FILE *log)
{
	/* YAML_WRITER_ERROR means no Netlink support so use old API */
	switch (emitter->error) {
	case YAML_MEMORY_ERROR:
		fprintf(log, "Memory error: Not enough memory for emitting\n");
			break;
	case YAML_WRITER_ERROR:
		fprintf(log, "Writer error: %s\n", emitter->problem);
		break;
	case YAML_EMITTER_ERROR:
		fprintf(log, "Emitter error: %s\n", emitter->problem);
	default:
		break;
	}
}

void yaml_parser_log_error(yaml_parser_t *parser, FILE *log, const char *errmsg)
{
	const char *extra;

	switch (parser->error) {
	case YAML_MEMORY_ERROR:
		fprintf(log, "Memory error: Not enough memory for parser\n");
		break;

	case YAML_SCANNER_ERROR:
	case YAML_PARSER_ERROR:
		if (parser->context) {
			fprintf(log,
				"%s error: %s at line %d, column %d\n%s at line %d, column %d\n",
				parser->error == YAML_SCANNER_ERROR ? "Scanner" : "Parser",
				parser->context,
				(int)parser->context_mark.line + 1,
				(int)parser->context_mark.column + 1,
				parser->problem,
				(int)parser->problem_mark.line + 1,
				(int)parser->problem_mark.column + 1);
		} else {
			fprintf(log, "%s error: %s at line %d, column %d\n",
				parser->error == YAML_SCANNER_ERROR ? "Scanner" : "Parser",
				parser->problem,
				(int)parser->problem_mark.line + 1,
				(int)parser->problem_mark.column + 1);
		}
		break;

	case YAML_READER_ERROR:
		extra = yaml_parser_get_reader_error(parser);
		if (!extra)
			extra = parser->problem;

		if (parser->problem_value != -1) {
			fprintf(log,
				"Failed to %s: reader error '%s':#%X at %ld'\n",
				errmsg, extra, parser->problem_value,
				(long)parser->problem_offset);
		} else {
			fprintf(log,
				"Failed to %s: reader error '%s' at %ld\n",
				errmsg, extra, (long)parser->problem_offset);
		}
	/* fallthrough */
	default:
		break;
	}
}
