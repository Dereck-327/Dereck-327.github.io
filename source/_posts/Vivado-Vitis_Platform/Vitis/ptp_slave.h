#pragma once
#include "lwip/pbuf.h"

#define PTP_SALVE_TEST

int InitPtp();

void PtpPacketHandler(struct pbuf *p);
void IEEE1588Ptpd();