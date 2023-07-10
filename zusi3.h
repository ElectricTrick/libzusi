#pragma once

#ifndef ZUSI
#define ZUSI

#include "EthernetInterface.h"
#include "mbed.h"
#include "rtos.h"
#include <cstdint>
#include <zusi3tcp.h>

#define INPUT_BUFFER_SIZE 1024
#define OUTPUT_BUFFER_SIZE 512

#define STATE_CLOSED 0
#define STATE_OPEN 1
#define STATE_HELLO 2
#define STATE_ACK_HELLO 3
#define STATE_NEEDED_DATA 5
#define STATE_ACK_NEEDED 6
#define STATE_OPERATION 7
#define STATE_DISPOSE 8
#define STATE_SHUTDOWN 9

typedef enum {
    status_closed = 0,
    status_connecting = 1,
    status_online = 2,
    status_faulty = 3,
} client_status;

class ZusiClient {
public:
/** Creates a ZusiClient object
 * Specify client name+version

*/
  ZusiClient(const char *my_name, const char *my_version,
             z3_data_notify data_callback);

  bool add_needed_data(word subgroup, word id, void *target);
  bool start(EthernetInterface *link, const char *server_ip, uint16_t port);
  void stop();
  client_status get_status();

private:
  void state_machine();
  void tcp_reader();
  bool socket_connect();
  bool send_hello_mesg();
  bool send_needed_data_mesg();
  void set_status(byte value);

  EthernetInterface *eth;
  TCPSocket *sock;
  const char *server_ip;
  uint16_t server_port;
  const char *client_name;
  const char *client_ver;
  zusi_data *z3data;
  Thread *main_thread;
  Thread *inbox_thread;
  byte status;
  bool run;
  DigitalOut ledRx, ledTx;
};

#endif // ZUSI