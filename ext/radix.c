/*
 * Radix data-store for ruby
 *
 * Copyright (c) 2012, Internet Initiative Japan Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

 */

#include <arpa/inet.h>
#include "ruby.h"
#include "radixlib.h"

/*--------- Macros --------*/
#define GetRadix(obj, radixp) do {\
		Data_Get_Struct(obj, struct radixdata, radixp); \
		if (radixp == NULL) closed_radix();             \
		if (radixp->rt[RT_IPV4] == NULL) closed_radix();\
		if (radixp->rt[RT_IPV6] == NULL) closed_radix();\
} while (0)

#define RaiseModified(idx, idy) do {\
	if (idx != idy) \
		rb_raise(rb_eRuntimeError, \
			 "Radix tree modified during iteration"); \
} while (0)

#define RTNUM 2		/* IPv4 table + IPv6 table */
#define RT_IPV4 0
#define RT_IPV6 1

/*--------- Local Structures --------*/
struct radixdata {
	radix_tree_t *rt[RTNUM];	/* Radix tree for IPv4/IPv6 */
	unsigned int gen_id;	/* Detect modification during iterations */
};

struct radixnode {
	prefix_t prefix;
	VALUE msg;
};

/*--------- Local Variables --------*/
static VALUE rb_cRadixNode;


/*--------- Prototypes --------*/
static VALUE rb_radix_initialize(int, VALUE *, VALUE);
static VALUE rb_radix_add(int, VALUE *, VALUE);
static VALUE rb_radix_search_best(int, VALUE *, VALUE);
static VALUE rb_radix_search_exact(int, VALUE *, VALUE);
static void closed_radix(void);
static prefix_t *args_to_prefix(char *, long);
static VALUE object_node_add(struct radixdata *, prefix_t *, VALUE, VALUE);
static void free_radixdata(struct radixdata *);
static VALUE rb_radix_aref(int, VALUE *, VALUE);
static VALUE rb_radix_keys(VALUE);
static VALUE rb_radix_values(VALUE);
static VALUE rb_radix_delete(int, VALUE *, VALUE);
static VALUE rb_radix_clear(VALUE);
static VALUE rb_radix_clear(VALUE);
static VALUE rb_radix_each_key(VALUE);
static VALUE rb_radix_each_value(VALUE);
static VALUE rb_radix_each_pair(VALUE);
static VALUE rb_radix_length(VALUE);
static VALUE rb_radix_to_hash(VALUE);
static void rn_free_func(radix_node_t *, void *);
static struct radixdata* newRadixData(void);
static void radix_mark(struct radixdata *);
static VALUE radix_s_alloc(VALUE);
static VALUE radixnode_s_alloc(VALUE);
static void rn_mark(struct radixnode *);

/*-------- class Radix --------*/

static int
init_radixdata(struct radixdata *radixp)
{
	radix_tree_t *rt4, *rt6;

	if ((rt4 = New_Radix()) == NULL)
		return NULL;
	if ((rt6 = New_Radix()) == NULL) {
		xfree(rt4);
		return NULL;
	}
	radixp->rt[RT_IPV4] = rt4;
	radixp->rt[RT_IPV6] = rt6;
	radixp->gen_id = 0;

	return radixp;
}

static struct radixdata*
newRadixData(void)
{
	struct radixdata *radixp;

	radixp = ALLOC(struct radixdata);
	init_radixdata(radixp);

	return radixp;
}

static void
free_radixdata0(struct radixdata *radixp)
{
	if (radixp == NULL)
		return;

	if (radixp->rt[RT_IPV4] != NULL)
		Destroy_Radix(radixp->rt[RT_IPV4], rn_free_func, NULL);

	if (radixp->rt[RT_IPV6] != NULL)
		Destroy_Radix(radixp->rt[RT_IPV6], rn_free_func, NULL);
}

static void
free_radixdata(struct radixdata *radixp)
{
	free_radixdata0(radixp);
	xfree(radixp);
}

static prefix_t*
args_to_prefix(char *addr, long prefixlen)
{
	prefix_t *prefix = NULL;
	const char *errmsg;

	if (addr == NULL)
		rb_raise(rb_eRuntimeError, "No address specified");

	if (addr != NULL) { /* Parse a string address */
		if ((prefix = prefix_pton(addr, prefixlen, &errmsg)) == NULL)
			rb_raise(rb_eRuntimeError, "Invalid address format");
	}
	if (prefix != NULL &&
	    prefix->family != AF_INET && prefix->family != AF_INET6) {
		Deref_Prefix(prefix);
		return NULL;
	}

	return prefix;
}

/*
 * call-seq:
 *   Radix.new() => radix
 *
 * Open a radix database.
 */
static VALUE
rb_radix_initialize(int argc, VALUE *argv, VALUE self)
{
	return self;
}

static void
radix_mark(struct radixdata *radixp)
{
	radix_node_t *rn;
	unsigned int gen_id_cur;
	int i;

	gen_id_cur = radixp->gen_id;

	for (i = 0; i < RTNUM; i++) {
		RADIX_WALK(radixp->rt[i]->head, rn) {
			RaiseModified(gen_id_cur, radixp->gen_id);
			if (rn->data != NULL)
				rb_gc_mark((VALUE)rn->data);
		} RADIX_WALK_END;
	}
}

#define PICKRT(prefix, rno) (prefix->family == AF_INET6 ? rno->rt[RT_IPV6] : rno->rt[RT_IPV4])

static VALUE
object_node_add(struct radixdata *radp, prefix_t *prefix, VALUE self, VALUE msg)
{
	radix_node_t *node;
	struct radixnode *rn;
	VALUE obj;

	if ((node = radix_lookup(PICKRT(prefix, radp), prefix)) == NULL)
		rb_raise(rb_eRuntimeError, "Cannot add prefix");

	obj = rb_obj_alloc(rb_cRadixNode);
	Data_Get_Struct(obj, struct radixnode, rn);
	node->data = (void *)msg;
	rn->msg = msg;
	memcpy(&rn->prefix, node->prefix, sizeof(prefix_t));
	radp->gen_id++;

	return obj;
}

/*
 * call-seq:
 *   radix.add(key[,prefixlen]) -> hash
 *
 * Stores the hash object in the database, indexed via the string key
 * provided.
 */
static VALUE
rb_radix_add(int argc, VALUE *argv, VALUE self)
{
	struct radixdata *radixp;
	VALUE v_addr, v_plen;
	prefix_t *prefix;
	int plen;
	VALUE obj;

	GetRadix(self, radixp);

	if (argc == 2) {
		rb_scan_args(argc, argv, "2", &v_addr, &v_plen);
		plen = FIX2INT(v_plen);
	} else {
		rb_scan_args(argc, argv, "1", &v_addr);
		plen = -1;
	}

	prefix = args_to_prefix(RSTRING_PTR(v_addr), plen);
	if (prefix == NULL)
		return Qnil;

	obj = object_node_add(radixp, prefix, self, Qtrue);
	Deref_Prefix(prefix);

	return obj;
}

static VALUE
rb_radix_add0(int argc, VALUE *argv, VALUE self)
{
	struct radixdata *radixp;
	VALUE v_addr, v_plen, v_msg;
	prefix_t *prefix;
	int plen;
	VALUE obj;

	GetRadix(self, radixp);

	if (argc == 3) {
		rb_scan_args(argc, argv, "3", &v_addr, &v_plen, &v_msg);
		plen = FIX2INT(v_plen);
	} else {
		rb_scan_args(argc, argv, "2", &v_addr, &v_msg);
		plen = -1;
	}

	prefix = args_to_prefix(RSTRING_PTR(v_addr), plen);
	if (prefix == NULL)
		return Qnil;

	obj = object_node_add(radixp, prefix, self, v_msg);
	Deref_Prefix(prefix);

	return obj;
}

static VALUE rb_radix_delete(int argc, VALUE *argv, VALUE self)
{
	struct radixdata *radixp;
	VALUE v_addr, v_plen;
	prefix_t *prefix;
	radix_node_t *node;
	int plen;

	GetRadix(self, radixp);

	if (argc == 2) {
		rb_scan_args(argc, argv, "11", &v_addr, &v_plen);
		plen = FIX2INT(v_plen);
	} else {
		rb_scan_args(argc, argv, "1", &v_addr);
		plen = -1;
	}

	prefix = args_to_prefix(RSTRING_PTR(v_addr), plen);
	if (prefix == NULL)
		return Qnil;

	if ((node = radix_search_exact(PICKRT(prefix, radixp), prefix)) == NULL) {
		Deref_Prefix(prefix);
		return Qnil;
	}
	if (node->data != NULL) {
		node->data = NULL;
	}		

	radix_remove(PICKRT(prefix, radixp), node);
	Deref_Prefix(prefix);

	radixp->gen_id++;

	return Qnil;
}

/*
 * call-seq:
 *   radix.search_best(key[, prefixlen]) -> hash
 *
 * Return a value from the database by locating the key string
 * provided. 
 * Search strategy is to match best suited for the key.
 * If the key is not found, returns nil.
 */
static VALUE
rb_radix_search_best(int argc, VALUE *argv, VALUE self)
{
	struct radixdata *radixp;
	radix_node_t *node;
	struct radixnode *rn;
	VALUE obj;
	VALUE v_addr, v_plen;
	prefix_t *prefix;
	int plen;

	GetRadix(self, radixp);

	if (argc == 2) {
		rb_scan_args(argc, argv, "11", &v_addr, &v_plen);
		plen = FIX2INT(v_plen);
	} else {
		rb_scan_args(argc, argv, "1", &v_addr);
		plen = -1;
	}

	prefix = args_to_prefix(RSTRING_PTR(v_addr), plen);
	if (prefix == NULL)
		return Qnil;

	if ((node = radix_search_best(PICKRT(prefix, radixp), prefix)) == NULL
	    || node->data == NULL) {
		Deref_Prefix(prefix);
		return Qnil;
	}
	Deref_Prefix(prefix);

	obj = rb_obj_alloc(rb_cRadixNode);
	Data_Get_Struct(obj, struct radixnode, rn);
	rn->msg = (VALUE)node->data;
	memcpy(&rn->prefix, node->prefix, sizeof(prefix_t));

	return obj;
}

/*
 * call-seq:
 *   radix.search_best(key[, prefixlen]) -> hash
 *
 * Return a value from the database by locating the key string
 * provided. 
 * Search strategy is to match exactly. If the key is not found,
 * returns nil.
 */
static VALUE
rb_radix_search_exact(int argc, VALUE *argv, VALUE self)
{
	struct radixdata *radixp;
	radix_node_t *node;
	struct radixnode *rn;
	VALUE v_addr, v_plen;
	VALUE obj;
	prefix_t *prefix;
	int plen;

	GetRadix(self, radixp);

	if (argc == 2) {
		rb_scan_args(argc, argv, "11", &v_addr, &v_plen);
		plen = FIX2INT(v_plen);
	} else {
		rb_scan_args(argc, argv, "1", &v_addr);
		plen = -1;
	}

	prefix = args_to_prefix(RSTRING_PTR(v_addr), plen);
	if (prefix == NULL)
		return Qnil;

	if ((node = radix_search_exact(PICKRT(prefix, radixp), prefix)) == NULL
	    || node->data == NULL) {
		Deref_Prefix(prefix);
		return Qnil;
	}
	Deref_Prefix(prefix);

	obj = rb_obj_alloc(rb_cRadixNode);
	Data_Get_Struct(obj, struct radixnode, rn);
	rn->msg = (VALUE)node->data;
	memcpy(&rn->prefix, node->prefix, sizeof(prefix_t));

	return obj;
}

/*
 * call-seq:
 *   radix[key] -> hash value or nil
 *
 * Return a value from the database by locating the key string
 * provided. If the key is not found, returns nil.
 */
static VALUE
rb_radix_aref(int argc, VALUE *argv, VALUE self)
{
	return rb_radix_search_best(argc, argv, self);
}


/*
 * call-seq:
 *   radix.length -> integer
 *
 * Returns the number of entries in the database.
 */
static VALUE
rb_radix_length(VALUE self)
{
	struct radixdata *radixp;
	radix_node_t *rn;
	unsigned int rn_count = 0;
	unsigned int gen_id_cur;
	int i;

	GetRadix(self, radixp);

	gen_id_cur = radixp->gen_id;

	for (i = 0; i < RTNUM; i++) {
		RADIX_WALK(radixp->rt[i]->head, rn) {
			RaiseModified(gen_id_cur, radixp->gen_id);
			if (rn->data != NULL)
				rn_count++;
		} RADIX_WALK_END;
	}

	return INT2FIX(rn_count);
}

/*
 * call-seq:
 *   radix.keys -> array
 *
 * Returns an array of all the string keys in the database.
 */
static VALUE
rb_radix_keys(VALUE self)
{
	struct radixdata *radixp;
	radix_node_t *rn;
	VALUE ary;
	char prefix[256];
	unsigned int gen_id_cur;
	int i;

	GetRadix(self, radixp);

	ary = rb_ary_new();
	gen_id_cur = radixp->gen_id;

	for (i = 0; i < RTNUM; i++) {
		RADIX_WALK(radixp->rt[i]->head, rn) {
			RaiseModified(gen_id_cur, radixp->gen_id);
			if (rn->data != NULL) {
				prefix_ntop(rn->prefix, prefix, sizeof(prefix));
				rb_ary_push(ary, rb_tainted_str_new(
						    prefix, strlen(prefix)));
			}
		} RADIX_WALK_END;
	}

	return ary;
}

/*
 * call-seq:
 *   radix.values -> array
 *
 * Returns an array of all the string values in the database.
 */
static VALUE
rb_radix_values(VALUE self)
{
	struct radixdata *radixp;
	radix_node_t *rn;
	VALUE ary;
	unsigned int gen_id_cur;
	int i;

	GetRadix(self, radixp);

	ary = rb_ary_new();
	gen_id_cur = radixp->gen_id;

	for (i = 0; i < RTNUM; i++) {
		RADIX_WALK(radixp->rt[i]->head, rn) {
			RaiseModified(gen_id_cur, radixp->gen_id);
			if (rn->data != NULL) {
				rb_ary_push(ary, (VALUE)rn->data);
			}
		} RADIX_WALK_END;
	}

	return ary;
}

static void
rn_free_func(radix_node_t *rn, void *cbctx)
{
	rn->data = NULL;
}

/*
 * call-seq:
 *   radix.clear
 *
 * Deletes all data from the database.
 */
static VALUE
rb_radix_clear(VALUE self)
{
	struct radixdata *radixp;

	GetRadix(self, radixp);
	free_radixdata0(radixp);
	init_radixdata(radixp);

	return self;
}

/*
 * call-seq:
 * radix.each_key {|key| block} -> self
 *
 * Calls the block once for each key string in the database. Returns self.
 */
static VALUE
rb_radix_each_key(VALUE self)
{
	struct radixdata *radixp;
	radix_node_t *rn;
	char prefix[256];
	unsigned int gen_id_cur;
	int i;

	GetRadix(self, radixp);

	gen_id_cur = radixp->gen_id;

	for (i = 0; i < RTNUM; i++) {
		RADIX_WALK(radixp->rt[i]->head, rn) {
			RaiseModified(gen_id_cur, radixp->gen_id);
			if (rn->data != NULL) {
				prefix_ntop(rn->prefix, prefix, sizeof(prefix));
				rb_yield(rb_tainted_str_new(prefix,
							    strlen(prefix)));
			}
		} RADIX_WALK_END;
	}

	return self;
}

/*
 * call-seq:
 * radix.each_value {|val| block} -> self
 *
 * Calls the block once for each value string in the database. Returns self.
 */
static VALUE
rb_radix_each_value(VALUE self)
{
	struct radixdata *radixp;
	radix_node_t *rn;
	unsigned int gen_id_cur;
	int i;

	GetRadix(self, radixp);

	gen_id_cur = radixp->gen_id;

	for (i = 0; i < RTNUM; i++) {
		RADIX_WALK(radixp->rt[i]->head, rn) {
			RaiseModified(gen_id_cur, radixp->gen_id);
			if (rn->data != NULL) {
				rb_yield((VALUE)rn->data);
			}
		} RADIX_WALK_END;
	}

	return self;
}

/*
 * call-seq:
 * radix.each {|key, value| block} -> self
 *
 * Calls the block once for each [key, value] pair int the database.
 * Returns self.
 */
static VALUE
rb_radix_each_pair(VALUE self)
{
	struct radixdata *radixp;
	radix_node_t *rn;
	char prefix[256];
	unsigned int gen_id_cur;
	VALUE keystr;
	int i;

	GetRadix(self, radixp);

	gen_id_cur = radixp->gen_id;

	for (i = 0; i < RTNUM; i++) {
		RADIX_WALK(radixp->rt[i]->head, rn) {
			RaiseModified(gen_id_cur, radixp->gen_id);
			if (rn->data != NULL) {
				prefix_ntop(rn->prefix, prefix, sizeof(prefix));
				keystr = rb_tainted_str_new(prefix,
							    strlen(prefix));
				rb_yield(rb_assoc_new(keystr, (VALUE)rn->data));
			}
		} RADIX_WALK_END;
	}

	return self;
}

/*
 * call-seq:
 * radix.to_hash -> hash
 *
 * Converts the contents of the database to an in-memory Hash object, and
 * returns it.
 */
static VALUE
rb_radix_to_hash(VALUE self)
{
	struct radixdata *radixp;
	radix_node_t *rn;
	char prefix[256];
	unsigned int gen_id_cur;
	VALUE keystr;
	VALUE hash;
	int i;

	GetRadix(self, radixp);

	gen_id_cur = radixp->gen_id;

	hash = rb_hash_new();
	for (i = 0; i < RTNUM; i++) {
		RADIX_WALK(radixp->rt[i]->head, rn) {
			RaiseModified(gen_id_cur, radixp->gen_id);
			if (rn->data != NULL) {
				prefix_ntop(rn->prefix, prefix, sizeof(prefix));
				keystr = rb_tainted_str_new(prefix,
							    strlen(prefix));
				rb_hash_aset(hash, keystr, (VALUE)rn->data);
			}
		} RADIX_WALK_END;
	}

	return hash;
}

static void
closed_radix(void)
{
	rb_raise(rb_eRuntimeError, "closed Radix tree");
}

static VALUE
radix_s_alloc(VALUE klass)
{
	struct radixdata *radixp;

	radixp = newRadixData();
	if (radixp == NULL)
		rb_raise(rb_eRuntimeError, "Initialize error");

	Data_Wrap_Struct(klass, radix_mark, free_radixdata, radixp);
}

static void
rn_mark(struct radixnode *rn)
{
	rb_gc_mark(rn->msg);
}

static void
free_rn(struct radixnode *rn)
{
	xfree(rn);
}

static VALUE
radixnode_s_alloc(VALUE klass)
{
	struct radixnode *rn;
	VALUE obj;

	obj = Data_Make_Struct(klass, struct radixnode, rn_mark, free_rn, rn);
	rn->msg = Qnil;

	return obj;
}

static VALUE
rb_rn_msg_get(VALUE self)
{
	struct radixnode *rn;

	Data_Get_Struct(self, struct radixnode, rn);

	if (rn == NULL)
		return Qnil;

	return rn->msg;
}

static VALUE
rb_rn_prefix_get(VALUE self)
{
	struct radixnode *rn;
	char prefix[256];

	Data_Get_Struct(self, struct radixnode, rn);

	prefix_ntop(&rn->prefix, prefix, sizeof(prefix));

	return rb_tainted_str_new(prefix, strlen(prefix));
}

static VALUE
rb_rn_network_get(VALUE self)
{
	struct radixnode *rn;
	char prefix[256];

	Data_Get_Struct(self, struct radixnode, rn);

	prefix_addr_ntop(&rn->prefix, prefix, sizeof(prefix));

	return rb_tainted_str_new(prefix, strlen(prefix));
}

static VALUE
rb_rn_prefixlen_get(VALUE self)
{
	struct radixnode *rn;

	Data_Get_Struct(self, struct radixnode, rn);

	return INT2FIX(rn->prefix.bitlen);
}

static VALUE
rb_rn_af_get(VALUE self)
{
	struct radixnode *rn;
	int family;

	Data_Get_Struct(self, struct radixnode, rn);

	if (rn->prefix.family == AF_INET)
		family = 4;
	else
		family = 6;
	return INT2FIX(family);
}


/*
 * Documented by sogabe sogabe@iij.ad.jp
 * = Introduction
 *
 * The Radix class provides a wrapper to a Radix-style Database
 * Manager library.
 *
 * = Example
 *
 *  require 'radix'
 *  r = Radix.new
 *  node = r.add("192.168.0.0", 24)
 *  node = r.add("172.31.0.0/16")
 *  r["172.16.0.0/16"] = "Hello Radix!"
 *  node = r["172.16.0.1"]
 *  node.msg
 *  node.prefix
 *  node.prefixlen
 *  node.af
 *  node.network
 *  r.search_best("192.168.0.1", 32)
 *  r.search_exact("192.168.0.0/24")
 *  r.delete("192.168.0.0/24")
 *  r.each_pair do |k, v|
 *    print k, " ", v.msg, "\n"
 *  end
 */
void Init_radix()
{
	VALUE rb_cRadix;

	rb_cRadix = rb_define_class("Radix", rb_cObject);

	rb_cRadixNode = rb_define_class("RadixNode", rb_cObject);

	rb_define_alloc_func(rb_cRadix, radix_s_alloc);
	rb_define_alloc_func(rb_cRadixNode, radixnode_s_alloc);

	rb_define_method(rb_cRadix, "initialize", rb_radix_initialize, -1);
	rb_define_method(rb_cRadix, "add", rb_radix_add, -1);
	rb_define_method(rb_cRadix, "search_best", rb_radix_search_best, -1);
	rb_define_method(rb_cRadix, "search_exact", rb_radix_search_exact, -1);
	rb_define_method(rb_cRadix, "[]", rb_radix_aref, -1);
	rb_define_method(rb_cRadix, "[]=", rb_radix_add0, -1);
	rb_define_method(rb_cRadix, "store", rb_radix_add, -1);
	rb_define_method(rb_cRadix, "keys", rb_radix_keys, 0);
	rb_define_method(rb_cRadix, "values", rb_radix_values, 0);
	rb_define_method(rb_cRadix, "delete", rb_radix_delete, -1);
	rb_define_method(rb_cRadix, "clear", rb_radix_clear, 0);
	rb_define_method(rb_cRadix, "each_key", rb_radix_each_key, 0);
	rb_define_method(rb_cRadix, "each_value", rb_radix_each_value, 0);
	rb_define_method(rb_cRadix, "each_pair", rb_radix_each_pair, 0);
	rb_define_method(rb_cRadix, "length", rb_radix_length, 0);
	rb_define_method(rb_cRadix, "to_hash", rb_radix_to_hash, 0);

	rb_define_method(rb_cRadixNode, "msg", rb_rn_msg_get, 0);
	rb_define_method(rb_cRadixNode, "prefix", rb_rn_prefix_get, 0);
	rb_define_method(rb_cRadixNode, "network", rb_rn_network_get, 0);
	rb_define_method(rb_cRadixNode, "family", rb_rn_af_get, 0);
	rb_define_method(rb_cRadixNode, "prefixlen", rb_rn_prefixlen_get, 0);
}
