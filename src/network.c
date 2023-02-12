#include <hardware/rtc.h>
#include <lwip/dns.h>
#include <lwip/pbuf.h>
#include <lwip/tcp.h>
#include <lwip/timeouts.h>
#include <pico/cyw43_arch.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "network.h"

#define RCVBUFSIZE 2048

/* adjustment for timezone (in seconds).
 * Germany = UTC + 1 hour */
#define OURTZADJ 3600

ip_addr_t dnsrip;
int havednsreply;
int tcpconnected;
int ntpreceived;
int rcvbufpos;
uint8_t rcvbuf[RCVBUFSIZE+1];

/* removes all occurences of char b from string a. */
static void delchar(char * a, char b) {
  char * s; char * d;
  s = a; d = a;
  while (*s != '\0') {
    if (*s != b) {
      *d = *s;
      s++; d++;
    } else {
      s++;
    }
  }
  *s = '\0';
}

/* DNS-finished callback function */
void dnsfcb(const char *name, const ip_addr_t * ipaddr, void * callback_arg)
{
  if (ipaddr == NULL) {
    havednsreply = -1; /* Signal error */
  } else {
    dnsrip = *ipaddr;
    havednsreply = 1; /* Success */
  }
}

/* TCP-connected callback function */
err_t tcpccbf(void * arg, struct tcp_pcb * tpcb, err_t err)
{
  printf("TCP connected!\r\n");
  tcpconnected = 1;
  return ERR_OK;
}

/* TCP Error callback function */
void tcpecbf(void *arg, err_t err)
{
  printf("TCP error callback called - errorcode: %d\r\n", err);
  tcpconnected = -1;
}

/* TCP-receive callback function */
err_t tcprcbf(void * arg, struct tcp_pcb * tpcb, struct pbuf * p, err_t err)
{
  if (p == NULL) {
    printf("end of TCP connection!\r\n");
    tcpconnected = -2;
    return ERR_OK;
  }
  printf("received %d bytes on TCP connection!\r\n", p->tot_len);
  /* Copy data into our receive-buffer */
  int buffer_left = RCVBUFSIZE - rcvbufpos;
  rcvbufpos += pbuf_copy_partial(p, &rcvbuf[rcvbufpos], (buffer_left < p->tot_len) ? buffer_left : p->tot_len, 0);
  /* Now free the pbuf (required, the library does NOT do that for us!) */
  pbuf_free(p);
  return ERR_OK;
}

/* NOTE: That function is obviously not reentrant as it it reusing
 * a static buffer! */
uint8_t * printip(ip_addr_t ip)
{
  static uint8_t pipbuf[20];
  sprintf(pipbuf, "%u.%u.%u.%u",
                  (ip.addr >>  0) & 0xff,
                  (ip.addr >>  8) & 0xff,
                  (ip.addr >> 16) & 0xff,
                  (ip.addr >> 24) & 0xff);
  return &pipbuf[0];
}

sensdata fetchtemphum(uint8_t * host, uint16_t port)
{
  sensdata res;
  res.isvalid = 0;
  havednsreply = 0;
  cyw43_arch_lwip_begin();
  err_t dnsres = dns_gethostbyname(host, &dnsrip, dnsfcb, NULL);
  cyw43_arch_lwip_end();
  if (dnsres == ERR_OK) {
    printf("OK, immediately resolved %s to IP %s\n", host, printip(dnsrip));
  }
  if (dnsres == ERR_INPROGRESS) {
    printf("OK, DNS request has been queued, waiting for a reply.\r\n");
    while (havednsreply == 0) {
      sys_check_timeouts();
    }
    if (havednsreply > 0) {
      printf("Received a reply! Resolved %s to IP %s\r\n", host, printip(dnsrip));
    } else {
      printf("Could not resolve hostname %s.\r\n", host);
      return res;
    }
  }
  tcpconnected = 0;
  struct tcp_pcb * conn = tcp_new();
  tcp_err(conn, tcpecbf);
  rcvbufpos = 0;
  tcp_recv(conn, tcprcbf);
  cyw43_arch_lwip_begin();
  err_t conres = tcp_connect(conn, &dnsrip, port, tcpccbf);
  cyw43_arch_lwip_end();
  if (conres != ERR_OK) {
    printf("Could not queue outgoing TCP connection.\r\n");
    cyw43_arch_lwip_begin();
    tcp_abort(conn);
    cyw43_arch_lwip_end();
    return res;
  }
  while (tcpconnected == 0) {
    sys_check_timeouts();
  }
  if (tcpconnected < 0) {
    printf("Could not establish TCP connection to %s on port %u.\r\n", host, port);
    cyw43_arch_lwip_begin();
    if (tcp_close(conn) != ERR_OK) {
      tcp_abort(conn);
    }
    cyw43_arch_lwip_end();
    return res;
  }
  while (tcpconnected >= 0) {
    sys_check_timeouts();
  }
  cyw43_arch_lwip_begin();
  if (tcp_close(conn) != ERR_OK) {
    printf("Warning: Could not cleanly end TCP connection - calling tcp_abort.\n");
    tcp_abort(conn);
  }
  cyw43_arch_lwip_end();
  rcvbuf[rcvbufpos] = 0;
  printf("Received %d bytes total.\r\n", rcvbufpos);
  printf("Content: %s\r\n", rcvbuf);
  /* Now "parse" this. First, lets get rid of all whitespace. */
  delchar(rcvbuf, '\r');
  delchar(rcvbuf, '\n');
  delchar(rcvbuf, '\t');
  delchar(rcvbuf, ' ');
  char * sp;
  char * pp = strtok_r(rcvbuf, ";", &sp);
  /* The first is the timestamp, we don't care about that at all,
   * so nothing to do here. */
  pp = strtok_r(NULL, ";", &sp);
  if (pp == NULL) { /* Bummer, no temperature here. */
    return res;
  }
  char * ep;
  res.temp = strtod(pp, &ep);
  if (pp == ep) { /* Nothing was converted */
    return res;
  }
  pp = strtok_r(NULL, ";", &sp);
  if (pp == NULL) { /* Bummer, no humidity here. */
    return res;
  }
  res.hum = strtod(pp, &ep);
  if (pp == ep) { /* Nothing was converted */
    return res;
  }
  /* OK, we have successfully extracted temperature and humidity. */
  res.isvalid = 1;
  printf("Successfully parsed: temp = %.2lf, humidity = %.1lf\r\n", res.temp, res.hum);
  return res;
}

int fetchvaluefromwpd(uint32_t sensorid, double * res)
{
  char * host = "wetter.poempelfox.de";
  uint16_t port = 80;
  havednsreply = 0;
  cyw43_arch_lwip_begin();
  err_t dnsres = dns_gethostbyname(host, &dnsrip, dnsfcb, NULL);
  cyw43_arch_lwip_end();
  if (dnsres == ERR_OK) {
    printf("OK, immediately resolved %s to IP %s\n", host, printip(dnsrip));
  }
  if (dnsres == ERR_INPROGRESS) {
    printf("OK, DNS request has been queued, waiting for a reply.\r\n");
    while (havednsreply == 0) {
      sys_check_timeouts();
    }
    if (havednsreply > 0) {
      printf("Received a reply! Resolved %s to IP %s\r\n", host, printip(dnsrip));
    } else {
      printf("Could not resolve hostname %s.\r\n", host);
      return 1;
    }
  }
  tcpconnected = 0;
  struct tcp_pcb * conn = tcp_new();
  tcp_err(conn, tcpecbf);
  rcvbufpos = 0;
  tcp_recv(conn, tcprcbf);
  cyw43_arch_lwip_begin();
  err_t conres = tcp_connect(conn, &dnsrip, port, tcpccbf);
  cyw43_arch_lwip_end();
  if (conres != ERR_OK) {
    printf("Could not queue outgoing TCP connection.\r\n");
    cyw43_arch_lwip_begin();
    tcp_abort(conn);
    cyw43_arch_lwip_end();
    return 1;
  }
  while (tcpconnected == 0) {
    sys_check_timeouts();
  }
  if (tcpconnected < 0) {
    printf("Could not establish TCP connection to %s on port %u.\r\n", host, port);
    cyw43_arch_lwip_begin();
    if (tcp_close(conn) != ERR_OK) {
      tcp_abort(conn);
    }
    cyw43_arch_lwip_end();
    return 1;
  }
  /* Send HTTP request now */
  char httpreq[500];
  sprintf(httpreq, "GET /api/getlastvalue/%u HTTP/1.1\r\n", sensorid);
  sprintf(&httpreq[strlen(httpreq)], "Host: %s\r\n", host);
  strcat(httpreq, "Connection: close\r\n\r\n");
  // We'll just ignore an error on the write - we'll find out soon
  // enough that we get no reply.
  (void)tcp_write(conn, httpreq, strlen(httpreq), 0);
  (void)tcp_output(conn);
  /* Now wait for reply to be sent and connection to be closed. */
  while (tcpconnected >= 0) {
    sys_check_timeouts();
  }
  cyw43_arch_lwip_begin();
  if (tcp_close(conn) != ERR_OK) {
    printf("Warning: Could not cleanly end TCP connection - calling tcp_abort.\n");
    tcp_abort(conn);
  }
  cyw43_arch_lwip_end();
  rcvbuf[rcvbufpos] = 0;
  printf("Received %d bytes total.\r\n", rcvbufpos);
  printf("Content: %s\r\n", rcvbuf);
  /* Now "parse" this. This is going to be "fun". */
  delchar(rcvbuf, '\r');
  rcvbufpos = 0;
  int inheader = 1;
  char thevalue[20];
  strcpy(thevalue, "");
  while (rcvbuf[rcvbufpos] != 0) {
    if (inheader) {
      if ((rcvbuf[rcvbufpos] == '\n') && (rcvbuf[rcvbufpos+1] == '\n')) {
        /* We've found the end of the headers! */
        rcvbufpos++; // advance one more than usual */
        inheader = 0;
      }
    } else { /* Not in the headers anymore. */
      if (strncmp(&rcvbuf[rcvbufpos], "\"v\":", 4) == 0) {
        /* OK, so this is where the value we're searching for is located. */
        int piv = 0;
        char * pp = &rcvbuf[rcvbufpos+4];
        while ((piv < 15)
            && ((*pp == '.')
             || (*pp == '-')
             || ((*pp >= '0') && (*pp <= '9')))) {
          thevalue[piv] = *pp;
          piv++;
          pp++;
        }
        thevalue[piv] = 0;
        break; /* We're done, no need to parse until the end. */
      }
    }
    rcvbufpos++;
  }
  char * afterendptr;
  *res = strtod(thevalue, &afterendptr);
  printf("Extracted value: '%s' - double %.2lf invalid %d\r\n",
         thevalue, *res, (afterendptr == &thevalue[0]));
  if (afterendptr == &thevalue[0]) { /* Conversion was not valid */
    return 1;
  }
  return 0;
}

/* Returns 1 if we are in DST according to EU rules,
 * 0 if we aren't. */
int isdst(struct tm * t)
{
  int month = t->tm_mon + 1;
  int dotw = t->tm_wday; // 0..6, Sunday == 0
  if (month < 3) {
    return 0; // definitely not DST
  }
  if (month > 10) {
    return 0; // definitely not DST
  }
  if ((month > 3) && (month < 10)) {
    return 1; // definitely in DST
  }
  // Now only March and October remain. In these
  // months DST starts or ends, we need to calculate
  // when exactly.
  // Start/End is 1:00 UTC on the last Sunday of the month.
  // what dayoftheweek is the 31st?
  int wd31st = ((31 - t->tm_mday) + dotw) % 7;
  // so what is the last sunday of the month?
  int lsotm = 31 - wd31st;
  if ((t->tm_mday > lsotm)
   || ((t->tm_mday == lsotm) && (t->tm_hour >= 1))) { // Are we past the switch date?
    return (month == 3); // 1 in March, 0 in October
  } else {
    return (month == 10); // 1 in October, 0 in March
  }
}

void ntpudprcb(void * arg, struct udp_pcb * pcb, struct pbuf * p, const ip_addr_t * addr, u16_t port)
{
  uint8_t mode = pbuf_get_at(p, 0) & 0x7;
  uint8_t stratum = pbuf_get_at(p, 1);
  if ((port == 123) && (p->tot_len >= 48)
   && (mode == 4) && (stratum > 0) && (stratum < 5)) { /* That looks like it's for us. */
    uint8_t seconds_buf[4] = { 0, 0, 0, 0 };
    /* The transmit timestamp is at offset 40. We need only the
     * main timestamp, not the fraction that would be at 44. */
    pbuf_copy_partial(p, seconds_buf, sizeof(seconds_buf), 40);
    /* of course, the following line will cause massive problems in the year 2036,
     * which makes the year 2038 problem a few lines down not much of a problem. */
    uint32_t seconds_since_1900 = seconds_buf[0] << 24 | seconds_buf[1] << 16 | seconds_buf[2] << 8 | seconds_buf[3];
    uint32_t seconds_since_1970 = seconds_since_1900 - 2208988800;
    time_t epoch = seconds_since_1970;
    struct tm * utc = gmtime(&epoch);
    printf("got time from NTP: %04d-%02d-%02d %02d:%02d:%02d (UTC)\n",
            utc->tm_year + 1900, utc->tm_mon + 1, utc->tm_mday,
            utc->tm_hour, utc->tm_min, utc->tm_sec);
    epoch += OURTZADJ; // Adjust timezone.
    /* That DST adjustment can be dropped once the EU finally abolishes
     * that DST nonsense. But because a massive mayority of the
     * population wants that, and all experts agree it makes sense,
     * but nobody has handed out any bribes for getting it done, that
     * will take long time, and we're probably hit by the year 2038
     * problem before that. */
    int mesz = 0;
    if (isdst(utc)) {
      mesz = 1;
      epoch += 3600; // in summertime it's 1 hour later.
    }
    utc = gmtime(&epoch);
    printf("that translates to local time: %04d-%02d-%02d %02d:%02d:%02d (%s)\n",
            utc->tm_year + 1900, utc->tm_mon + 1, utc->tm_mday,
            utc->tm_hour, utc->tm_min, utc->tm_sec,
            (mesz) ? "CEST" : "CET");
    /* Now put that time into the Picos RTC. Of course, that uses its
     * own struct that has much the same fields as struct tm, but if they
     * had just reused that, that would have been to easy, so they
     * invented their own. */
    datetime_t curdate;
    curdate.year = utc->tm_year + 1900;
    curdate.month = utc->tm_mon + 1;
    curdate.day = utc->tm_mday;
    curdate.dotw = utc->tm_wday;
    curdate.hour = utc->tm_hour;
    curdate.min = utc->tm_min;
    curdate.sec = utc->tm_sec;
    if (rtc_set_datetime(&curdate)) {
      printf("Successfully put that time into the RTC. Can be queried in >60 ms.\r\n");
    } else {
      printf("Failed to put time into the RTC.\r\n");
    }
    ntpreceived = 1;
  } else {
    printf("Received an invalid ntp response (or something unrelated)\r\n");
    ntpreceived = -1;
  }
}

int settimefromntp(uint8_t * host)
{
  havednsreply = 0;
  cyw43_arch_lwip_begin();
  err_t dnsres = dns_gethostbyname(host, &dnsrip, dnsfcb, NULL);
  cyw43_arch_lwip_end();
  if (dnsres == ERR_OK) {
    printf("OK, immediately resolved %s to IP %s\n", host, printip(dnsrip));
  }
  if (dnsres == ERR_INPROGRESS) {
    printf("OK, DNS request has been queued, waiting for a reply.\r\n");
    while (havednsreply == 0) {
      sys_check_timeouts();
    }
    if (havednsreply > 0) {
      printf("Received a reply! Resolved %s to IP %s\r\n", host, printip(dnsrip));
    } else {
      printf("Could not resolve hostname %s.\r\n", host);
      return 1;
    }
  }
  struct udp_pcb * conn = udp_new();
  if (conn == NULL) {
    printf("failed to allocate UDP PCB\r\n");
    return 2;
  }
  cyw43_arch_lwip_begin();
  err_t udpe = udp_connect(conn, &dnsrip, 123);
  cyw43_arch_lwip_end();
  if (udpe != ERR_OK) {
    printf("UDP connect failed.\r\n");
    cyw43_arch_lwip_begin();
    udp_remove(conn);
    cyw43_arch_lwip_end();
    return 3;
  }
  /* Allocate buffer for the NTP request */
  struct pbuf * ntpreq = pbuf_alloc(PBUF_TRANSPORT, 48, PBUF_RAM);
  /* The simplest possible NTP request is very simple indeed:
   * Just a packet with 48 bytes, and the "version" and "mode"
   * bitfields in byte 0 set to 3 each.
   * The other 47 bytes are 0. */
  memset(ntpreq->payload, 0, 48);
  pbuf_put_at(ntpreq, 0, 0x1b);
  udp_recv(conn, ntpudprcb, NULL);
  ntpreceived = 0;
  cyw43_arch_lwip_begin();
  udpe = udp_send(conn, ntpreq);
  cyw43_arch_lwip_end();
  pbuf_free(ntpreq);
  if (udpe != ERR_OK) {
    printf("UDP send failed.\r\n");
    cyw43_arch_lwip_begin();
    udp_remove(conn);
    cyw43_arch_lwip_end();
    return 4;
  }
  absolute_time_t ntptot = make_timeout_time_ms(2000);
  while (ntpreceived == 0) { /* Wait for reply or timeout */
    sys_check_timeouts();
    if (get_absolute_time() > ntptot) { break; }
  }
  cyw43_arch_lwip_begin();
  udp_remove(conn);
  cyw43_arch_lwip_end();
  if (ntpreceived <= 0) { /* no valid time received. */
    printf("No (valid) time received, timeout\r\n");
    return 5;
  }
  return 0;
}
