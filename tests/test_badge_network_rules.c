#include "badge_network_rules.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void){
    assert(badge_network_power_policy(0,false)==0);
    assert(badge_network_power_policy(0,true)==1); /* Home standby, no AI. */
    assert(badge_network_power_policy(1,false)==1);
    assert(badge_network_power_policy(1,true)==1);
    assert(badge_network_power_policy(2,true)==2); /* New voice reply wins. */
    assert(badge_network_power_policy(2,false)==2);
    bool seen=false;
    assert(badge_setup_tick(&seen,false,false,900)==BADGE_SETUP_IDLE);
    assert(badge_setup_tick(&seen,true,false,0)==BADGE_SETUP_START);
    assert(badge_setup_tick(&seen,true,true,3600)==BADGE_SETUP_IDLE);
    assert(badge_setup_tick(&seen,true,false,3600)==BADGE_SETUP_IDLE); /* Respect manual close; OK can reopen. */
    assert(badge_setup_tick(&seen,false,true,3600)==BADGE_SETUP_EXTEND); /* First save finishes onboarding. */
    assert(badge_setup_tick(&seen,false,true,600)==BADGE_SETUP_IDLE);
    assert(badge_setup_tick(&seen,false,true,601)==BADGE_SETUP_IDLE); /* Configuration remains available. */
    assert(badge_setup_tick(&seen,false,true,3600)==BADGE_SETUP_IDLE);
    assert(badge_setup_tick(&seen,true,false,900)==BADGE_SETUP_START); /* Last profile cleared. */
    uint8_t ipv4[4]={192,168,4,1};
    uint8_t mapped[16]={0,0,0,0,0,0,0,0,0,0,255,255,192,168,4,1};
    assert(badge_address_is_ap(ipv4,4));assert(badge_address_is_ap(mapped,16));
    ipv4[2]=1;assert(!badge_address_is_ap(ipv4,4));
    mapped[10]=0;assert(!badge_address_is_ap(mapped,16));
    mapped[10]=255;mapped[15]=2;assert(!badge_address_is_ap(mapped,16));
    assert(!badge_address_is_ap(NULL,4));assert(!badge_address_is_ap(ipv4,3));
    badge_scan_entry_t list[BADGE_SCAN_LIMIT],entry={.ssid="Network",.rssi=-70,.auth=3};size_t count=0;
    badge_scan_insert(list,&count,&entry);assert(count==1);
    entry.rssi=-80;badge_scan_insert(list,&count,&entry);assert(count==1&&list[0].rssi==-70);
    entry.rssi=-40;badge_scan_insert(list,&count,&entry);assert(count==1&&list[0].rssi==-40);
    entry.auth=0;badge_scan_insert(list,&count,&entry);assert(count==2);
    entry.ssid[0]=0;badge_scan_insert(list,&count,&entry);assert(count==2);
    for(int i=0;i<30;i++){snprintf(entry.ssid,sizeof(entry.ssid),"AP-%d",i);entry.rssi=-90+i;badge_scan_insert(list,&count,&entry);}
    assert(count==BADGE_SCAN_LIMIT);
    for(size_t i=1;i<count;i++)assert(list[i-1].rssi>=list[i].rssi);
    assert(list[0].rssi==-40&&list[count-1].rssi==-74);
    puts("Network interface guard: PASS (IPv4, mapped IPv6, non-AP rejection)");
}
