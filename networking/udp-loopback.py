#!/usr/bin/python

import argparse
import contextlib
import socket

def main():
    parser = argparse.ArgumentParser(description='UDP Loopback')
    parser.add_argument(
        '--bind',
        metavar='ADDRESS_PORT',
        required=True,
        default=':8888',
        help='Bind address, default %(default)s',
    )
    parser.add_argument(
        '--prefix',
        metavar='STRING',
        default='',
        help='Optional prefix to inject',
    )
    args = parser.parse_args()

    addr, port = args.bind.split(':')
    prefix = args.prefix.encode('UTF-8')

    with contextlib.closing(socket.socket(
        socket.AF_INET,
        socket.SOCK_DGRAM
    )) as sock:
        sock.bind((addr, int(port)))
        while True:
            buf, addr = sock.recvfrom(64*1024)
            sock.sendto(prefix + buf, addr)

if __name__ == '__main__':
    main()
