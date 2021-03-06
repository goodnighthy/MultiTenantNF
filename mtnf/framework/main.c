#include <stdint.h>
#include <stdio.h>
#include <signal.h>
#include <stdbool.h>
#include <unistd.h>

#include <rte_mbuf.h>
#include <rte_ethdev.h>
#include <rte_lcore.h>
#include <rte_launch.h>
#include <rte_log.h>
#include <rte_prefetch.h>
#include <rte_branch_prediction.h>

#include "mtnf_help.h"

#include "mtnf_common.h"
#include "mtnf_args.h"
#include "mtnf_tenant.h"
#include "mtnf_init.h"
#include "mtnf_stats.h"

#define RTE_LOGTYPE_MTNF          RTE_LOGTYPE_USER1
#define MAX_PKT_BURST 32
#define MAX_PKT_BUFFER 32
#define BUFFER_SIZE 128

/* test time latency */
/*
#include <sys/time.h>
#define TIMESLOT_NUM 10000
static unsigned long process_time_slot[TIMESLOT_NUM];
static unsigned long buffer_time_slot[TIMESLOT_NUM];
static unsigned long start_usec, end_usec;
static int buffer_odd_cnt;
*/

static volatile bool keep_running = 1;

static void
handle_signal(int sig) {
    if (sig == SIGINT || sig == SIGTERM) {
        keep_running = 0;
    }
}

static int
worker_thread(void *arg) {
    uint8_t port_id, tenant_id;
	uint16_t i, nb_rx, nb_tx, nb_handler;
	struct rte_mbuf *pkts[PACKET_READ_SIZE];
	struct worker_info *worker_info = (struct worker_info *)arg;

    /* test time latency */
    /*
    struct timeval start[2], end_process;
    buffer_odd_cnt = -1;
    */
    
	port_id = ports->id[worker_info->id];

	RTE_LOG(INFO, MTNF, "Core %d: Running worker thread\n", rte_lcore_id());

	for (; keep_running;) {
		nb_rx = rte_eth_rx_burst(port_id, 0, pkts, MAX_PKT_BURST);
		ports->stats[port_id].rx += nb_rx;

        for (i = 0; i < nb_rx; i++) {
            rte_prefetch0(rte_pktmbuf_mtod(pkts[i], void *));
            
//            mtnf_pkt_print(pkts[i]);

            tenant_id = get_tenant_id(pkts[i]);
            buffers[tenant_id].buffer_slot[buffers[tenant_id].num++] = pkts[i];

            if (buffers[tenant_id].num == MAX_PKT_BUFFER) {
                /* test time latenct */
                /* get buffer time */
                /*
                if (buffer_odd_cnt == -1) {
                    gettimeofday(&start[1], NULL);
                } else {
                    gettimeofday(&start[buffer_odd_cnt % 2], NULL);
                    start_usec = start[(buffer_odd_cnt + 1) % 2].tv_usec;
                    end_usec = start[buffer_odd_cnt % 2].tv_usec;
                    if (start_usec > end_usec)
                        end_usec += 1000000;
                    buffer_time_slot[buffer_odd_cnt] = (end_usec - start_usec);
                }
                buffer_odd_cnt = (buffer_odd_cnt + 1) % TIMESLOT_NUM;
                */

                tenants[tenant_id].stats.rx += buffers[tenant_id].num;

                /* handle by network function */
                nb_handler = mtnf_packets_handler(&buffers[tenant_id], tenant_id);

                /* get the time of processing */
                /*
                gettimeofday(&end_process, NULL);
                end_usec = end_process.tv_usec;
                start_usec = start[(buffer_odd_cnt + 1) % 2].tv_usec;
                if (start_usec > end_usec)
                    end_usec += 1000000;
                process_time_slot[buffer_odd_cnt] = (end_usec - start_usec);
                */

                tenants[tenant_id].stats.tx += nb_handler;

//                pktmbuf_free_bulk(&buffers[tenant_id].buffer_slot[0], nb_handler);
                nb_tx = rte_eth_tx_burst(port_id, 0, buffers[tenant_id].buffer_slot, nb_handler);
                if (unlikely(nb_tx < nb_handler)) {
                    pktmbuf_free_bulk(&buffers[tenant_id].buffer_slot[nb_tx], nb_handler - nb_tx);
                    ports->stats[port_id].tx_drop += (nb_handler - nb_tx);
                }
                ports->stats[port_id].tx += nb_tx;
            }
        }

		
	}

	RTE_LOG(INFO, MTNF, "Core %d: worker thread done\n", rte_lcore_id());

	return 0;
}

static int
master_thread(void) {
	const uint64_t sleeptime = timer_period;

	RTE_LOG(INFO, MTNF, "Core %d: Running master thread\n", rte_lcore_id());

	/* Initial pause so above printf is seen */
    sleep(3);

    /* Loop forever: sleep always returns 0 or <= param */
    while (keep_running && sleep(sleeptime) <= sleeptime) {
        mtnf_stats_display_all(sleeptime, ports, tenants);
        /* test time latency */
        /*
        int i;
        uint64_t buffer_sum = 0, process_sum = 0;
        for (i = 0; i < buffer_odd_cnt; i ++) {
            buffer_sum += buffer_time_slot[i];
            process_sum += process_time_slot[i];
        }
        printf("buffer latency: %f, process latency: %f", \
            (double)buffer_sum / buffer_odd_cnt, (double)process_sum / buffer_odd_cnt);
        */
    }

    RTE_LOG(INFO, MTNF, "Core %d: Master thread done\n", rte_lcore_id());

    return 0;
}

int
main(int argc, char *argv[]) {
	unsigned cur_lcore, worker_lcores;
	unsigned i;

	/* init the system */
	if (init(argc, argv) < 0)
		return -1;
	RTE_LOG(INFO, MTNF, "Finished Process Init.\n");

	cur_lcore = rte_lcore_id();
    worker_lcores = rte_lcore_count() - 1;

    RTE_LOG(INFO, MTNF, "%d cores available in total\n", rte_lcore_count());
    RTE_LOG(INFO, MTNF, "%d cores available for worker\n", worker_lcores);
    RTE_LOG(INFO, MTNF, "%d cores available for master\n", 1);

	/* Listen for ^C and docker stop so we can exit gracefully */
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    /* Launch each worker thread on cores */
    for (i = 0; i < worker_lcores; i++) {
            struct worker_info *worker_info = calloc(1, sizeof(struct worker_info));
            worker_info->id = i;
            cur_lcore = rte_get_next_lcore(cur_lcore, 1, 1);
            if (rte_eal_remote_launch(worker_thread, (void*)worker_info,  cur_lcore) == -EBUSY) {
                    RTE_LOG(ERR, MTNF, "Core %d is already busy, can't use for worker %d\n", cur_lcore, worker_info->id);
                    return -1;
            }
    }

    /* Master thread handles statistics and NF management */
    master_thread();

    return 0;
}