#include "contiki.h"
#include "net/routing/routing.h"
#include "random.h"
#include "net/netstack.h"
#include "sys/etimer.h"
#include "net/ipv6/uip.h"
#include "net/linkaddr.h"
#include "net/ipv6/uip-ds6.h"
#include "net/ipv6/simple-udp.h"
#include <stdint.h>
#include <inttypes.h>
#include "net/packetbuf.h"
#include <string.h>
#include <stdio.h> /* For printf() */
#include <stdlib.h> /* For atoi() */
#include <math.h>



#include "sys/node-id.h"
#include "net/mac/tsch/tsch.h"

/* Log configuration */
#include "sys/log.h"
#define LOG_MODULE "AnchorNode"
#define LOG_LEVEL LOG_LEVEL_INFO

#define DEBUG DEBUG_PRINT
#include "net/ipv6/uip-debug.h"

#define WITH_SERVER_REPLY  1
#define UDP_CLIENT_PORT	8765
#define UDP_SERVER_PORT	5678

#define A_REF -45.0 // Reference signal strength in dBm at 1m distance
#define N_FACTOR 2.0 // Signal propagation factor (varies depending on the environment)
#define MAX_ANCHORS 3
#define C 2


static double anchors[MAX_ANCHORS][MAX_ANCHORS] = { 
    {1.0, 20.0, 25.0}, 
    {2.0, 10.0, 8.0}, 
    {3.0, 1.0, 20.0} 
};

static double positions[MAX_ANCHORS][2];
static double distances[MAX_ANCHORS][2] = {
    {-1.0, -1.0},
    {-1.0, -1.0},
    {-1.0, -1.0}
};
static int16_t rssis[MAX_ANCHORS] = {-1, -1, -1};

static double coordinates[2][1] ;

static struct simple_udp_connection broadcast_connection;



 
static void printLocation( double point[][1]) {
  printf("\n Location:");
  
  double P1 = point[0][0];
  double P2 = point[1][0];
  if (P1 - (int)P1 == 0) {
      uint8_t number =(uint8_t)((point[0][0])*1000);
      int decimalPart = 0;
      uint8_t firstPart = number;
      printf("CoordinateX: %u.%d ", firstPart, decimalPart);
  } else {
      double number = point[0][0];
      uint8_t decimalPart =  (uint8_t)(((number - (int)number) * 10)*1000);
      uint8_t firstPart = (uint8_t)number;
      printf("CoordinateX: %u.%u ", firstPart, decimalPart);
  }
  if (P2 - (int)P2 == 0) {
      uint8_t number = (uint8_t)((point[1][0])*1000);
      int decimalPart = 0;
      uint8_t firstPart = number;
      printf("CoordinateY: %u.%d ", firstPart, decimalPart);
  } else {
      double number = point[1][0];
      uint8_t decimalPart =(uint8_t)(((number - (int)number) * 10)*1000);
      uint8_t firstPart = (uint8_t)number;
      printf("CoordinateY: %u.%u ", firstPart, decimalPart);
  }
}
static void matrix_inverse(double matrix[][2], double inverse[][2]) {

  double determinant = matrix[0][0] * matrix[1][1] - matrix[0][1] * matrix[1][0];

  if (determinant == 0) {
      printf("Tersi bulunamaz. Determinant sifir.\n");
  }

  double inverseDeterminant = 1.0 / determinant;

  inverse[0][0] =  matrix[1][1] * inverseDeterminant;
  inverse[0][1] = -matrix[0][1] * inverseDeterminant;
  inverse[1][0] = -matrix[1][0] * inverseDeterminant;
  inverse[1][1] =  matrix[0][0] * inverseDeterminant;
}

static void matmul(double a[][2], double b[][1], double c[][1]) {
  int i, j, k;

  for (i = 0; i < 2; i++) {
    for (j = 0; j < 1; j++) {
      c[i][j] = 0;
      for (k = 0; k < 2; k++) {
        c[i][j] += a[i][k] * b[k][j];
      }
    }
  }
}

static void trilateration(double positions[][2], double distances[][2], double point[][1]){

  double inversedA[2][2];
  double x1 = positions[0][0];
  double y1 = positions[0][1];
  double x2 = positions[1][0];
  double y2 = positions[1][1];
  double x3 = positions[2][0];
  double y3 = positions[2][1];
 

  double A[2][2]={
    {2*(x1-x3),2*(y1-y3)},
    {2*(x2-x3),2*(y2-y3)}
  };

  matrix_inverse(A, inversedA);

  double B[2][1]={
    {x1*x1-x3*x3+y1*y1-y3*y3+distances[2][1]*distances[2][1]-distances[0][1]*distances[0][1]},
    {x2*x2-x3*x3+y2*y2-y3*y3+distances[2][1]*distances[2][1]-distances[1][1]*distances[1][1]}

  };
  
  matmul(inversedA,B,point);

  printLocation(point);
}

/*---------------------------------------------------------------------------*/
// Function to calculate the distance from RSSI value
static double calculate_distance(double rssi)
{
  double distance = powf(10.0, ((A_REF - rssi) / (10.0 * N_FACTOR))+C);
  return distance;
}

/*---------------------------------------------------------------------------*/
PROCESS(broadcast_example_process, "Server");
AUTOSTART_PROCESSES(&broadcast_example_process);
/*---------------------------------------------------------------------------*/
static void receiver(struct simple_udp_connection *c,
         const uip_ipaddr_t *sender_addr,
         uint16_t sender_port,
         const uip_ipaddr_t *receiver_addr,
         uint16_t receiver_port,
         const uint8_t *data,
         uint16_t datalen)
{
  if(strncmp((char*)data, "Node ID:", 8) == 0){
    int node_id = atoi((char*)(data+strlen("Node ID:")));
    
    uint8_t hop_count = uip_ds6_if.cur_hop_limit - UIP_IP_BUF->ttl ;
    LOG_INFO("Hop count to this node: %u\n", hop_count);

    if(hop_count==0){
      int16_t rssi = packetbuf_attr(PACKETBUF_ATTR_RSSI);
      double d_rssi= rssi;
      

      for(int i=0;i<MAX_ANCHORS;i++){
                  
        if(rssis[i]==-1){
          rssis[i] =rssi;
        }
        if(distances[i][1]==-1.0){

          distances[i][0] = node_id;
          distances[i][1] = calculate_distance(d_rssi);
          printf("The distance was calculated between both nodes: %d ",(int)distances[i][1] );
          break;

        }
      }
      printf("Anchor's Node ID: %d", node_id);
      printf("RSSI: %d", rssi);

    if(rssis[2]!=-1){
        for(int i=0;i<MAX_ANCHORS;i++){
          if (distances[i][0] == anchors[0][0]) {
            positions[0][0] = anchors[0][1];
            positions[0][1] = anchors[0][2];
          }
          else if (distances[i][0] == anchors[1][0]) {
            positions[1][0] = anchors[1][1];
            positions[1][1] = anchors[1][2];
          }
          else {
            positions[2][0] = anchors[2][1];
            positions[2][1] = anchors[2][2];
          }

        }
        
        trilateration(positions, distances, coordinates);

        distances[0][0] = -1.0;
        distances[0][1] = -1.0;
        distances[1][0] = -1.0;
        distances[1][1] = -1.0;
        distances[2][0] = -1.0;
        distances[2][1] = -1.0;

        rssis[0] = -1;
        rssis[1] = -1;
        rssis[2] = -1;
      }             
    }   
  }
}
/*---------------------------------------------------------------------------*/
PROCESS_THREAD(broadcast_example_process, ev, data)
{
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
  simple_udp_register(&broadcast_connection, UDP_SERVER_PORT, NULL, UDP_CLIENT_PORT, receiver);

  while(1) {
    PROCESS_WAIT_EVENT();
  }
  
  PROCESS_END();
}
