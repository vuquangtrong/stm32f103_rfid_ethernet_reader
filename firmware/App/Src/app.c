#include "app.h"
#include "enc28j60.h"
#include "main.h"
#include "mfrc522.h"

#include <lwip/dhcp.h>
#include <lwip/dns.h>
#include <lwip/err.h>
#include <lwip/etharp.h>
#include <lwip/init.h>
#include <lwip/netif.h>
#include <lwip/pbuf.h>
#include <lwip/tcp.h>
#include <lwip/timeouts.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

extern uint32_t tick_count;

uint32_t sys_now(void) {
    return tick_count;
}

static uint8_t mac_addr[6];
static struct netif eth0;
static uint8_t pkt_buf[ENC28J60_MAXFRAME];

static void low_level_init(struct netif *netif) {
    /* set MAC hardware address */
    netif->hwaddr_len = ETHARP_HWADDR_LEN;
    netif->hwaddr[0] = mac_addr[0];
    netif->hwaddr[1] = mac_addr[1];
    netif->hwaddr[2] = mac_addr[2];
    netif->hwaddr[3] = mac_addr[3];
    netif->hwaddr[4] = mac_addr[4];
    netif->hwaddr[5] = mac_addr[5];

    /* maximum transfer unit */
    netif->mtu = ENC28J60_MAXFRAME;

    /* hardware initialization */
    enc28j60_init(mac_addr);
    printf("MAC = %02X:%02X:%02X:%02X:%02X:%02X\n",
           mac_addr[0],
           mac_addr[1],
           mac_addr[2],
           mac_addr[3],
           mac_addr[4],
           mac_addr[5]);
    uint16_t phid1 = enc28j60_read_phy(PHID1);
    printf("ID1 = 0x%04X\n", phid1);
    uint16_t phid2 = enc28j60_read_phy(PHID2);
    printf("ID2 = 0x%04X\n", phid2);
    uint8_t erevid = enc28j60_rcr(EREVID);
    printf("REV = 0x%02X\n", erevid);

    /* device capabilities */
    /* don't set NETIF_FLAG_ETHARP if this device is not an ethernet one */
    netif->flags |= NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_ETHERNET | NETIF_FLAG_LINK_UP;
}

static err_t low_level_output(struct netif *netif, struct pbuf *p) {
    (void)netif;
    struct pbuf *q;
    /* Send the data from the pbuf to the interface, one pbuf at a
       time. The size of the data in each pbuf is kept in the ->len
       variable. */
    for (q = p; q != NULL; q = q->next) {
        /* Send the data from the pbuf to the interface */
        enc28j60_send_packet(q->payload, q->len);
    }

    return ERR_OK;
}

static struct pbuf *low_level_input(struct netif *netif) {
    (void)netif;

    u16_t len = enc28j60_recv_packet(pkt_buf, ENC28J60_MAXFRAME);
    if (len == 0) {
        return NULL;
    }

    /* We allocate a pbuf chain of pbufs from the pool. */
    struct pbuf *p = NULL;
    p = pbuf_alloc(PBUF_RAW, len, PBUF_POOL);
    if (p != NULL) {
        memcpy(p->payload, pkt_buf, len);
    }
    return p;
}

static void ethernetif_input(struct netif *netif) {
    struct pbuf *p;
    /* move received packet into a new pbuf */
    while (p = low_level_input(netif)) {
        /* pass all packets to ethernet_input, which decides what packets it supports */
        if (netif->input(p, netif) != ERR_OK) {
            pbuf_free(p);
            p = NULL;
        }
    }
}

static err_t ethernetif_init(struct netif *netif) {
    /* Interface name */
    netif->name[0] = 'e';
    netif->name[1] = 'n';

    /* Output methods */
    netif->output = etharp_output;
    netif->linkoutput = low_level_output;

    /* Hardware init */
    low_level_init(netif);

    return ERR_OK;
}

static void eth_init() {
    /* Set IP Address and processing methods */
    netif_add(&eth0, IP4_ADDR_ANY, IP4_ADDR_ANY, IP4_ADDR_ANY, NULL, &ethernetif_init, &netif_input);

    /* Set the default interface */
    netif_set_default(&eth0);

    /* When Link (HW) is up, process to set interface up */
    if (netif_is_link_up(&eth0)) {
        netif_set_up(&eth0);
    }

    /* Start DHCP negotiation */
    dhcp_start(&eth0);
}

typedef enum {
    TYPE_PING = 0,
    TYPE_CARD = 1,
} send_type_t;

static uint8_t card_buf[MF_BLOCK_SIZE + 2] = {0};
static uint8_t data_buf[8] = {0};

static void send_data(send_type_t type) {
    data_buf[3] = type;
    struct pbuf *pbuf = pbuf_alloc(PBUF_TRANSPORT, sizeof(data_buf), PBUF_RAM);
    if (pbuf != NULL) {
        memcpy(pbuf->payload, data_buf, sizeof(data_buf));
        ip_addr_t dest_ip;
        IP4_ADDR(&dest_ip, 255, 255, 255, 255);
        struct udp_pcb *broadcast_udp_pcb = udp_new();
        if (broadcast_udp_pcb != NULL) {
            err_t err = udp_sendto(broadcast_udp_pcb, pbuf, &dest_ip, 12345);
            if (err != ERR_OK) {
                printf("udp_sendto err: %d\n", err);
            }
            udp_remove(broadcast_udp_pcb);
        } else {
            printf("upd_new failed\n");
        }
        pbuf_free(pbuf);
    } else {
        printf("pbuf_alloc failed\n");
    }
}

__attribute__((noreturn)) void app_main(void) {
    setbuf(stdout, NULL);
    printf("\n");

    LL_SYSTICK_EnableIT();

    uint32_t UID0 = LL_GetUID_Word0();
    uint32_t UID1 = LL_GetUID_Word1();
    uint32_t UID2 = LL_GetUID_Word2();
    mac_addr[0] = ETH_MAC_ADDR_0;
    mac_addr[1] = ETH_MAC_ADDR_1;
    mac_addr[2] = ETH_MAC_ADDR_2;
    mac_addr[3] = (UID0 & 0xFF);
    mac_addr[4] = (UID1 & 0xFF);
    mac_addr[5] = (UID2 & 0xFF);

    data_buf[0] = mac_addr[3];
    data_buf[1] = mac_addr[4];
    data_buf[2] = mac_addr[5];
    data_buf[3] = 0x00;

    lwip_init();
    eth_init();
    mfrc522_init();

    uint32_t last_ping_tick = sys_now();
    uint32_t last_rfid_tick = sys_now();
    while (1) {
        /* read RFID Card */
        uint8_t status = mfrc522_request(PICC_REQIDL, card_buf);
        if (status == MI_OK) {
            status = mfrc522_anti_collision(card_buf);
            if (status == MI_OK) {
                // uchar size = mfrc522_select_tag(card_buf);
                // mfrc522_halt();
                if (htonl(*(uint32_t *)card_buf) != htonl(*(uint32_t *)&data_buf[4])) {
                    data_buf[4] = card_buf[0];
                    data_buf[5] = card_buf[1];
                    data_buf[6] = card_buf[2];
                    data_buf[7] = card_buf[3];
                    printf("SNDUID\n");
                    send_data(TYPE_CARD);
                }
            }
        }

        /* read Ethernet packets */
        ethernetif_input(&eth0);
        sys_check_timeouts();

        /* internal routines */
        if (sys_now() - last_ping_tick >= 10000) {
            data_buf[4] = 0xFF;
            data_buf[5] = 0xFF;
            data_buf[6] = 0xFF;
            data_buf[7] = 0xFF;
            printf("SNDALV\n");
            send_data(TYPE_PING);
            last_ping_tick = sys_now();
        }

        if (sys_now() - last_rfid_tick >= 3000) {
            data_buf[4] = data_buf[5] = data_buf[6] = data_buf[7] = 0;
            printf("CLRUID\n");
            last_rfid_tick = sys_now();
        }
    }
}
