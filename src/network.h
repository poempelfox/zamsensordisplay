
#ifndef _NETWORK_H_
#define _NETWORK_H_

struct sensdata {
  int isvalid;
  double temp;
  double hum;
};

typedef struct sensdata sensdata;

sensdata fetchtemphum(uint8_t * host, uint16_t port);
/* Tries to fetch one value from wetter.poempelfox.de.
 * Returns 0 on success. Only then the value
 * in res will be valid. */
int fetchvaluefromwpd(uint32_t sensorid, double * res);

/* Tries to set the Picos time from a NTP server.
 * will return 0 if that succeeded, anything else if not. */
int settimefromntp(uint8_t * host);

#endif /* _NETWORK_H_ */

