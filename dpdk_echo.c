#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <math.h>
#include <errno.h>
#include <stdio.h>
#include <getopt.h>
#include <stdlib.h>
#include <stdint.h>

#include <rte_ip.h>
#include <rte_eal.h>
#include <rte_log.h>
#include <rte_tcp.h>
#include <rte_flow.h>
#include <rte_mbuf.h>
#include <rte_ring.h>
#include <rte_ether.h>
#include <rte_errno.h>
#include <rte_atomic.h>
#include <rte_ethdev.h>
#include <rte_malloc.h>
#include <rte_mempool.h>

#define BURST_SIZE 				32
#define RING_ELEMENTS 			512*1024
#define MEMPOOL_CACHE_SIZE 		512
#define PKTMBUF_POOL_ELEMENTS 	512*1024 - 1

uint16_t nr_cores = 1;

// Convert string type into int type
static uint16_t process_int_arg(const char *arg) {
	char *end = NULL;

	return strtoul(arg, &end, 10);
}

static inline void swap_eth_ip_batch(struct rte_mbuf **pkts, uint16_t n) {
    for(uint16_t i = 0; i < n; i++) {
        struct rte_mbuf *pkt = pkts[i];

        struct rte_ether_addr tmp;
        struct rte_ether_hdr *eth_hdr = (struct rte_ether_hdr *) rte_pktmbuf_mtod(pkt, struct ether_hdr*);
        tmp = eth_hdr->dst_addr;
        eth_hdr->dst_addr = eth_hdr->src_addr;
        eth_hdr->src_addr = tmp;

		rte_be32_t tmp2;
		struct rte_ipv4_hdr *ip_hdr = rte_pktmbuf_mtod_offset(pkt, struct rte_ipv4_hdr *, sizeof(struct rte_ether_hdr));
        tmp2 = ip_hdr->dst_addr;
        ip_hdr->dst_addr = ip_hdr->src_addr;
        ip_hdr->src_addr = tmp2;
    }
}

static int lcore_echo_fn(void *arg) {
    uint32_t portid = 0;
    uint16_t qid = *((uint16_t*) arg);
    struct rte_mbuf *pkts[BURST_SIZE];

    while(1) {
        // retrieve the packets from the NIC
        uint16_t nb_rx = rte_eth_rx_burst(portid, qid, pkts, BURST_SIZE);

        // swap the ethernet and IP
        swap_eth_ip_batch(pkts, nb_rx);

        // send the packets
        rte_eth_tx_burst(portid, qid, pkts, nb_rx);
	}

    return 0;
}

static inline int init_dpdk(uint16_t nr_queues) {
    // allocate the packet pool
	char s[64];
	snprintf(s, sizeof(s), "mbuf_pool");
	struct rte_mempool *pktmbuf_pool = rte_pktmbuf_pool_create(s, PKTMBUF_POOL_ELEMENTS, MEMPOOL_CACHE_SIZE, 0,	RTE_MBUF_DEFAULT_BUF_SIZE, rte_socket_id());

	if(pktmbuf_pool == NULL) {
		rte_exit(EXIT_FAILURE, "Cannot init mbuf pool on socket %d\n", rte_socket_id());
	}

    // configurable number of RX/TX queues
    uint16_t nb_rx_queues = nr_queues;
    uint16_t nb_tx_queues = nr_queues;

    // configurable number of RX/TX ring descriptors
	uint16_t nb_rxd = 4096;
	uint16_t nb_txd = 4096;

	// get default port_conf
	struct rte_eth_conf port_conf = {
		.rxmode = {
			.mq_mode = nb_rx_queues > 1 ? RTE_ETH_MQ_RX_RSS : RTE_ETH_MQ_RX_NONE,
			.max_lro_pkt_size = RTE_ETHER_MAX_LEN,
			.offloads = RTE_ETH_RX_OFFLOAD_CHECKSUM,
		},
		// .rx_adv_conf = {
		// 	.rss_conf = {
		// 		.rss_key = NULL,
		// 		.rss_hf = RTE_ETH_RSS_TCP,
		// 	},
		// },
		.txmode = {
			.mq_mode = RTE_ETH_MQ_TX_NONE,
			// .offloads = RTE_ETH_TX_OFFLOAD_TCP_CKSUM|RTE_ETH_TX_OFFLOAD_IPV4_CKSUM|RTE_ETH_TX_OFFLOAD_MBUF_FAST_FREE,
		},
	};

	// configure the NIC
    uint32_t portid = 1;
	int retval = rte_eth_dev_configure(portid, nb_rx_queues, nb_tx_queues, &port_conf);
	if(retval != 0) {
		return retval;
	}

	// adjust and set up the number of RX/TX descriptors
	retval = rte_eth_dev_adjust_nb_rx_tx_desc(portid, &nb_rxd, &nb_txd);
	if(retval != 0) {
		return retval;
	}

	// setup the RX queues
	for(int q = 0; q < nb_rx_queues; q++) {
		retval = rte_eth_rx_queue_setup(portid, q, nb_rxd, rte_eth_dev_socket_id(portid), NULL, pktmbuf_pool);
		if (retval < 0) {
			return retval;
		}
	}

	// setup the TX queues
	for(int q = 0; q < nb_tx_queues; q++) {
		retval = rte_eth_tx_queue_setup(portid, q, nb_txd, rte_eth_dev_socket_id(portid), NULL);
		if (retval < 0) {
			return retval;
		}
	}

	// start the Ethernet port
	retval = rte_eth_dev_start(portid);
	if(retval < 0) {
		return retval;
	}

	return 0;
}

// Parse the argument given in the command line of the application
int app_parse_args(int argc, char **argv) {
	int opt, ret;
	char **argvopt;
	char *prgname = argv[0];

	argvopt = argv;
	while ((opt = getopt(argc, argvopt, "n:")) != EOF) {
		switch (opt) {
		// number of cores
		case 'n':
            nr_cores = process_int_arg(optarg);
			break;

		default:
			rte_exit(EXIT_FAILURE, "Invalid arguments.\n");
		}
	}

	if(optind >= 0) {
		argv[optind - 1] = prgname;
	}

	ret = optind-1;
	optind = 1;

	return ret;
}

int main(int argc, char **argv) {
    int ret = rte_eal_init(argc, argv);
	if(ret < 0) {
		rte_exit(EXIT_FAILURE, "Invalid EAL parameters\n");
	}
	argc -= ret;
	argv += ret;

	ret = app_parse_args(argc, argv);
	if(ret < 0) {
		rte_exit(EXIT_FAILURE, "Invalid arguments\n");
	}

	init_dpdk(nr_cores);

    uint32_t id_lcore = rte_lcore_id();	
	for(uint16_t q = 0; q < nr_cores; q++) {
		id_lcore = rte_get_next_lcore(id_lcore, 1, 1);
		rte_eal_remote_launch(lcore_echo_fn, (void*) &q, id_lcore);
	}

    // wait for the threads
	uint32_t lcore_id;
	RTE_LCORE_FOREACH_WORKER(lcore_id) {
		if(rte_eal_wait_lcore(lcore_id) < 0) {
			return -1;
		}
	}

    return 0;
}
