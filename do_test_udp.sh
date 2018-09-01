#!/bin/bash
send_num=40000000
dst_addr="192.168.6.51"

echo "dst_addr:$dst_addr send_num:$send_num"
for packet_size in 128 256 512 1024 1500; do
    for i in {0..19}; do
        ./test_packet.out $dst_addr $packet_size $send_num udp >> ip_gre_result.csv
        echo -n "$i "
    done
    echo "\npacket $packet_size done"
done
