/*
 * Software License Agreement (MIT License)
 *
 * Copyright (c) 2017, DUKELEC, Inc.
 * All rights reserved.
 *
 * Author: Duke Fong <d@d-l.io>
 */

#include "cdnet.h"


static int get_port_size(uint8_t val, uint8_t *src_size, uint8_t *dst_size)
{
    switch (val) {
    case 0x00: *src_size = 0; *dst_size = 1; break;
    case 0x01: *src_size = 0; *dst_size = 2; break;
    case 0x02: *src_size = 1; *dst_size = 0; break;
    case 0x03: *src_size = 2; *dst_size = 0; break;
    case 0x04: *src_size = 1; *dst_size = 1; break;
    case 0x05: *src_size = 1; *dst_size = 2; break;
    case 0x06: *src_size = 2; *dst_size = 1; break;
    case 0x07: *src_size = 2; *dst_size = 2; break;
    default: return -1;
    }
    return 0;
}

static int cal_port_val(uint16_t src, uint16_t dst,
        uint8_t *src_size, uint8_t *dst_size)
{
    if (src == CDN_DEF_PORT)
        *src_size = 0;
    else if (src <= 0xff)
        *src_size = 1;
    else
        *src_size = 2;

    if (dst == CDN_DEF_PORT && src != CDN_DEF_PORT)
        *dst_size = 0;
    else if (dst <= 0xff)
        *dst_size = 1;
    else
        *dst_size = 2;

    switch ((*src_size << 4) | *dst_size) {
    case 0x01: return 0x00;
    case 0x02: return 0x01;
    case 0x10: return 0x02;
    case 0x20: return 0x03;
    case 0x11: return 0x04;
    case 0x12: return 0x05;
    case 0x21: return 0x06;
    case 0x22: return 0x07;
    default: return -1;
    }
}


int cdn1_to_payload(const cdn_pkt_t *pkt, uint8_t *payload)
{
    uint8_t s_port_size, d_port_size;
    const cdn_sockaddr_t *src = &pkt->src;
    const cdn_sockaddr_t *dst = &pkt->dst;
    uint8_t *buf = payload + 1;
    cdn_multi_t multi = CDN_MULTI_NONE;

    multi |= (src->addr[0] & 0xf0) == 0xa0 ? CDN_MULTI_NET : CDN_MULTI_NONE;
    multi |= (dst->addr[0] & 0xf0) == 0xf0 ? CDN_MULTI_CAST : CDN_MULTI_NONE;
    *payload = CDN_HDR_L1L2 | (multi << 4); // hdr

    if (multi & CDN_MULTI_NET) {
        *buf++ = src->addr[1];
        *buf++ = src->addr[2];
    }
    if (multi != CDN_MULTI_NONE) {
        *buf++ = dst->addr[1];
        *buf++ = dst->addr[2];
    }
    if (dst->addr[0] & 8) {
        *payload |= CDN_HDR_L1L2_SEQ;
        *buf++ = pkt->seq;
    }

    *payload |= cal_port_val(src->port, dst->port, &s_port_size, &d_port_size);
    if (s_port_size >= 1)
        *buf++ = src->port & 0xff;
    if (s_port_size == 2)
        *buf++ = src->port >> 8;
    if (d_port_size >= 1)
        *buf++ = dst->port & 0xff;
    if (d_port_size == 2)
        *buf++ = dst->port >> 8;

    cdn_assert(buf - payload + pkt->len <= 253);
    memcpy(buf, pkt->dat, pkt->len);
    return buf - payload + pkt->len;
}

// addition in: seq, _s_mac, _d_mac
int cdn1_to_frame(const cdn_pkt_t *pkt, uint8_t *frame)
{
    frame[0] = pkt->_s_mac;
    frame[1] = pkt->_d_mac;
    int ret = cdn1_to_payload(pkt, frame + 3);
    if (ret < 0)
        return ret;
    frame[2] = ret;
    return 0;
}


int cdn1_from_payload(const uint8_t *payload, uint8_t len, cdn_pkt_t *pkt)
{
    uint8_t s_port_size, d_port_size;
    cdn_sockaddr_t *src = &pkt->src;
    cdn_sockaddr_t *dst = &pkt->dst;

    const uint8_t *buf = payload + 1;
    cdn_assert((*payload & 0xc0) == 0x80);

    bool seq = !!(*payload & CDN_HDR_L1L2_SEQ);
    cdn_multi_t multi = (*payload >> 4) & 3;

    if (multi & CDN_MULTI_NET) {
        src->addr[0] = seq ? 0xa8 : 0xa0;
        src->addr[1] = *buf++;
        src->addr[2] = *buf++;
    } else {
        src->addr[0] = seq ? 0x88 : 0x80;
        src->addr[1] = pkt->_l_net;
        src->addr[2] = pkt->_s_mac;
    }
    if (multi != CDN_MULTI_NONE) {
        dst->addr[0] = ((multi & CDN_MULTI_CAST) ? 0xf0 : 0xa0) | (seq ? 8 : 0);
        dst->addr[1] = *buf++;
        dst->addr[2] = *buf++;
    } else {
        dst->addr[0] = seq ? 0x88 : 0x80;
        dst->addr[1] = pkt->_l_net;
        dst->addr[2] = pkt->_d_mac;
    }
    if (seq)
        pkt->seq = *buf++;

    get_port_size(*payload & 0x07, &s_port_size, &d_port_size);
    if (s_port_size == 0) {
        src->port = CDN_DEF_PORT;
    } else {
        if (s_port_size >= 1)
            src->port = *buf++;
        if (s_port_size == 2)
            src->port |= *buf++ << 8;
    }
    if (d_port_size == 0) {
        dst->port = CDN_DEF_PORT;
    } else {
        if (d_port_size >= 1)
            dst->port = *buf++;
        if (d_port_size == 2)
            dst->port |= *buf++ << 8;
    }

    pkt->len = len - (buf - payload);
    memcpy(pkt->dat, buf, pkt->len);
    return 0;
}

// addition in: _l_net; out: seq
int cdn1_from_frame(const uint8_t *frame, cdn_pkt_t *pkt)
{
    pkt->_s_mac = frame[0];
    pkt->_d_mac = frame[1];
    return cdn1_from_payload(frame + 3, frame[2], pkt);
}
