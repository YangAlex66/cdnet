/*
 * Software License Agreement (MIT License)
 *
 * Copyright (c) 2017, DUKELEC, Inc.
 * All rights reserved.
 *
 * Author: Duke Fong <d@d-l.io>
 */

#include "cdnet.h"


int cdn2_to_payload(const cdn_packet_t *pkt, uint8_t *payload)
{
    const uint8_t *s_addr = &pkt->src.addr;
    uint8_t *buf = payload + 1;

    cdn_assert((pkt->l2_uf & ~7) == 0);

    bool seq = !!(s_addr[0] & 8);
    uint8_t mtu = CDN2_MTU - (seq ? 2 : 1);
    cdn_assert(s_addr[0] == 0xc0 || s_addr[0] == 0xc8);

    *payload = CDN_HDR_L1L2 | CDN_HDR_L2 | pkt->l2_uf;

    if (pkt->len - pkt->_l2_pos <= mtu)
        pkt->_l2_frag = pkt->_l2_pos ? CDN_FRAG_LAST : CDN_FRAG_NONE;
    else
        pkt->_l2_frag = pkt->_l2_pos ? CDN_FRAG_MORE : CDN_FRAG_FIRST;

    if (seq) {
        *payload |= CDN_HDR_L1L2_SEQ;
        *buf++ = pkt->_seq;
    }
    if (pkt->_l2_frag) {
        cdn_assert(seq);
        *payload |= pkt->_l2_frag << 4;
    }

    const uint8_t *dat = pkt->dat + pkt->_l2_pos;
    uint8_t len = min(pkt->len - pkt->_l2_pos, mtu);

    cdn_assert(buf - payload + len <= 253);
    memcpy(buf, dat, len);
    pkt->_l2_pos += len;
    return buf - payload + len;
}

// addition in: _seq, _l2_pos, l2_uf; out: _l2_pos, _l2_frag
int cdn2_to_frame(const cdn_packet_t *pkt, uint8_t *frame)
{
    frame[0] = pkt->src.addr[2];
    frame[1] = pkt->dst.addr[2];
    int ret = cdn2_to_payload(pkt, frame + 3);
    if (ret < 0)
        return ret;
    frame[2] = ret;
    return 0;
}


int cdn2_from_payload(const uint8_t *payload, uint8_t len, cdn_packet_t *pkt)
{
    uint8_t *s_addr = &pkt->src.addr;
    uint8_t *d_addr = &pkt->dst.addr;
    const uint8_t *buf = payload + 1;

    cdn_assert((*payload & 0xc0) == 0xc0);
    bool seq = !!(*payload & CDN_HDR_L1L2_SEQ);
    pkt->l2_uf = *payload & 7; // hdr

    s_addr[0] = seq ? 0xc8 : 0xc0;
    d_addr[0] = s_addr[0];
    s_addr[1] = pkt->_l_net;
    d_addr[1] = pkt->_l_net;
    s_addr[2] = pkt->_s_mac;
    d_addr[2] = pkt->_d_mac;

    if (*payload & 0x30) {
        cdn_assert(seq);
        pkt->_l2_frag = (*payload >> 4) & 3;
    } else {
        pkt->_l2_frag = CDN_FRAG_NONE;
    }

    if (seq)
        pkt->_seq = *buf++;

    pkt->len = len - (seq ? 2 : 1);
    memcpy(pkt->dat, buf, pkt->len);
    return 0;
}

// addition in: _l_net; out: _seq, _l2_frag, l2_uf
int cdn2_from_frame(const uint8_t *frame, cdn_packet_t *pkt)
{
    pkt->_s_mac = frame[0];
    pkt->_d_mac = frame[1];
    return cdn2_from_payload(frame + 3, frame[2], pkt);
}
