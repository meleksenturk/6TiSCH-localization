#include "contiki.h"
#include "net/routing/routing.h"
#include "net/netstack.h"
#include "sys/etimer.h"
#include "net/ipv6/uip.h"
#include "net/linkaddr.h"
#include "net/ipv6/simple-udp.h"
#include <stdint.h>
#include <stdio.h> /* For printf() */
#include "sys/node-id.h"
#include "net/mac/tsch/tsch.h"

#include "sys/log.h"
#define LOG_MODULE "Node"
#define LOG_LEVEL LOG_LEVEL_INFO

#define DEBUG DEBUG_PRINT
#include "net/ipv6/uip-debug.h"

#define WITH_SERVER_REPLY  1
#define UDP_CLIENT_PORT	8765
#define UDP_SERVER_PORT	5678

#define SEND_INTERVAL       (20 * CLOCK_SECOND)
#define SEND_TIME       (random_rand() % (SEND_INTERVAL))

static struct simple_udp_connection broadcast_connection;

PROCESS(broadcast_example_process, "Client");
AUTOSTART_PROCESSES(&broadcast_example_process);


PROCESS_THREAD(broadcast_example_process, ev, data)
{
  static struct etimer periodic_timer;
  char message[32];
  uip_ipaddr_t addr; //dest
  static uint32_t tx_count;
  int is_coordinator;

  PROCESS_BEGIN();

  is_coordinator = 0;
  
  #if CONTIKI_TARGET_COOJA || CONTIKI_TARGET_SKY
    is_coordinator = (node_id == 1);
  #endif

  if(is_coordinator) {  /* Running on the root? */
    NETSTACK_ROUTING.root_start();
  }
  NETSTACK_MAC.on();
  
  /* Initialize UDP connection */
  simple_udp_register(&broadcast_connection, UDP_CLIENT_PORT, NULL, UDP_SERVER_PORT, NULL);
  
  etimer_set(&periodic_timer, SEND_INTERVAL);
  
  while(1) { 
    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&periodic_timer));       
    if(NETSTACK_ROUTING.node_is_reachable() && NETSTACK_ROUTING.get_root_ipaddr(&addr)) {
      printf("Sending broadcast %lu\n", (unsigned long)tx_count);
      snprintf(message,sizeof(message), "Node ID:%d", node_id );
      uip_create_linklocal_allnodes_mcast(&addr);
      simple_udp_sendto(&broadcast_connection, message , strlen(message) , &addr);
      tx_count++;
    } else {
      LOG_INFO("Not reachable yet\n");
    }
    etimer_set(&periodic_timer, SEND_INTERVAL);
  }
  PROCESS_END();
}