#include "zusi3.h"
#include <zusi3tcp.h>

ZusiClient::ZusiClient(const char *my_name, const char *my_version,
                       z3_data_notify data_callback) {
  this->run = false;
  this->client_name = my_name;
  this->client_ver = my_version;
  this->sock = NULL;
  this->z3data = new zusi_data{0};
  this->status = STATE_CLOSED;
  this->main_thread = new Thread();
  this->inbox_thread = new Thread();
  this->sock = new TCPSocket();
  z3_init(this->z3data, INPUT_BUFFER_SIZE, OUTPUT_BUFFER_SIZE, data_callback);
}

bool ZusiClient::start(EthernetInterface *link, const char *server_ip,
                       uint16_t port) {

  debug("ZusiClient::start\r\n");
  this->eth = link;
  this->server_ip = server_ip;
  this->server_port = port;
  this->run = true;
  this->main_thread->start(callback(this, &ZusiClient::state_machine));

  return (true);
}

void ZusiClient::stop() {
  debug("ZusiClient::stop\r\n");
  this->set_status(STATE_DISPOSE);
  this->run = false;
}

void ZusiClient::state_machine() {

  debug("state_machine thread started\r\n");

  bool ret = false;

  while (this->run) {
    switch (this->status) {
    case STATE_CLOSED:
      ret = this->socket_connect();
      if (ret)
        this->set_status(STATE_OPEN);
      else
        ThisThread::sleep_for(5s);
      break;
    case STATE_OPEN:
      this->inbox_thread->start(callback(this, &ZusiClient::tcp_reader));
      this->set_status(STATE_HELLO);
      break;
    case STATE_HELLO:
      ret = this->send_hello_mesg();
      if (ret)
        this->set_status(STATE_ACK_HELLO);
      else
        this->set_status(STATE_DISPOSE);
      break;
    case STATE_ACK_HELLO:
      if (this->z3data->status >= z3_ack_hello_ok)
        this->set_status(STATE_NEEDED_DATA);
      break;
    case STATE_NEEDED_DATA:
      ret = this->send_needed_data_mesg();
      if (ret)
        this->set_status(STATE_ACK_NEEDED);
      else
        this->set_status(STATE_DISPOSE);
      break;
    case STATE_ACK_NEEDED:
      if (this->z3data->status >= z3_ack_needed_data_ok)
        this->set_status(STATE_OPERATION);
      break;
    case STATE_DISPOSE:
      this->set_status(STATE_CLOSED);
      if (this->inbox_thread->Running)
        this->inbox_thread->join();
      this->sock->close();
      ThisThread::sleep_for(5s);
      break;
    }

    ThisThread::sleep_for(100ms);
  }

  debug("state_machine thread ended\r\n");
}

client_status ZusiClient::get_status() {
  if (this->status == STATE_CLOSED)
    return status_closed;
  else if (this->status == STATE_OPERATION)
    return status_online;
  else if (this->status < STATE_OPERATION)
    return status_connecting;
  else
    return status_faulty;
}

void ZusiClient::set_status(byte value) {
  debug("Status from %d to %d\r\n", this->status, value);
  this->status = value;
}

bool ZusiClient::socket_connect() {

  debug("ZusiClient::socket_connect\r\n");

  SocketAddress a;
  a.set_ip_address(this->server_ip);
  a.set_port(this->server_port);
  this->sock->set_blocking(true);
  this->sock->set_timeout(5000);

  if (this->sock->open(this->eth) != NSAPI_ERROR_OK) {
    debug("Failed to open socket\r\n");
    this->sock->close();
    return (false);
  }

  debug("Connecting to %s:%d...\r\n", this->server_ip, this->server_port);

  nsapi_error_t ret = this->sock->connect(a);
  if (ret != NSAPI_ERROR_OK) {
    debug("Connection error -%d\r\n", ret);
    this->sock->close();
    return (false);
  }

  return (true);
}

bool ZusiClient::send_hello_mesg() {
  debug("Sending HELLO message\r\n");

  zusi_hello_msg(this->z3data, 0x02, this->client_name, this->client_ver);

  nsapi_size_or_error_t len = this->sock->send(
      (char *)z3_get_send_buffer(this->z3data), z3_bytes_sent(this->z3data, 0));

  if (len <= 0) {
    debug("Error sending HELLO message %d\r\n", len);
    return (false);
  }

  z3_bytes_sent(this->z3data, (word)len);

  return (true);
}

bool ZusiClient::send_needed_data_mesg() {
  debug("Sending NEEDED_DATA message\r\n");

  zusi_needed_data_msg(this->z3data);

  nsapi_size_or_error_t len = this->sock->send(
      (char *)z3_get_send_buffer(this->z3data), z3_bytes_sent(this->z3data, 0));

  if (len <= 0) {
    debug("Error sending NEEDED_DATA message - %d\r\n", len);
    return (false);
  }

  z3_bytes_sent(this->z3data, (word)len);

  return (true);
}

bool ZusiClient::add_needed_data(word subgroup, word id, void *target) {
  if (zusi_add_needed_data(this->z3data, subgroup, id, target) != z3_ok)
    return (false);

  return (true);
}

void ZusiClient::tcp_reader() {

  debug("tcp_reader thread started\r\n");
  nsapi_size_or_error_t sock_ret = NSAPI_ERROR_OK;
  z3_return_code zusi_ret = z3_ok;

  while (this->status > STATE_CLOSED) {
    sock_ret = this->sock->recv(z3_get_buffer(this->z3data, ZUSI_RECV_BUF),
                                z3_buffer_avail(this->z3data, ZUSI_RECV_BUF));

    if (sock_ret > 0) {
      zusi_ret = z3_decode(this->z3data, (word)sock_ret);
    } else if (sock_ret == NSAPI_ERROR_WOULD_BLOCK) {
      if (this->run)
        continue;
      else
        break;
    } else if (sock_ret < 0) {
      debug("socket receive error %d\r\n", sock_ret);
      this->status = STATE_DISPOSE;
      break;

    } else {
      debug("Connection close request from server\r\n");
      this->status = STATE_DISPOSE;
      break;
    }
  }

  debug("tcp_reader thread ended\r\n");
}