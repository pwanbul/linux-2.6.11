#ifndef _IPV6_H
#define _IPV6_H

#include <linux/config.h>
#include <linux/in6.h>
#include <asm/byteorder.h>

/* The latest drafts declared increase in minimal mtu up to 1280. */

#define IPV6_MIN_MTU	1280

/*
 *	Advanced API
 *	source interface/address selection, source routing, etc...
 *	*under construction*
 */


struct in6_pktinfo {
	struct in6_addr	ipi6_addr;
	int		ipi6_ifindex;
};


struct in6_ifreq {
	struct in6_addr	ifr6_addr;
	__u32		ifr6_prefixlen;
	int		ifr6_ifindex; 
};

#define IPV6_SRCRT_STRICT	0x01	/* this hop must be a neighbor	*/
#define IPV6_SRCRT_TYPE_0	0	/* IPv6 type 0 Routing Header	*/

/*
 *	routing header
 */
struct ipv6_rt_hdr {
	__u8		nexthdr;
	__u8		hdrlen;
	__u8		type;
	__u8		segments_left;

	/*
	 *	type specific data
	 *	variable length field
	 */
};


struct ipv6_opt_hdr {
	__u8 		nexthdr;
	__u8 		hdrlen;
	/* 
	 * TLV encoded option data follows.
	 */
};

#define ipv6_destopt_hdr ipv6_opt_hdr
#define ipv6_hopopt_hdr  ipv6_opt_hdr

#ifdef __KERNEL__
#define ipv6_optlen(p)  (((p)->hdrlen+1) << 3)
#endif

/*
 *	routing header type 0 (used in cmsghdr struct)
 */

struct rt0_hdr {
	struct ipv6_rt_hdr	rt_hdr;
	__u32			bitmap;		/* strict/loose bit map */
	struct in6_addr		addr[0];

#define rt0_type		rt_hdr.type
};

struct ipv6_auth_hdr {
	__u8  nexthdr;
	__u8  hdrlen;           /* This one is measured in 32 bit units! */
	__u16 reserved;
	__u32 spi;
	__u32 seq_no;           /* Sequence number */
	__u8  auth_data[0];     /* Length variable but >=4. Mind the 64 bit alignment! */
};

struct ipv6_esp_hdr {
	__u32 spi;
	__u32 seq_no;           /* Sequence number */
	__u8  enc_data[0];      /* Length variable but >=8. Mind the 64 bit alignment! */
};

struct ipv6_comp_hdr {
	__u8 nexthdr;
	__u8 flags;
	__u16 cpi;
};

/*
 *	IPv6 固定头部
 *
 *	当心，这是不正确的。 flow_lbl的前4位现在粘在优先级上，形成“类”。
 *
 *	ipv6首部
 */
struct ipv6hdr {
#if defined(__LITTLE_ENDIAN_BITFIELD)
	__u8			priority:4,
				version:4;
#elif defined(__BIG_ENDIAN_BITFIELD)
	__u8			version:4,		// 版本号
				priority:4;
#else
#error	"Please fix <asm/byteorder.h>"
#endif
	__u8			flow_lbl[3];		// 流标签

	__u16			payload_len;		// 载荷长度，不含首部
	__u8			nexthdr;			// 下一个头首部
	__u8			hop_limit;			// time to live

	struct	in6_addr	saddr;		// 128位源IP地址
	struct	in6_addr	daddr;		// 128位目的IP地址
};

/*
 * This structure contains configuration options per IPv6 link.
 */
struct ipv6_devconf {
	__s32		forwarding;
	__s32		hop_limit;
	__s32		mtu6;
	__s32		accept_ra;
	__s32		accept_redirects;
	__s32		autoconf;
	__s32		dad_transmits;
	__s32		rtr_solicits;
	__s32		rtr_solicit_interval;
	__s32		rtr_solicit_delay;
	__s32		force_mld_version;
#ifdef CONFIG_IPV6_PRIVACY
	__s32		use_tempaddr;
	__s32		temp_valid_lft;
	__s32		temp_prefered_lft;
	__s32		regen_max_retry;
	__s32		max_desync_factor;
#endif
	__s32		max_addresses;
	void		*sysctl;
};

/* index values for the variables in ipv6_devconf */
enum {
	DEVCONF_FORWARDING = 0,
	DEVCONF_HOPLIMIT,
	DEVCONF_MTU6,
	DEVCONF_ACCEPT_RA,
	DEVCONF_ACCEPT_REDIRECTS,
	DEVCONF_AUTOCONF,
	DEVCONF_DAD_TRANSMITS,
	DEVCONF_RTR_SOLICITS,
	DEVCONF_RTR_SOLICIT_INTERVAL,
	DEVCONF_RTR_SOLICIT_DELAY,
	DEVCONF_USE_TEMPADDR,
	DEVCONF_TEMP_VALID_LFT,
	DEVCONF_TEMP_PREFERED_LFT,
	DEVCONF_REGEN_MAX_RETRY,
	DEVCONF_MAX_DESYNC_FACTOR,
	DEVCONF_MAX_ADDRESSES,
	DEVCONF_FORCE_MLD_VERSION,
	DEVCONF_MAX
};

#ifdef __KERNEL__
#include <linux/in6.h>          /* struct sockaddr_in6 */
#include <linux/icmpv6.h>
#include <net/if_inet6.h>       /* struct ipv6_mc_socklist */
#include <linux/tcp.h>
#include <linux/udp.h>

/* 
   This structure contains results of exthdrs parsing
   as offsets from skb->nh.
 */

struct inet6_skb_parm {
	int			iif;
	__u16			ra;
	__u16			hop;
	__u16			dst0;
	__u16			srcrt;
	__u16			dst1;
};

#define IP6CB(skb)	((struct inet6_skb_parm*)((skb)->cb))

/**
 * struct ipv6_pinfo - ipv6 private area
 *
 * In the struct sock hierarchy (tcp6_sock, upd6_sock, etc)
 * this _must_ be the last member, so that inet6_sk_generic
 * is able to calculate its offset from the base struct sock
 * by using the struct proto->slab_obj_size member. -acme
 */
struct ipv6_pinfo {
	struct in6_addr 	saddr;
	struct in6_addr 	rcv_saddr;
	struct in6_addr		daddr;
	struct in6_addr		*daddr_cache;

	__u32			flow_label;
	__u32			frag_size;
	int			hop_limit;
	int			mcast_hops;
	int			mcast_oif;

	/* pktoption flags */
	union {
		struct {
			__u8	srcrt:2,
			        rxinfo:1,
				rxhlim:1,
				hopopts:1,
				dstopts:1,
                                rxflow:1;
		} bits;
		__u8		all;
	} rxopt;

	/* sockopt flags */
	__u8			mc_loop:1,
	                        recverr:1,
	                        sndflow:1,
				pmtudisc:2,
				ipv6only:1;

	struct ipv6_mc_socklist	*ipv6_mc_list;
	struct ipv6_ac_socklist	*ipv6_ac_list;
	struct ipv6_fl_socklist *ipv6_fl_list;
	__u32			dst_cookie;

	struct ipv6_txoptions	*opt;
	struct sk_buff		*pktoptions;
	struct {
		struct ipv6_txoptions *opt;
		struct rt6_info	*rt;
		int hop_limit;
	} cork;
};

/* WARNING: don't change the layout of the members in {raw,udp,tcp}6_sock! */
struct raw6_sock {
	/* inet_sock has to be the first member of raw6_sock */
	struct inet_sock	inet;
	__u32			checksum;	/* perform checksum */
	__u32			offset;		/* checksum offset  */
	struct icmp6_filter	filter;
	/* ipv6_pinfo has to be the last member of raw6_sock, see inet6_sk_generic */
	struct ipv6_pinfo	inet6;
};

struct udp6_sock {
	struct udp_sock	  udp;
	/* ipv6_pinfo has to be the last member of udp6_sock, see inet6_sk_generic */
	struct ipv6_pinfo inet6;
};

struct tcp6_sock {
	struct tcp_sock	  tcp;
	/* ipv6_pinfo has to be the last member of tcp6_sock, see inet6_sk_generic */
	struct ipv6_pinfo inet6;
};

#if defined(CONFIG_IPV6) || defined(CONFIG_IPV6_MODULE)
static inline struct ipv6_pinfo * inet6_sk(const struct sock *__sk)
{
	return inet_sk(__sk)->pinet6;
}

static inline struct raw6_sock *raw6_sk(const struct sock *sk)
{
	return (struct raw6_sock *)sk;
}

static inline void inet_sk_copy_descendant(struct sock *sk_to,
					   const struct sock *sk_from)
{
	int ancestor_size = sizeof(struct inet_sock);

	if (sk_from->sk_family == PF_INET6)
		ancestor_size += sizeof(struct ipv6_pinfo);

	__inet_sk_copy_descendant(sk_to, sk_from, ancestor_size);
}

#define __ipv6_only_sock(sk)	(inet6_sk(sk)->ipv6only)
#define ipv6_only_sock(sk)	((sk)->sk_family == PF_INET6 && __ipv6_only_sock(sk))
#else
#define __ipv6_only_sock(sk)	0
#define ipv6_only_sock(sk)	0

static inline struct ipv6_pinfo * inet6_sk(const struct sock *__sk)
{
	return NULL;
}

static inline struct raw6_sock *raw6_sk(const struct sock *sk)
{
	return NULL;
}

#endif

#endif

#endif
